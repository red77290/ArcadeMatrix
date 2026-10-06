#include "MqttDataEngine.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_heap_caps.h>
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"
#include "../core/SpiRamJsonDocument.h"
#include "mqttdata/GraphRenderer.h"
#include "mqttdata/ValuesRenderer.h"
#include "mqttdata/WeatherPageRenderer.h"
#include "mqttdata/PanelText.h"

extern ConfigLoader config;

namespace {

// Per-activation buffers go to PSRAM when the board has it, else to the internal heap (the same
// fallback SpiRamJsonDocument uses), so the engine itself never asks which board it runs on.
void* dataAlloc(size_t bytes) {
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(bytes);
}
void dataRelease(void* p) { free(p); }
const mqd::Allocator kAllocator{dataAlloc, dataRelease};

constexpr uint32_t kNoDataAfterMs = 3000;      ///< a topic with no retained payload by then shows NO DATA
constexpr uint32_t kRetryMs = 5000;            ///< reconnect interval while on screen
constexpr uint32_t kStopWaitMs = 3000;         ///< shutdownForDestruction: how long to wait for the task

}  // namespace

MqttDataEngine::~MqttDataEngine() {
    // Normally the task is long gone (deactivate + shutdownForDestruction). If not, it must not be
    // left running on a freed object: stop it as the Weather engine does, then free what remains.
    m_wantActive.store(false, std::memory_order_release);
    for (uint32_t t = 0; t < kStopWaitMs && m_taskAlive.load(std::memory_order_acquire); t += 10) vTaskDelay(pdMS_TO_TICKS(10));
    if (m_taskAlive.load(std::memory_order_acquire) && m_task) {
        LOGW("MqttData", "task did not stop in time; deleting it");
        vTaskDelete(m_task);
    }
    m_session.destroy();
}

EngineError MqttDataEngine::initialize(EngineContext* context, const EngineConfig* cfg) {
    if (!context || !context->getMatrix()) return EngineError::InitializationFailed;
    m_matrix = context->getMatrix();
    m_hasPsram = context->hasPsram();   // HardwareHAL's answer; sizes every activation (mqd::limitsFor)
    onConfigChanged(cfg);
    return EngineError::OK;
}

void MqttDataEngine::onConfigChanged(const EngineConfig* cfg) {
    if (!cfg) return;
    const mqd::Limits limits = mqd::limitsFor(m_hasPsram);
    String list = cfg->getString("topics", "");
    const uint8_t cur = m_topicsIdx.load(std::memory_order_relaxed);
    const uint8_t spare = cur ^ 1;
    uint8_t tooLong = 0;
    m_topicCount[spare] = mqd::parseTopics(list.c_str(), m_topics[spare], limits.maxTopics, &tooLong);
    if (tooLong) LOGW("MqttData", "%u topic(s) longer than %u characters ignored", tooLong, (unsigned)(mqd::TOPIC_LEN - 1));
    // Every settings save notifies every engine; reconnect only when this instance's topics changed.
    bool changed = m_topicCount[spare] != m_topicCount[cur];
    for (uint8_t i = 0; !changed && i < m_topicCount[spare]; i++) changed = strcmp(m_topics[spare][i], m_topics[cur][i]) != 0;
    if (changed) {
        m_topicsIdx.store(spare, std::memory_order_release);
        m_configGen.fetch_add(1, std::memory_order_acq_rel);   // a running task reconnects with the new list
        m_cycle = mqd::Cycle();
        m_cycle.sinceMs = millis();
        m_viewTopic = -1;
    }
    requestRedraw();
}

void MqttDataEngine::activate() {
    m_activatedAt = millis();
    // Until payloads say otherwise, every expected page counts DEFAULT_SECONDS.
    const uint8_t topics = m_topicCount[m_topicsIdx.load(std::memory_order_acquire)];
    m_cycleMs = (uint32_t)(topics ? topics : 1) * mqd::DEFAULT_SECONDS * 1000u;
    m_cycle = mqd::Cycle();
    m_cycle.sinceMs = m_activatedAt;
    m_viewTopic = -1;
    m_viewSeq = 0;
    m_notice = Notice::None;
    requestRedraw();
    m_wantActive.store(true, std::memory_order_release);
    startTask();
}

void MqttDataEngine::deactivate() {
    // State only (Core 1 must not block): the task sees this, disconnects, frees and exits.
    m_wantActive.store(false, std::memory_order_release);
}

bool MqttDataEngine::shutdownForDestruction() {
    m_wantActive.store(false, std::memory_order_release);
    for (uint32_t t = 0; t < kStopWaitMs && m_taskAlive.load(std::memory_order_acquire); t += 10) vTaskDelay(pdMS_TO_TICKS(10));
    if (m_taskAlive.load(std::memory_order_acquire)) {
        LOGE("MqttData", "task failed to stop within %u ms", (unsigned)kStopWaitMs);
        return false;   // quarantined: never delete an object its task still uses
    }
    return true;
}

void MqttDataEngine::startTask() {
    // Exactly one of activate() and a task that is just finishing wins this exchange, so a quick
    // deactivate/activate pair either keeps the old task running or starts a new one, never neither.
    if (m_taskAlive.exchange(true, std::memory_order_acq_rel)) return;
    m_link.store(Link::Connecting, std::memory_order_release);
    if (xTaskCreatePinnedToCore(taskEntry, "mqtt_data", 6144, this, 1, &m_task, 0) != pdPASS) {
        m_task = nullptr;
        m_taskAlive.store(false, std::memory_order_release);
        m_link.store(Link::Failed, std::memory_order_release);
        LOGE("MqttData", "task did not start");
    }
}

void MqttDataEngine::taskEntry(void* arg) {
    static_cast<MqttDataEngine*>(arg)->runTask();
}

void MqttDataEngine::runTask() {
    for (;;) {
        runSession();   // returns once the slot left the screen or the settings changed
        if (m_wantActive.load(std::memory_order_acquire)) continue;   // settings changed: reconnect
        m_taskAlive.store(false, std::memory_order_release);
        // activate() may have come in between: if it saw the task still alive it did not start a
        // new one, so this one carries on.
        if (m_wantActive.load(std::memory_order_acquire) && !m_taskAlive.exchange(true, std::memory_order_acq_rel)) continue;
        vTaskDelete(nullptr);   // m_task is left alone: a newer task may already own it
        return;
    }
}

void MqttDataEngine::runSession() {
    const uint32_t heapBefore = ESP.getFreeHeap();
    const uint32_t psramBefore = ESP.getFreePsram();
    const uint32_t gen = m_configGen.load(std::memory_order_acquire);
    const uint8_t idx = m_topicsIdx.load(std::memory_order_acquire);
    const mqd::Limits limits = mqd::limitsFor(m_hasPsram);
    m_link.store(Link::Connecting, std::memory_order_release);

    bool ok = m_topicCount[idx] > 0 && m_session.create(limits, m_topics[idx], m_topicCount[idx], kAllocator);
    SpiRamJsonDocument* doc = ok ? new SpiRamJsonDocument(limits.jsonBytes) : nullptr;
    if (!doc || doc->capacity() == 0) {
        LOGE("MqttData", "out of memory for %u topic(s)", m_topicCount[idx]);
        delete doc;
        m_session.destroy();
        m_link.store(Link::Failed, std::memory_order_release);
        while (m_wantActive.load(std::memory_order_acquire) && gen == m_configGen.load(std::memory_order_acquire)) vTaskDelay(pdMS_TO_TICKS(100));
        m_link.store(Link::Idle, std::memory_order_release);
        return;
    }
    m_live.store(&m_session, std::memory_order_seq_cst);
    LOGI("MqttData", "activate: %u topic(s), heap %u -> %u, psram %u -> %u", m_session.pageCount(),
         (unsigned)heapBefore, (unsigned)ESP.getFreeHeap(), (unsigned)psramBefore, (unsigned)ESP.getFreePsram());

    {
        WiFiClient net;
        PubSubClient client(net);
        client.setBufferSize((uint16_t)(limits.payloadBytes + 256));   // topic + MQTT header headroom
        client.setKeepAlive(30);
        client.setSocketTimeout(3);
        client.setCallback([this, doc](char* topic, uint8_t* payload, unsigned int length) {
            m_session.handle(topic, reinterpret_cast<char*>(payload), length, *doc);
        });

        DataMqttConfig dm;
        String clientId;
        {
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            dm = guard->dataMqtt;
            const String& host = guard->wifi.hostname.length() ? guard->wifi.hostname : guard->mqtt.deviceName;
            clientId = (host.length() ? host : String("arcadematrix")) + "-data";
        }
        client.setServer(dm.broker.c_str(), (uint16_t)dm.port);

        uint32_t nextTry = 0;
        while (m_wantActive.load(std::memory_order_acquire) && gen == m_configGen.load(std::memory_order_acquire)) {
            if (client.connected()) {
                client.loop();   // messages arrive through the callback above, on this task
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (m_link.load(std::memory_order_acquire) == Link::Connected) m_link.store(Link::Failed, std::memory_order_release);
            if ((int32_t)(millis() - nextTry) < 0 || WiFi.status() != WL_CONNECTED || dm.broker.isEmpty()) {
                if (dm.broker.isEmpty()) m_link.store(Link::Failed, std::memory_order_release);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            bool connected = dm.user.length() ? client.connect(clientId.c_str(), dm.user.c_str(), dm.pass.c_str())
                                              : client.connect(clientId.c_str());
            for (uint8_t i = 0; connected && i < m_session.pageCount(); i++) connected = client.subscribe(m_session.topic(i));
            if (connected) {
                m_connectedAt.store(millis(), std::memory_order_release);
                m_link.store(Link::Connected, std::memory_order_release);
                LOGI("MqttData", "connected to %s:%d as %s", dm.broker.c_str(), dm.port, clientId.c_str());
            } else {
                LOGW("MqttData", "cannot reach %s:%d (state %d)", dm.broker.c_str(), dm.port, client.state());
                if (client.connected()) client.disconnect();
                m_link.store(Link::Failed, std::memory_order_release);
                nextTry = millis() + kRetryMs;
            }
        }
        if (client.connected()) client.disconnect();   // the broker drops the subscriptions with it
    }

    // Withdraw the pages, wait out a render-loop read in progress, then free everything.
    m_live.store(nullptr, std::memory_order_seq_cst);
    while (m_readers.load(std::memory_order_seq_cst) != 0) vTaskDelay(pdMS_TO_TICKS(1));
    delete doc;
    m_session.destroy();
    m_link.store(Link::Idle, std::memory_order_release);
    LOGI("MqttData", "deactivate: heap %u, psram %u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

void MqttDataEngine::update(EngineContext* context) {
    m_presented = false;
    if (!m_matrix) return;
    m_readers.fetch_add(1, std::memory_order_seq_cst);
    mqd::Session* s = m_live.load(std::memory_order_seq_cst);
    if (s) {
        draw(*s);
    } else {
        Notice n = m_link.load(std::memory_order_acquire) == Link::Failed ? Notice::NoConnection : Notice::Connecting;
        if (n != m_notice) { m_notice = n; requestRedraw(); }
        if (m_redrawFrames) {
            m_redrawFrames--;
            m_matrix->fillScreen(0);
            drawNotice(m_notice);
            m_presented = true;
        }
    }
    m_readers.fetch_sub(1, std::memory_order_seq_cst);
}

void MqttDataEngine::draw(mqd::Session& s) {
    const uint8_t pages = s.pageCount();
    if (pages == 0) return;
    const uint32_t now = millis();
    if (m_cycle.topic >= pages) { m_cycle = mqd::Cycle(); m_cycle.sinceMs = now; }

    auto refresh = [&]() {
        // Re-copy the current topic's page when it changed or the cycle moved to another topic.
        const uint32_t seq = s.sequence(m_cycle.topic);
        if (m_viewTopic == m_cycle.topic && seq == m_viewSeq) return true;
        if (!s.snapshot(m_cycle.topic)) return false;   // being written: try again next frame
        m_viewTopic = m_cycle.topic;
        m_viewSeq = seq;
        const uint8_t subs = mqd::subPageCount(s.view());
        if (m_cycle.sub >= subs) m_cycle.sub = 0;
        requestRedraw();
        return true;
    };
    if (!refresh()) return;
    m_cycleMs = s.cycleMs();   // grows or shrinks as payloads arrive (weather pages, "seconds")
    const uint32_t pageMs = (uint32_t)s.view().seconds * 1000u;
    if (now - m_cycle.sinceMs >= pageMs) {
        mqd::advance(m_cycle, pages, mqd::subPageCount(s.view()), now);
        requestRedraw();
        if (!refresh()) return;
    }

    // What to show when the page has nothing: still connecting, no broker, or no retained payload.
    const mqd::View& v = s.view();
    Notice n = Notice::None;
    if (v.kind == feed::Kind::Unsupported) {
        n = Notice::Unsupported;
    } else if (v.kind == feed::Kind::None) {
        const Link link = m_link.load(std::memory_order_acquire);
        if (link == Link::Failed) n = Notice::NoConnection;
        else if (link != Link::Connected || now - m_connectedAt.load(std::memory_order_acquire) < kNoDataAfterMs) n = Notice::Connecting;
        else n = Notice::NoData;
    }
    if (n != m_notice) { m_notice = n; requestRedraw(); }

    if (m_redrawFrames == 0) return;   // both DMA buffers already show this page
    m_redrawFrames--;
    m_matrix->fillScreen(0);
    switch (v.kind) {
        case feed::Kind::Graph:   graph_renderer::drawGraph(m_matrix, v.graph, v.graph.showHeader); break;
        case feed::Kind::Value:
        case feed::Kind::Table:   values_renderer::drawValues(m_matrix, v.values, v.values.showTitle); break;
        case feed::Kind::Weather: weather_page::draw(m_matrix, v.weather, m_cycle.sub); break;
        default: drawNotice(m_notice); break;
    }
    m_presented = true;
}

void MqttDataEngine::drawNotice(Notice n) {
    if (n == Notice::NoData) { panel_text::drawNoData(m_matrix); return; }
    if (n == Notice::Connecting) { panel_text::drawNotice(m_matrix, panel_text::Message::Connecting); return; }
    if (n == Notice::NoConnection) { panel_text::drawNotice(m_matrix, panel_text::Message::NoConnection); return; }
    if (n == Notice::Unsupported) { panel_text::drawNotice(m_matrix, panel_text::Message::Unsupported); return; }
}

EngineDescriptor MqttDataEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"mqttdata", "MQTT Data", "info", FIRMWARE_VERSION};
    desc.capabilities.realtime = false;
    desc.capabilities.selfPaced = true;   // time on screen = one cycle of its pages (payload "seconds")
    desc.requirements.needsNetwork = true;
    // Only the subscriptions are configured here; everything else comes from the payloads.
    desc.schema.fields = {
        ConfigField("topics", ConfigType::STRING, "MQTT Topics", "Comma-separated full topics, one page each (up to 6 with PSRAM, 4 without)", "", true, "", "", "", "", "", false, "", ValidationPolicy::Accept)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MqttDataEngine()); };
    return desc;
}
