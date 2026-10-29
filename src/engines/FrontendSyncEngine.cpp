#include "FrontendSyncEngine.h"
#include "WebUiSyncEngine.h"
#include <ArduinoJson.h>
#include <cstring>
#include <cstdlib>
#include "../core/drawing/IDrawingSurface.h"
#include "../core/drawing/DmdzStreamDecoder.h"
#include "../core/SDUtils.h"
#include "../core/Logger.h"
#include "../core/Globals.h"
#include "../core/SdLockGuard.h"
#include "../core/NetworkBudget.h"
#include "../core/net/SecureHttpClient.h"
#include "../core/net/ShowcaseDiscovery.h"
#include "../hal/HardwareHAL.h"
#include <esp_task_wdt.h>
#if defined(ESP32)
#include <esp_heap_caps.h>
#if !defined(UNIT_TEST)
#include <ESPmDNS.h>
#endif
#endif

extern ConfigLoader config;

namespace {

void copySafeCStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

bool isLoopbackOrEmptyHost(const char* h) {
    if (!h || h[0] == '\0') return true;
    return (strcmp(h, "127.0.0.1") == 0 ||
            strcasecmp(h, "localhost") == 0 ||
            strcmp(h, "0.0.0.0") == 0 ||
            strcmp(h, "::1") == 0);
}

bool parseUrlHostAndPort(const char* rawUrl, uint16_t defaultPort, char* outHost, size_t hostCap, uint16_t& outPort, bool& outHttps) {
    if (!rawUrl || !outHost || hostCap == 0) return false;
    while (*rawUrl == ' ') ++rawUrl;
    if (*rawUrl == '\0') return false;

    outHttps = false;
    const char* p = rawUrl;
    if (strncmp(p, "https://", 8) == 0) {
        outHttps = true;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "mqtt://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "tcp://", 6) == 0) {
        p += 6;
    }

    const char* slash = strchr(p, '/');
    size_t authLen = slash ? static_cast<size_t>(slash - p) : strlen(p);
    if (authLen == 0) return false;

    char auth[96];
    size_t copyLen = (authLen < sizeof(auth) - 1) ? authLen : (sizeof(auth) - 1);
    memcpy(auth, p, copyLen);
    auth[copyLen] = '\0';

    outPort = defaultPort;
    char* colon = strrchr(auth, ':');
    if (colon) {
        *colon = '\0';
        int parsed = atoi(colon + 1);
        if (parsed > 0 && parsed <= 65535) {
            outPort = static_cast<uint16_t>(parsed);
        }
    }
    if (auth[0] == '\0') return false;
    copySafeCStr(outHost, hostCap, auth);
    return true;
}

bool resolveBaseServerUrl(const char* rawUrl, char* outBaseUrl, size_t outCap, char* outResolvedHost, size_t hostCap) {
    uint16_t port = 8890;
    bool online = false;
    return net::ShowcaseDiscovery::resolveUrl(
        rawUrl,
        outBaseUrl,
        outCap,
        outResolvedHost,
        hostCap,
        port,
        online,
        nullptr,
        0,
        true,
        false
    );
}

bool readExactTokenAware(
    Stream& st,
    uint8_t* dst,
    size_t len,
    const std::atomic<uint32_t>& currentToken,
    uint32_t expectedToken,
    const std::atomic<bool>& stopFlag,
    uint32_t timeoutMs = 2500
) {
    size_t got = 0;
    uint32_t lastProgress = millis();
    while (got < len) {
        if (stopFlag.load(std::memory_order_acquire) ||
            currentToken.load(std::memory_order_acquire) != expectedToken) {
            return false;
        }
        int avail = st.available();
        if (avail > 0) {
            size_t toRead = len - got;
            if (static_cast<size_t>(avail) < toRead) toRead = static_cast<size_t>(avail);
            size_t r = st.readBytes(dst + got, toRead);
            if (r > 0) {
                got += r;
                lastProgress = millis();
                continue;
            }
        }
        int c = st.read();
        if (c >= 0) {
            dst[got++] = static_cast<uint8_t>(c);
            lastProgress = millis();
        } else {
            if (millis() - lastProgress > timeoutMs) return false;
#if defined(ESP32)
            vTaskDelay(pdMS_TO_TICKS(2));
#endif
        }
    }
    return true;
}

struct SharedBrokerTargetState {
    std::atomic<uint32_t> seq{0};
    char host[96] = "";
    uint16_t port = 1883;
    bool fromShowcase = false;
    bool probedOnce = false;
};

SharedBrokerTargetState s_brokerState;

void writeSharedBrokerState(bool fromShowcase, const char* host, uint16_t port, bool probedOnce) {
    s_brokerState.seq.fetch_add(1, std::memory_order_acq_rel);
    copySafeCStr(s_brokerState.host, sizeof(s_brokerState.host), host);
    s_brokerState.port = (port > 0) ? port : 1883;
    s_brokerState.fromShowcase = fromShowcase;
    s_brokerState.probedOnce = probedOnce;
    s_brokerState.seq.fetch_add(1, std::memory_order_release);
}

void readSharedBrokerState(bool& outFromShowcase, char* outHost, size_t hostCap, uint16_t& outPort, bool& outProbedOnce) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t s1 = s_brokerState.seq.load(std::memory_order_acquire);
        if ((s1 & 1U) != 0U) continue;
        bool fs = s_brokerState.fromShowcase;
        uint16_t p = s_brokerState.port;
        bool po = s_brokerState.probedOnce;
        char localH[96];
        copySafeCStr(localH, sizeof(localH), s_brokerState.host);
        uint32_t s2 = s_brokerState.seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            outFromShowcase = fs;
            copySafeCStr(outHost, hostCap, localH);
            outPort = p;
            outProbedOnce = po;
            return;
        }
    }
    outFromShowcase = false;
    if (outHost && hostCap > 0) outHost[0] = '\0';
    outPort = 1883;
    outProbedOnce = false;
}

} // namespace

// ============================================================================
// InGameShowcaseEngine (Core 1 lock-free RGB565 presenter)
// ============================================================================

EngineError InGameShowcaseEngine::initialize(EngineContext* context, const EngineConfig* cfg) {
    (void)cfg;
    if (!context || !context->getSurface()) {
        return EngineError::InitializationFailed;
    }
    m_surface = context->getSurface();
    DisplayGeometry geom = context->getGeometry();
    m_width = geom.width > 0 ? geom.width : 128;
    m_height = geom.height > 0 ? geom.height : 32;
    m_dirty = false;
    m_renderedNewFrame = false;
    return EngineError::OK;
}

void InGameShowcaseEngine::activate() {
    m_displayEngaged.store(true, std::memory_order_release);
    m_lastRenderedSeq = 0;
    m_dirty = true;
    m_renderedNewFrame = false;
}

void InGameShowcaseEngine::update(EngineContext* context) {
    (void)context;
    uint32_t seq = m_frameSeq.load(std::memory_order_acquire);
    if ((seq & 1U) == 0U && seq != m_lastRenderedSeq && m_activeFramePtr.load(std::memory_order_acquire) != nullptr) {
        m_dirty = true;
    }
}

void InGameShowcaseEngine::render(EngineContext* context) {
    if (!m_dirty) {
        m_renderedNewFrame = false;
        return;
    }
    IDrawingSurface* s = (context && context->getSurface()) ? context->getSurface() : m_surface;
    if (!s) {
        m_renderedNewFrame = false;
        return;
    }

    uint32_t seq = m_frameSeq.load(std::memory_order_acquire);
    if ((seq & 1U) != 0U) {
        m_renderedNewFrame = false;
        return;
    }
    const uint16_t* ptr = m_activeFramePtr.load(std::memory_order_acquire);
    uint16_t fw = m_frameW.load(std::memory_order_relaxed);
    uint16_t fh = m_frameH.load(std::memory_order_relaxed);

    if (s->width() != static_cast<int>(fw) || s->height() != static_cast<int>(fh)) {
        s->clear(0);
    }
    if (ptr && fw > 0 && fh > 0) {
        s->blit565(ptr, 0, 0, fw, fh);
        m_lastRenderedSeq = seq;
    }
    m_dirty = false;
    m_renderedNewFrame = true;
}

void InGameShowcaseEngine::deactivate() {
    // Invariant 15: 100% allocation-free and wait-free on Core 1
    m_displayEngaged.store(false, std::memory_order_release);
    m_dirty = false;
    m_renderedNewFrame = false;
}

void InGameShowcaseEngine::pause() {
    m_displayEngaged.store(false, std::memory_order_release);
    m_renderedNewFrame = false;
}

void InGameShowcaseEngine::resume() {
    m_displayEngaged.store(true, std::memory_order_release);
    m_lastRenderedSeq = 0;
    m_dirty = true;
    m_renderedNewFrame = false;
}

void InGameShowcaseEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_width = geometry.width > 0 ? geometry.width : 128;
    m_height = geometry.height > 0 ? geometry.height : 32;
    m_dirty = true;
}

// ============================================================================
// FrontendSyncEngine
// ============================================================================

FrontendSyncEngine* FrontendSyncEngine::instance = nullptr;

FrontendSyncEngine::FrontendSyncEngine(
    const MqttConfig& cfg,
    GifEngine* gifEngine,
    MessageEngine* messageEngine,
    InGameShowcaseEngine* inGameEngine
)
    : mqttConfig(cfg),
      gif(gifEngine),
      message(messageEngine),
      m_inGameEngine(inGameEngine),
      mqttClient(espClient) {
    instance = this;
    lastReconnectAttempt = 0;
    syncWorkerConfigSlot();
}

FrontendSyncEngine::~FrontendSyncEngine() {
    stop();
    if (instance == this) {
        instance = nullptr;
    }
}

void FrontendSyncEngine::syncWorkerConfigSlot() {
    m_cfgSeq.fetch_add(1, std::memory_order_acq_rel);
    copySafeCStr(m_workerCfg.showcaseUrl, sizeof(m_workerCfg.showcaseUrl), showcaseConfig.server_url.c_str());
    copySafeCStr(m_workerCfg.localBroker, sizeof(m_workerCfg.localBroker), mqttConfig.broker.c_str());
    copySafeCStr(m_workerCfg.lang, sizeof(m_workerCfg.lang), m_systemLang.length() > 0 ? m_systemLang.c_str() : "en");
    copySafeCStr(m_workerCfg.cycleMode, sizeof(m_workerCfg.cycleMode), mqttConfig.ingame_cycle_mode.c_str());
    copySafeCStr(m_workerCfg.cardStyleSource, sizeof(m_workerCfg.cardStyleSource), mqttConfig.card_style_source.c_str());
    copySafeCStr(m_workerCfg.cardLayout, sizeof(m_workerCfg.cardLayout), mqttConfig.card_layout.c_str());
    copySafeCStr(m_workerCfg.cardTheme, sizeof(m_workerCfg.cardTheme), mqttConfig.card_theme.c_str());
    copySafeCStr(m_workerCfg.textScrollSpeed, sizeof(m_workerCfg.textScrollSpeed), mqttConfig.text_scroll_speed.c_str());
    copySafeCStr(m_workerCfg.videoMode, sizeof(m_workerCfg.videoMode), mqttConfig.video_mode.c_str());
    copySafeCStr(m_workerCfg.colorBoost, sizeof(m_workerCfg.colorBoost), mqttConfig.color_boost.c_str());
    m_workerCfg.localPort = (mqttConfig.port > 0 && mqttConfig.port <= 65535) ? static_cast<uint16_t>(mqttConfig.port) : 1883;
    m_workerCfg.panelW = (m_panelW >= 32) ? m_panelW : 128;
    m_workerCfg.panelH = (m_panelH >= 16) ? m_panelH : 32;
    m_workerCfg.marqueeIntervalSec = static_cast<uint16_t>(constrain(mqttConfig.ingame_marquee_interval_sec, 5, 600));
    m_workerCfg.snapshotPanoramaCount = static_cast<uint8_t>(constrain(mqttConfig.snapshot_panorama_count, 0, 5));
    m_workerCfg.videoMaxDurationS = static_cast<uint8_t>(constrain(mqttConfig.video_max_duration_s, 2, 120));
    m_workerCfg.videoFps = static_cast<uint8_t>(constrain(mqttConfig.video_fps, 8, 60));
    m_workerCfg.mqttEnabled = mqttConfig.enabled;
    m_workerCfg.showcaseEnabled = showcaseConfig.enabled;
    m_workerCfg.useShowcaseBroker = mqttConfig.use_showcase_broker;
    m_workerCfg.ingameShowcaseEnabled = mqttConfig.ingame_showcase_enabled;
    m_workerCfg.includeCardIntro = mqttConfig.include_card_intro;
    m_workerCfg.showPoster = mqttConfig.show_poster;
    m_workerCfg.showSynopsis = mqttConfig.show_synopsis;
    m_workerCfg.jacketPanorama = mqttConfig.jacket_panorama;
    m_workerCfg.marqueePanorama = mqttConfig.marquee_panorama;
    m_workerCfg.showVideo = mqttConfig.show_video;
    m_cfgSeq.fetch_add(1, std::memory_order_release);
    m_cfgGen.fetch_add(1, std::memory_order_acq_rel);
}

void FrontendSyncEngine::updateRuntimeConfig(
    const MqttConfig& mqttCfg,
    const ShowcaseConfig& showcaseCfg,
    const String& systemLang,
    uint16_t panelW,
    uint16_t panelH
) {
    mqttConfig = mqttCfg;
    showcaseConfig = showcaseCfg;
    m_systemLang = systemLang.length() > 0 ? systemLang : "en";
    m_panelW = panelW > 0 ? panelW : 128;
    m_panelH = panelH > 0 ? panelH : 32;
    syncWorkerConfigSlot();

    if (!showcaseConfig.enabled || !mqttConfig.ingame_showcase_enabled) {
        cancelInGameShowcaseSession();
    }
}

void FrontendSyncEngine::getResolvedBrokerTarget(
    String& outHost,
    uint16_t& outPort,
    String& outSource,
    bool& outShowcaseActive
) {
    bool fromShowcase = false;
    char brokerHost[96] = "";
    uint16_t brokerPort = 1883;
    bool probedOnce = false;
    readSharedBrokerState(fromShowcase, brokerHost, sizeof(brokerHost), brokerPort, probedOnce);

    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const auto& snap = guard.get();

    if (fromShowcase && snap.showcase.enabled && snap.mqtt.use_showcase_broker && brokerHost[0] != '\0') {
        outHost = String(brokerHost);
        outPort = brokerPort > 0 ? brokerPort : 1883;
        outSource = "showcase";
        outShowcaseActive = true;
        return;
    }

    uint16_t cfgPort = (snap.mqtt.port > 0 && snap.mqtt.port <= 65535)
        ? static_cast<uint16_t>(snap.mqtt.port)
        : 1883;
    if (!isLoopbackOrEmptyHost(snap.mqtt.broker.c_str())) {
        outHost = snap.mqtt.broker;
        outPort = cfgPort;
        outSource = "external";
        outShowcaseActive = false;
        return;
    }

    outHost = "";
    outPort = cfgPort;
    outSource = "local";
    outShowcaseActive = false;
}

bool FrontendSyncEngine::parseShowcaseHealthBroker(
    const char* healthJson,
    const char* fallbackHost,
    String& outHost,
    uint16_t& outPort
) {
    if (!healthJson || healthJson[0] == '\0') return false;
    StaticJsonDocument<1024> filter;
    filter["status"] = true;
    filter["mqtt_mode"] = true;
    filter["mqtt_host"] = true;
    filter["mqtt_port"] = true;
    filter["mqtt"]["mode"] = true;
    filter["mqtt"]["broker_host"] = true;
    filter["mqtt"]["broker_port"] = true;

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, healthJson, DeserializationOption::Filter(filter));
    if (err) return false;

    const char* status = doc["status"] | "";
    if (strcmp(status, "ok") != 0 && !doc.containsKey("mqtt_port") && !doc.containsKey("mqtt")) {
        return false;
    }

    const char* rawHost = doc["mqtt_host"] | "";
    if (rawHost[0] == '\0' && doc["mqtt"].is<JsonObjectConst>()) {
        rawHost = doc["mqtt"]["broker_host"] | "";
    }

    int rawPort = doc["mqtt_port"] | 0;
    if (rawPort <= 0 && doc["mqtt"].is<JsonObjectConst>()) {
        rawPort = doc["mqtt"]["broker_port"] | 0;
    }
    outPort = (rawPort > 0 && rawPort <= 65535) ? static_cast<uint16_t>(rawPort) : 1883;

    char hostBuf[96] = "";
    if (!isLoopbackOrEmptyHost(rawHost)) {
        copySafeCStr(hostBuf, sizeof(hostBuf), rawHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(hostBuf, sizeof(hostBuf), 8890)) {
            return false;
        }
    } else if (fallbackHost && !isLoopbackOrEmptyHost(fallbackHost)) {
        copySafeCStr(hostBuf, sizeof(hostBuf), fallbackHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(hostBuf, sizeof(hostBuf), 8890)) {
            return false;
        }
    } else {
        return false;
    }

    outHost = String(hostBuf);
    return outHost.length() > 0;
}

String FrontendSyncEngine::buildShowcaseQueueRequestJson(
    const MqttConfig& mqttCfg,
    const String& systemId,
    const String& gameQuery,
    uint16_t width,
    uint16_t height,
    const String& lang
) {
    StaticJsonDocument<1024> doc;
    doc["domain"] = "games";
    JsonArray sysArr = doc.createNestedArray("systems");
    String cleanSys = systemId;
    cleanSys.trim();
    if (cleanSys.length() > 0) {
        sysArr.add(cleanSys);
    }
    String trimmedGame = gameQuery;
    trimmedGame.trim();
    doc["query"] = trimmedGame;
    doc["mode"] = "play_now";
    doc["mark_seen"] = false;
    doc["async_prepare"] = true;
    doc["render_card_in_stream"] = true;
    doc["width"] = (width >= 32) ? width : 128;
    doc["height"] = (height >= 16) ? height : 32;
    doc["lang"] = (lang.length() > 0) ? lang : "en";

    if (mqttCfg.card_style_source.equalsIgnoreCase("custom")) {
        doc["include_card_intro"] = mqttCfg.include_card_intro;
        doc["card_layout"] = mqttCfg.card_layout.length() > 0 ? mqttCfg.card_layout : "classic_split";
        doc["card_theme"] = mqttCfg.card_theme.length() > 0 ? mqttCfg.card_theme : "tinted";
        doc["include_poster"] = mqttCfg.show_poster;
        doc["include_synopsis"] = mqttCfg.show_synopsis;
        doc["text_scroll_speed"] = mqttCfg.text_scroll_speed.length() > 0 ? mqttCfg.text_scroll_speed : "4";
        doc["jacket_panorama"] = mqttCfg.jacket_panorama;
        doc["marquee_panorama"] = mqttCfg.marquee_panorama;
        doc["snapshot_panorama_count"] = constrain(mqttCfg.snapshot_panorama_count, 0, 5);
        doc["include_video"] = mqttCfg.show_video;
        doc["video_max_duration_s"] = constrain(mqttCfg.video_max_duration_s, 2, 120);
        doc["video_fps"] = constrain(mqttCfg.video_fps, 8, 60);
        doc["video_mode"] = mqttCfg.video_mode.length() > 0 ? mqttCfg.video_mode : "auto_action";
        doc["color_boost"] = mqttCfg.color_boost.length() > 0 ? mqttCfg.color_boost : "auto";
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void FrontendSyncEngine::stopReconnectTask() {
    stopReconnectRequested.store(true, std::memory_order_release);
    if (reconnectTaskHandle || !reconnectTaskExited.load(std::memory_order_acquire)) {
        espClient.stop();
        for (int i = 0; i < 120 && !reconnectTaskExited.load(std::memory_order_acquire); ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        reconnectTaskHandle = nullptr;
    }
    isReconnecting.store(false, std::memory_order_release);
}

void FrontendSyncEngine::applyBrokerTransport(bool fromShowcase, const char* host, uint16_t port) {
    stopReconnectTask();
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    espClient.stop();

    m_transportConfigured = true;
    m_activeFromShowcase = fromShowcase;
    copySafeCStr(m_activeBrokerHost, sizeof(m_activeBrokerHost), host);
    m_activeBrokerPort = (port > 0) ? port : 1883;
    lastReconnectAttempt = 0;

    if (fromShowcase && m_activeBrokerHost[0] != '\0') {
        if (internalBroker) {
            delete internalBroker;
            internalBroker = nullptr;
            LOGI("RetroFrontend", "Stopped embedded PicoMQTT broker to free SRAM (sharing Showcase broker %s:%u)", m_activeBrokerHost, m_activeBrokerPort);
        } else {
            LOGI("RetroFrontend", "Using Showcase MQTT broker at %s:%u", m_activeBrokerHost, m_activeBrokerPort);
        }
        espClient.setTimeout(1);
        mqttClient.setClient(espClient);
        mqttClient.setBufferSize(1024);
        mqttClient.setServer(m_activeBrokerHost, m_activeBrokerPort);
        mqttClient.setCallback(FrontendSyncEngine::callback);
        mqttClient.setSocketTimeout(1);
        return;
    }

    if (isLoopbackOrEmptyHost(mqttConfig.broker.c_str())) {
        if (!internalBroker) {
            LOGI("RetroFrontend", "Starting embedded PicoMQTT Broker on port %d...", mqttConfig.port);
            internalBroker = new PicoMQTT::Server(mqttConfig.port, 5, 2);
            internalBroker->subscribe(MQTT_TOPIC_WILDCARD, [](const char* topic, const char* payload) {
                if (instance) instance->handleMessage(String(topic), String(payload));
            });
            internalBroker->begin();
            LOGI("RetroFrontend", "Embedded MQTT Broker started successfully.");
        }
    } else {
        if (internalBroker) {
            delete internalBroker;
            internalBroker = nullptr;
        }
        LOGI("RetroFrontend", "Configuring external MQTT Client connecting to %s:%u", m_activeBrokerHost, m_activeBrokerPort);
        espClient.setTimeout(1);
        mqttClient.setClient(espClient);
        mqttClient.setBufferSize(1024);
        mqttClient.setServer(m_activeBrokerHost, m_activeBrokerPort);
        mqttClient.setCallback(FrontendSyncEngine::callback);
        mqttClient.setSocketTimeout(1);
    }
}

void FrontendSyncEngine::begin() {
    stop();
    if (!mqttConfig.enabled) return;

    hasReceivedAnyEvent = false;
    hasPendingEvent = false;
    isGamePlaying = false;
    waitingDisplayed = false;
    m_transportConfigured = false;
    m_activeFromShowcase = false;
    m_activeBrokerHost[0] = '\0';
    m_activeMarqueePath[0] = '\0';
    m_wasShowingInGame = false;
    lastReconnectAttempt = 0;
    m_beginMs = millis();

    {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const auto& snap = guard.get();
        showcaseConfig = snap.showcase;
        m_systemLang = snap.system.lang.length() > 0 ? snap.system.lang : "en";
        if (m_inGameEngine) {
            m_panelW = m_inGameEngine->surfaceWidth();
            m_panelH = m_inGameEngine->surfaceHeight();
        }
    }
    syncWorkerConfigSlot();
    writeSharedBrokerState(false, mqttConfig.broker.c_str(), static_cast<uint16_t>(mqttConfig.port), false);

#if defined(ESP32) && !defined(UNIT_TEST)
    m_workerStopRequested.store(false, std::memory_order_release);
    m_workerExited.store(false, std::memory_order_release);
    if (xTaskCreatePinnedToCore(showcaseWorkerTaskFunc, "SyncShowcaseWkr", 6144, this, 1, &m_showcaseWorkerHandle, 0) != pdPASS) {
        m_showcaseWorkerHandle = nullptr;
        m_workerExited.store(true, std::memory_order_release);
    }
#endif

    // If Showcase broker sharing is NOT enabled, start local/external broker immediately.
    // Otherwise wait briefly (< 1.2s) in loop() for Core 0's initial Showcase health probe
    // so we avoid allocating PicoMQTT::Server when Showcase is already online.
    if (!(showcaseConfig.enabled && mqttConfig.use_showcase_broker)) {
        applyBrokerTransport(false, mqttConfig.broker.c_str(), static_cast<uint16_t>(mqttConfig.port));
    }

    systemMappings = loadMappingsFromSD();
}

void FrontendSyncEngine::reconnectTaskFunc(void* param) {
    FrontendSyncEngine* self = static_cast<FrontendSyncEngine*>(param);
    if (self) {
        if (!self->stopReconnectRequested.load(std::memory_order_acquire)) {
            self->reconnect();
        }
        self->isReconnecting.store(false, std::memory_order_release);
        self->reconnectTaskHandle = nullptr;
        self->reconnectTaskExited.store(true, std::memory_order_release);
    }
    vTaskDelete(nullptr);
}

void FrontendSyncEngine::stop() {
    cancelInGameShowcaseSession();
    isGamePlaying = false;
    waitingDisplayed = false;
    hasPendingEvent = false;
    m_activeMarqueePath[0] = '\0';
    m_wasShowingInGame = false;

#if defined(ESP32) && !defined(UNIT_TEST)
    m_workerStopRequested.store(true, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_INGAME_SYNC);
    if (m_showcaseWorkerHandle || !m_workerExited.load(std::memory_order_acquire)) {
        for (int i = 0; i < 200 && !m_workerExited.load(std::memory_order_acquire); ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        m_showcaseWorkerHandle = nullptr;
    }
#endif
    freeStreamBuffersSafely();

    stopReconnectTask();

    if (internalBroker) {
        delete internalBroker;
        internalBroker = nullptr;
    }
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    m_transportConfigured = false;
    writeSharedBrokerState(false, "", 1883, false);

    if (gif) gif->stop();
    if (message) message->deactivate();
    LOGI("RetroFrontend", "MQTT Listener stopped.");
}

bool FrontendSyncEngine::loop() {
    if (!mqttConfig.enabled) {
        if (waitingDisplayed) {
            waitingDisplayed = false;
            if (message) message->deactivate();
        }
        return true;
    }

    // Check lock-free broker resolution state from Core 0 worker
    bool wantShowcaseBroker = (showcaseConfig.enabled && mqttConfig.use_showcase_broker);
    bool resolvedFromShowcase = false;
    char resolvedHost[96] = "";
    uint16_t resolvedPort = 1883;
    bool probedOnce = false;
    readSharedBrokerState(resolvedFromShowcase, resolvedHost, sizeof(resolvedHost), resolvedPort, probedOnce);

    if (!wantShowcaseBroker) {
        resolvedFromShowcase = false;
        copySafeCStr(resolvedHost, sizeof(resolvedHost), mqttConfig.broker.c_str());
        resolvedPort = static_cast<uint16_t>(mqttConfig.port > 0 ? mqttConfig.port : 1883);
        probedOnce = true;
    }

    if (probedOnce || (millis() - m_beginMs > 1500)) {
        const char* desiredHost = resolvedFromShowcase ? resolvedHost : mqttConfig.broker.c_str();
        uint16_t desiredPort = resolvedFromShowcase
            ? resolvedPort
            : static_cast<uint16_t>(mqttConfig.port > 0 ? mqttConfig.port : 1883);
        if (!m_transportConfigured ||
            m_activeFromShowcase != resolvedFromShowcase ||
            m_activeBrokerPort != desiredPort ||
            strcmp(m_activeBrokerHost, desiredHost) != 0) {
            applyBrokerTransport(resolvedFromShowcase, desiredHost, desiredPort);
        }
    }

    // Restore static PNG marquee cleanly onto shared surface when InGameShowcase yields back (`once` / `alternating`)
    bool showingInGame = m_wantsInGameDisplay.load(std::memory_order_acquire);
    if (m_wasShowingInGame && !showingInGame && isGamePlaying && gif && m_activeMarqueePath[0] != '\0') {
        size_t pLen = strlen(m_activeMarqueePath);
        if (pLen > 4 && strcasecmp(m_activeMarqueePath + pLen - 4, ".png") == 0) {
            gif->playGif(m_activeMarqueePath);
        }
    }
    m_wasShowingInGame = showingInGame;

    if (m_transportConfigured) {
        if (internalBroker) {
            internalBroker->loop();
        } else {
#if defined(ESP32)
            // Single-activity discipline on non-PSRAM `esp32dev`: never hold a background FrontendSync
            // MQTT client socket open simultaneously with `WebUiSync` or an active `MediaShowcaseBase`
            // MQTT/Stream turn while idle (no game playing).
            if (!hardwareHAL.capabilities().hasPsram && !isGamePlaying) {
                auto act = NetworkBudget::getDmdNetActivity();
                if (WebUiSyncEngine::isRequested() ||
                    WebUiSyncEngine::isReadyForServing() ||
                    act != NetworkBudget::DmdNetActivity::Idle) {
                    if (mqttClient.connected()) {
                        mqttClient.disconnect();
                        espClient.stop();
                        lastReconnectAttempt = millis() == 0 ? 1UL : millis();
                    }
                    return true;
                }
            }
#endif
            if (!mqttClient.connected()) {
                unsigned long now = millis();
                unsigned long retryInterval = (lastReconnectAttempt == 0) ? 0UL : 10000UL;
                if ((lastReconnectAttempt == 0 || (now - lastReconnectAttempt > retryInterval)) &&
                    !isReconnecting.load(std::memory_order_acquire) &&
                    reconnectTaskExited.load(std::memory_order_acquire)) {
                    lastReconnectAttempt = now == 0 ? 1 : now;
                    stopReconnectRequested.store(false, std::memory_order_release);
                    isReconnecting.store(true, std::memory_order_release);
                    reconnectTaskExited.store(false, std::memory_order_release);
                    if (xTaskCreatePinnedToCore(reconnectTaskFunc, "MqttReconnect", 4096, this, 1, &reconnectTaskHandle, 0) != pdPASS) {
                        reconnectTaskHandle = nullptr;
                        isReconnecting.store(false, std::memory_order_release);
                        reconnectTaskExited.store(true, std::memory_order_release);
                    }
                }
            } else {
                mqttClient.loop();
            }
        }
    }

    if (hasPendingEvent) {
        // Debounce browsing events by 150ms of quiet time so rapid navigation doesn't hammer SD card or broker.
        // Non-browsing events (gameStart, gameStop, playing, stopped) are processed immediately.
        bool isBrowsing = (pendingPayload.indexOf("\"status\":\"browsing\"") != -1 ||
                           pendingPayload.indexOf("\"status\": \"browsing\"") != -1);
        if (!isBrowsing || (millis() - pendingEventTime >= 150)) {
            hasPendingEvent = false;
            uint32_t reqId = currentRequestId;
            handleGameEvent(pendingPayload, reqId);
        }
    } else if (!hasReceivedAnyEvent && (!waitingDisplayed || (message && !message->isActive())) && message) {
        waitingDisplayed = true;
        MessageConfig cfg = { "WAITING FOR MARQUEE", 0xFFFF, 1, "none", 40, 0 };
        message->displayMessage(cfg);
        LOGI("RetroFrontend", "MQTT Enabled: Displaying WAITING FOR MARQUEE on DMD.");
    }

    return true;
}

void FrontendSyncEngine::reconnect() {
    if (stopReconnectRequested.load(std::memory_order_acquire)) return;
    if (WiFi.status() == WL_CONNECTED) {
        String cid = mqttConfig.deviceName;
        char hwId[8] = "";
        net::ShowcaseDiscovery::hardwareShortId(hwId, sizeof(hwId));
        cid += "_";
        cid += hwId;
        bool connected = false;
        if (!m_activeFromShowcase && mqttConfig.user.length() > 0) {
            connected = mqttClient.connect(cid.c_str(), mqttConfig.user.c_str(), mqttConfig.pass.c_str());
        } else {
            connected = mqttClient.connect(cid.c_str());
        }
        if (connected) {
            if (!stopReconnectRequested.load(std::memory_order_acquire)) {
                mqttClient.subscribe(MQTT_TOPIC_WILDCARD);
                if (m_activeFromShowcase) {
                    mqttClient.subscribe(MQTT_TOPIC_SHOWCASE_CMD);
                }
            }
        }
    }
}

void FrontendSyncEngine::callback(char* topic, byte* payload, unsigned int length) {
    if (!instance) return;

    String payloadStr;
    payloadStr.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }

    instance->handleMessage(String(topic), payloadStr);
}

void FrontendSyncEngine::handleMessage(String topic, String msg) {
    if (topic.startsWith("system/playing/")) {
        LOGI("RetroFrontend", "MQTT Event Received: Topic = '%s', Payload = '%s'", topic.c_str(), msg.c_str());
        pendingPayload = msg;
        hasPendingEvent = true;
        pendingEventTime = millis();
        currentRequestId++;
        return;
    }

    if (topic.startsWith("dmd_showcase/clients/") && topic.endsWith("/command")) {
        char hwPrefix[20] = "";
        char hwId[8] = "";
        net::ShowcaseDiscovery::hardwareShortId(hwId, sizeof(hwId));
        snprintf(hwPrefix, sizeof(hwPrefix), "esp32_%s_", hwId);
        // Only wake up for commands targeting this board's hardware ID (or legacy broadcast slot)
        if ((topic.indexOf(hwPrefix) != -1 || topic.indexOf("/esp32_games_") != -1) &&
            msg.indexOf("\"play_now\"") != -1) {
            m_readyWakeupHint.store(true, std::memory_order_release);
        }
        return;
    }
}

void FrontendSyncEngine::startInGameShowcaseSession(const String& cleanSystem, const String& rawGame) {
    uint32_t tok = m_playSessionToken.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_wantsInGameDisplay.store(false, std::memory_order_release);
    m_readyWakeupHint.store(false, std::memory_order_release);
    if (m_inGameEngine) {
        m_inGameEngine->clearFrame();
    }

    if (!showcaseConfig.enabled || !mqttConfig.ingame_showcase_enabled || rawGame.length() == 0) {
        m_jobSeq.fetch_add(1, std::memory_order_acq_rel);
        m_jobSlot.token = tok;
        m_jobSlot.active = false;
        m_jobSlot.system[0] = '\0';
        m_jobSlot.game[0] = '\0';
        m_jobSeq.fetch_add(1, std::memory_order_release);
        return;
    }

    m_jobSeq.fetch_add(1, std::memory_order_acq_rel);
    copySafeCStr(m_jobSlot.system, sizeof(m_jobSlot.system), cleanSystem.c_str());
    copySafeCStr(m_jobSlot.game, sizeof(m_jobSlot.game), rawGame.c_str());
    m_jobSlot.token = tok;
    m_jobSlot.active = true;
    m_jobSeq.fetch_add(1, std::memory_order_release);
}

void FrontendSyncEngine::cancelInGameShowcaseSession() {
    uint32_t tok = m_playSessionToken.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_wantsInGameDisplay.store(false, std::memory_order_release);
    m_readyWakeupHint.store(false, std::memory_order_release);
    if (m_inGameEngine) {
        m_inGameEngine->clearFrame();
    }
    m_jobSeq.fetch_add(1, std::memory_order_acq_rel);
    m_jobSlot.token = tok;
    m_jobSlot.active = false;
    m_jobSlot.system[0] = '\0';
    m_jobSlot.game[0] = '\0';
    m_jobSeq.fetch_add(1, std::memory_order_release);
}

bool FrontendSyncEngine::allocateStreamBuffers(uint16_t w, uint16_t h) {
    size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (pixels == 0) return false;
    if (m_streamFrames[0] != nullptr && m_streamFramePixels == pixels) {
        return true;
    }
    freeStreamBuffersSafely();

    size_t frameBytes = pixels * sizeof(uint16_t);
#if defined(ESP32)
    bool hasPsram = hardwareHAL.capabilities().hasPsram;
    if (hasPsram) {
        m_streamFrames[0] = static_cast<uint16_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_streamFrames[1] = static_cast<uint16_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    } else {
        // Classic ESP32 (`esp32dev`): allocate a single 8 KB frame buffer (`m_streamFrames[0]`) for in-place
        // `DMDZ` / `DMDS` decoding so 8-bit HUB75 DMA stays pinned with plenty of internal 8-bit DRAM headroom.
        size_t free8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        size_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (frameBytes <= 16384U && free8 >= frameBytes + 6144U && largest8 >= frameBytes) {
            m_streamFrames[0] = static_cast<uint16_t*>(malloc(frameBytes));
        }
    }
#else
    m_streamFrames[0] = static_cast<uint16_t*>(malloc(frameBytes));
    m_streamFrames[1] = static_cast<uint16_t*>(malloc(frameBytes));
#endif

    if (!m_streamFrames[0]) {
        if (m_streamFrames[1]) {
            free(m_streamFrames[1]);
            m_streamFrames[1] = nullptr;
        }
        m_streamFramePixels = 0;
        return false;
    }
    memset(m_streamFrames[0], 0, frameBytes);
    if (m_streamFrames[1]) {
        memset(m_streamFrames[1], 0, frameBytes);
    }
    m_streamFramePixels = pixels;
    return true;
}

void FrontendSyncEngine::freeStreamBuffersSafely() {
    m_wantsInGameDisplay.store(false, std::memory_order_release);
    if (m_inGameEngine) {
        m_inGameEngine->clearFrame();
#if defined(ESP32) && !defined(UNIT_TEST)
        for (int i = 0; i < 50 && m_inGameEngine->isDisplayEngaged(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        vTaskDelay(pdMS_TO_TICKS(18));
#endif
    }
    for (int i = 0; i < 2; ++i) {
        if (m_streamFrames[i]) {
            free(m_streamFrames[i]);
            m_streamFrames[i] = nullptr;
        }
    }
    m_streamFramePixels = 0;
}

void FrontendSyncEngine::deleteShowcaseQueueItemHttp(
    const char* resolvedServerUrl,
    const char* syncClientId,
    const char* queueId
) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!resolvedServerUrl || !syncClientId || !queueId || queueId[0] == '\0') return;
    char host[96] = "";
    uint16_t port = 8890;
    bool isHttps = false;
    if (!parseUrlHostAndPort(resolvedServerUrl, 8890, host, sizeof(host), port, isHttps)) return;

    WiFiClient cli;
    if (!cli.connect(host, port, 800)) return;
    cli.printf(
        "DELETE /api/v1/clients/%s/queue/%s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
        syncClientId,
        queueId,
        host,
        port
    );
    uint32_t t0 = millis();
    while (cli.connected() && !cli.available() && (millis() - t0) < 300U) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    cli.stop();
#else
    (void)resolvedServerUrl;
    (void)syncClientId;
    (void)queueId;
#endif
}

bool FrontendSyncEngine::streamReadyItemOnce(
    const char* resolvedServerUrl,
    const char* itemId,
    uint32_t jobToken,
    uint16_t expectedW,
    uint16_t expectedH
) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!resolvedServerUrl || !itemId || itemId[0] == '\0') return false;
    NetworkBudget::ScopedDmdNetActivity streamAct(NetworkBudget::DmdNetActivity::Stream);
    if (!allocateStreamBuffers(expectedW, expectedH)) {
        LOGW("RetroFrontend", "Insufficient memory for %ux%u In-Game Showcase stream; keeping marquee.", expectedW, expectedH);
        return false;
    }

    net::SecureHttpOptions httpOpts;
    httpOpts.ownerId = net::OWNER_INGAME_SYNC;
    httpOpts.requestTimeoutMs = 3500;
    httpOpts.keepAlive = false;

    String streamUrl = String(resolvedServerUrl) + "/api/v1/games/stream/" + itemId + ".rgb565.stream?purge=false&paced=1&codec=dmdz1";
    auto resp = net::SecureHttpClient::get(streamUrl, httpOpts);
    if (!resp.ok()) {
        return false;
    }

    Stream& st = resp.stream();
    uint8_t hdr[16];
    core::drawing::DmdzStreamHeader parsedHdr;
    if (!readExactTokenAware(st, hdr, sizeof(hdr), m_playSessionToken, jobToken, m_workerStopRequested, 2500) ||
        !core::drawing::DmdzStreamDecoder::parseHeader(hdr, parsedHdr)) {
        return false;
    }

    uint8_t fps = parsedHdr.fps;
    uint16_t w = parsedHdr.width;
    uint16_t h = parsedHdr.height;
    uint16_t totalFrames = parsedHdr.totalFrames;
    size_t framePixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    size_t frameBytes = framePixels * sizeof(uint16_t);
    if (framePixels == 0 || framePixels > m_streamFramePixels || totalFrames == 0) {
        return false;
    }

    core::drawing::DmdzStreamDecoder dmdzDec;
    if (parsedHdr.isDmdz) {
        dmdzDec.init(w, h);
    }

    const uint32_t safeFps = std::max<uint32_t>(1U, static_cast<uint32_t>(fps));
    uint32_t scheduleT0 = 0U;
    uint32_t publishedOnClock = 0U;
    const bool singleBuffer = (m_streamFrames[1] == nullptr) || parsedHdr.isDmdz;

    for (uint16_t f = 0; f < totalFrames; ++f) {
        if (m_workerStopRequested.load(std::memory_order_acquire) ||
            m_playSessionToken.load(std::memory_order_acquire) != jobToken) {
            return false;
        }

        uint16_t* dst = singleBuffer ? m_streamFrames[0] : m_streamFrames[f & 1U];
        if (singleBuffer && m_inGameEngine && f > 0U) {
            m_inGameEngine->beginFrameUpdate();
        }

        bool okFrame = false;
        if (parsedHdr.isDmdz) {
            uint8_t lenBytes[2] = {0, 0};
            if (readExactTokenAware(st, lenBytes, 2U, m_playSessionToken, jobToken, m_workerStopRequested, 2500)) {
                const uint16_t payloadLen = static_cast<uint16_t>(lenBytes[0]) | (static_cast<uint16_t>(lenBytes[1]) << 8);
                dmdzDec.beginFrame(payloadLen);
                uint8_t decChunk[256];
                size_t rem = payloadLen;
                okFrame = true;
                while (rem > 0U) {
                    const size_t take = (rem < sizeof(decChunk)) ? rem : sizeof(decChunk);
                    if (!readExactTokenAware(st, decChunk, take, m_playSessionToken, jobToken, m_workerStopRequested, 2500) ||
                        !dmdzDec.feed(decChunk, take, dst)) {
                        okFrame = false;
                        break;
                    }
                    rem -= take;
                }
                if (okFrame && !dmdzDec.finishFrame()) {
                    okFrame = false;
                }
            }
        } else {
            okFrame = readExactTokenAware(st, reinterpret_cast<uint8_t*>(dst), frameBytes, m_playSessionToken, jobToken, m_workerStopRequested, 2500);
        }

        if (!okFrame ||
            m_workerStopRequested.load(std::memory_order_acquire) ||
            m_playSessionToken.load(std::memory_order_acquire) != jobToken) {
            if (singleBuffer && m_inGameEngine) {
                m_inGameEngine->endFrameUpdateWithoutPublish();
            }
            return false;
        }

        const uint32_t now = millis();
        if (publishedOnClock == 0U) {
            scheduleT0 = now;
        } else {
            const uint32_t due = scheduleT0 + static_cast<uint32_t>((static_cast<uint64_t>(publishedOnClock) * 1000ULL) / safeFps);
            const int32_t lagMs = static_cast<int32_t>(now - due);
            if (lagMs > static_cast<int32_t>(1000U / safeFps)) {
                scheduleT0 = now - static_cast<uint32_t>((static_cast<uint64_t>(publishedOnClock) * 1000ULL) / safeFps);
            } else {
                while (!m_workerStopRequested.load(std::memory_order_acquire) &&
                       m_playSessionToken.load(std::memory_order_acquire) == jobToken &&
                       static_cast<int32_t>(due - millis()) > 0) {
                    const uint32_t rem = due - millis();
                    vTaskDelay(pdMS_TO_TICKS(rem > 2U ? (rem - 1U) : 1U));
                }
            }
        }

        if (m_inGameEngine) {
            m_inGameEngine->publishFrame(dst, w, h);
        }
        ++publishedOnClock;
        if (f == 0) {
            // Late Preemption: Frame 0 is now buffered and published -> preempt marquee cleanly!
            m_wantsInGameDisplay.store(true, std::memory_order_release);
        }
    }
    return true;
#else
    (void)resolvedServerUrl;
    (void)itemId;
    (void)jobToken;
    (void)expectedW;
    (void)expectedH;
    return false;
#endif
}

void FrontendSyncEngine::showcaseWorkerTaskFunc(void* param) {
    FrontendSyncEngine* self = static_cast<FrontendSyncEngine*>(param);
    if (self) {
        self->showcaseWorkerLoop();
        self->m_showcaseWorkerHandle = nullptr;
        self->m_workerExited.store(true, std::memory_order_release);
    }
    vTaskDelete(nullptr);
}

void FrontendSyncEngine::showcaseWorkerLoop() {
#if defined(ESP32) && !defined(UNIT_TEST)
    uint32_t lastHealthProbeMs = 0;
    uint32_t lastCheckedCfgGen = 0;
    uint8_t consecutiveHealthFailures = 0;
    bool currentShowcaseOnline = false;
    uint32_t lastProcessedJobToken = 0;

    char activeQueueServerUrl[112] = "";
    char activeQueueClientId[48] = "";
    char activeQueueId[48] = "";

    auto cleanupActiveQueue = [&]() {
        if (activeQueueId[0] != '\0') {
            deleteShowcaseQueueItemHttp(activeQueueServerUrl, activeQueueClientId, activeQueueId);
            activeQueueId[0] = '\0';
        }
    };

    auto readWorkerCfg = [&](WorkerConfigSlot& out) {
        for (int i = 0; i < 4; ++i) {
            uint32_t s1 = m_cfgSeq.load(std::memory_order_acquire);
            if ((s1 & 1U) != 0U) continue;
            WorkerConfigSlot tmp = m_workerCfg;
            uint32_t s2 = m_cfgSeq.load(std::memory_order_acquire);
            if (s1 == s2) {
                out = tmp;
                return;
            }
        }
        out = m_workerCfg;
    };

    auto readJobSlot = [&](InGameJobSlot& out) {
        for (int i = 0; i < 4; ++i) {
            uint32_t s1 = m_jobSeq.load(std::memory_order_acquire);
            if ((s1 & 1U) != 0U) continue;
            InGameJobSlot tmp = m_jobSlot;
            uint32_t s2 = m_jobSeq.load(std::memory_order_acquire);
            if (s1 == s2) {
                out = tmp;
                return;
            }
        }
        out = m_jobSlot;
    };

    while (!m_workerStopRequested.load(std::memory_order_acquire)) {
        WorkerConfigSlot wcfg;
        readWorkerCfg(wcfg);
        uint32_t curCfgGen = m_cfgGen.load(std::memory_order_acquire);

        // 1. Periodic / config-triggered Showcase MQTT broker discovery
        uint32_t nowMs = millis();
        bool hasPsram = hardwareHAL.capabilities().hasPsram;
        bool needProbe = (curCfgGen != lastCheckedCfgGen) ||
                         (lastHealthProbeMs == 0) ||
                         ((nowMs - lastHealthProbeMs) >= (currentShowcaseOnline ? (hasPsram ? 12000U : 30000U) : 5000U));
        if (needProbe) {
            if (!hasPsram &&
                (WebUiSyncEngine::isRequested() ||
                 WebUiSyncEngine::isReadyForServing() ||
                 NetworkBudget::isBulkTransferActive() ||
                 NetworkBudget::getDmdNetActivity() != NetworkBudget::DmdNetActivity::Idle)) {
                // Fast zero-socket cache reuse when `MediaShowcaseBase` already verified the Showcase host
                if (wcfg.mqttEnabled && wcfg.showcaseEnabled && wcfg.useShowcaseBroker && strlen(wcfg.showcaseUrl) >= 8) {
                    char cachedUrl[112] = "";
                    char cachedHost[64] = "";
                    uint16_t cachedPort = 8890;
                    bool cachedOnline = false;
                    if (net::ShowcaseDiscovery::peekCachedUrlStatus(
                            wcfg.showcaseUrl,
                            cachedUrl,
                            sizeof(cachedUrl),
                            cachedHost,
                            sizeof(cachedHost),
                            cachedPort,
                            cachedOnline,
                            nullptr,
                            0) &&
                        cachedOnline && !net::ShowcaseDiscovery::isAutoDiscoverableHost(cachedHost)) {
                        consecutiveHealthFailures = 0;
                        currentShowcaseOnline = true;
                        lastCheckedCfgGen = curCfgGen;
                        lastHealthProbeMs = nowMs == 0 ? 1 : nowMs;
                        writeSharedBrokerState(true, cachedHost, 1883, true);
                    }
                }
            } else {
                lastCheckedCfgGen = curCfgGen;
                lastHealthProbeMs = nowMs == 0 ? 1 : nowMs;

                if (wcfg.mqttEnabled && wcfg.showcaseEnabled && wcfg.useShowcaseBroker &&
                    WiFi.status() == WL_CONNECTED && strlen(wcfg.showcaseUrl) >= 8) {
                    char baseServerUrl[112] = "";
                    char resolvedShowcaseHost[96] = "";
                    uint16_t cachedPort = 8890;
                    bool cachedOnline = false;
                    bool healthOk = false;
                    String brokerHost;
                    uint16_t brokerPort = 1883;

                    // Zero-socket fast path when `MediaShowcaseBase` already holds a fresh verified lease
                    if (!hasPsram &&
                        net::ShowcaseDiscovery::peekCachedUrlStatus(
                            wcfg.showcaseUrl,
                            baseServerUrl,
                            sizeof(baseServerUrl),
                            resolvedShowcaseHost,
                            sizeof(resolvedShowcaseHost),
                            cachedPort,
                            cachedOnline,
                            nullptr,
                            0,
                            60000U) &&
                        cachedOnline && !net::ShowcaseDiscovery::isAutoDiscoverableHost(resolvedShowcaseHost)) {
                        brokerHost = String(resolvedShowcaseHost);
                        brokerPort = 1883;
                        healthOk = true;
                    } else if (resolveBaseServerUrl(wcfg.showcaseUrl, baseServerUrl, sizeof(baseServerUrl), resolvedShowcaseHost, sizeof(resolvedShowcaseHost))) {
                        net::SecureHttpOptions hOpts;
                        hOpts.ownerId = net::OWNER_INGAME_SYNC;
                        hOpts.requestTimeoutMs = 1200;
                        hOpts.keepAlive = false;
                        auto hResp = net::SecureHttpClient::get(String(baseServerUrl) + "/api/v1/health", hOpts);
                        if (hResp.ok()) {
                            String body = hResp.body();
                            healthOk = parseShowcaseHealthBroker(body.c_str(), resolvedShowcaseHost, brokerHost, brokerPort);
                        }
                    }

                    if (healthOk) {
                        consecutiveHealthFailures = 0;
                        currentShowcaseOnline = true;
                        writeSharedBrokerState(true, brokerHost.c_str(), brokerPort, true);
                    } else {
                        if (consecutiveHealthFailures < 10) consecutiveHealthFailures++;
                        if (!currentShowcaseOnline || consecutiveHealthFailures >= 2) {
                            currentShowcaseOnline = false;
                            writeSharedBrokerState(false, wcfg.localBroker, wcfg.localPort, true);
                        }
                    }
                } else {
                    consecutiveHealthFailures = 0;
                    currentShowcaseOnline = false;
                    writeSharedBrokerState(false, wcfg.localBroker, wcfg.localPort, true);
                }
            }
        }

        // 2. Check for pending In-Game Showcase launch or cancellation
        InGameJobSlot job;
        readJobSlot(job);
        if (job.token != lastProcessedJobToken) {
            cleanupActiveQueue();
            freeStreamBuffersSafely();
            lastProcessedJobToken = job.token;

            if (job.active && wcfg.mqttEnabled && wcfg.showcaseEnabled && wcfg.ingameShowcaseEnabled &&
                WiFi.status() == WL_CONNECTED && job.game[0] != '\0') {
                uint16_t targetW = m_inGameEngine ? m_inGameEngine->surfaceWidth() : wcfg.panelW;
                uint16_t targetH = m_inGameEngine ? m_inGameEngine->surfaceHeight() : wcfg.panelH;
                if (targetW < 32) targetW = 128;
                if (targetH < 16) targetH = 32;

                char baseServerUrl[112] = "";
                char resolvedShowcaseHost[96] = "";
                if (resolveBaseServerUrl(wcfg.showcaseUrl, baseServerUrl, sizeof(baseServerUrl), resolvedShowcaseHost, sizeof(resolvedShowcaseHost))) {
                    char syncClientId[48];
                    net::ShowcaseDiscovery::formatClientId("games", targetW, targetH, true, syncClientId, sizeof(syncClientId));

                    MqttConfig reqMqttCfg;
                    reqMqttCfg.card_style_source = wcfg.cardStyleSource;
                    reqMqttCfg.include_card_intro = wcfg.includeCardIntro;
                    reqMqttCfg.card_layout = wcfg.cardLayout;
                    reqMqttCfg.card_theme = wcfg.cardTheme;
                    reqMqttCfg.show_poster = wcfg.showPoster;
                    reqMqttCfg.show_synopsis = wcfg.showSynopsis;
                    reqMqttCfg.text_scroll_speed = wcfg.textScrollSpeed;
                    reqMqttCfg.jacket_panorama = wcfg.jacketPanorama;
                    reqMqttCfg.marquee_panorama = wcfg.marqueePanorama;
                    reqMqttCfg.snapshot_panorama_count = wcfg.snapshotPanoramaCount;
                    reqMqttCfg.show_video = wcfg.showVideo;
                    reqMqttCfg.video_max_duration_s = wcfg.videoMaxDurationS;
                    reqMqttCfg.video_fps = wcfg.videoFps;
                    reqMqttCfg.video_mode = wcfg.videoMode;
                    reqMqttCfg.color_boost = wcfg.colorBoost;

                    String postBody = buildShowcaseQueueRequestJson(
                        reqMqttCfg,
                        String(job.system),
                        String(job.game),
                        targetW,
                        targetH,
                        String(wcfg.lang)
                    );

                    net::SecureHttpOptions qOpts;
                    qOpts.ownerId = net::OWNER_INGAME_SYNC;
                    qOpts.requestTimeoutMs = 3500;
                    qOpts.keepAlive = false;

                    String enqueueUrl = String(baseServerUrl) + "/api/v1/clients/" + syncClientId + "/queue/async";
                    auto qResp = net::SecureHttpClient::post(enqueueUrl, "application/json", postBody, qOpts);
                    if (qResp.ok()) {
                        DynamicJsonDocument qDoc(2048);
                        if (deserializeJson(qDoc, qResp.stream()) == DeserializationError::Ok) {
                            JsonVariantConst itemVar = qDoc.containsKey("item")
                                ? qDoc["item"].as<JsonVariantConst>()
                                : qDoc.as<JsonVariantConst>();
                            const char* qId = itemVar["queue_id"] | "";
                            const char* qStatus = itemVar["status"] | "";
                            const char* qItemId = itemVar["item_id"] | "";

                            if (qId[0] != '\0') {
                                copySafeCStr(activeQueueServerUrl, sizeof(activeQueueServerUrl), baseServerUrl);
                                copySafeCStr(activeQueueClientId, sizeof(activeQueueClientId), syncClientId);
                                copySafeCStr(activeQueueId, sizeof(activeQueueId), qId);

                                if (m_playSessionToken.load(std::memory_order_acquire) != job.token ||
                                    m_workerStopRequested.load(std::memory_order_acquire)) {
                                    cleanupActiveQueue();
                                } else {
                                    String readyItemId = (strcmp(qStatus, "ready") == 0 || strcmp(qStatus, "playing") == 0)
                                        ? String(qItemId)
                                        : String("");

                                    // Wait for async preparation while marquee stays undisturbed on the DMD
                                    uint32_t waitStartMs = millis();
                                    String pollUrl = String(baseServerUrl) + "/api/v1/clients/" + syncClientId + "/queue";
                                    while (readyItemId.length() == 0 &&
                                           !m_workerStopRequested.load(std::memory_order_acquire) &&
                                           m_playSessionToken.load(std::memory_order_acquire) == job.token &&
                                           (millis() - waitStartMs) < 60000U) {
                                        for (int w = 0; w < 15; ++w) {
                                            if (m_workerStopRequested.load(std::memory_order_acquire) ||
                                                m_playSessionToken.load(std::memory_order_acquire) != job.token ||
                                                m_readyWakeupHint.exchange(false, std::memory_order_acq_rel)) {
                                                break;
                                            }
                                            vTaskDelay(pdMS_TO_TICKS(50));
                                        }
                                        if (m_workerStopRequested.load(std::memory_order_acquire) ||
                                            m_playSessionToken.load(std::memory_order_acquire) != job.token) {
                                            break;
                                        }

                                        auto pResp = net::SecureHttpClient::get(pollUrl, qOpts);
                                        if (pResp.ok()) {
                                            DynamicJsonDocument pDoc(3072);
                                            if (deserializeJson(pDoc, pResp.stream()) == DeserializationError::Ok) {
                                                JsonArrayConst itemsArr = pDoc.containsKey("items")
                                                    ? pDoc["items"].as<JsonArrayConst>()
                                                    : pDoc.as<JsonArrayConst>();
                                                bool foundInQueue = false;
                                                for (JsonObjectConst it : itemsArr) {
                                                    const char* itQid = it["queue_id"] | "";
                                                    if (strcmp(itQid, activeQueueId) == 0) {
                                                        foundInQueue = true;
                                                        const char* st = it["status"] | "";
                                                        const char* iid = it["item_id"] | "";
                                                        if ((strcmp(st, "ready") == 0 || strcmp(st, "playing") == 0) && iid[0] != '\0') {
                                                            readyItemId = String(iid);
                                                        } else if (strcmp(st, "error") == 0) {
                                                            foundInQueue = false;
                                                        }
                                                        break;
                                                    }
                                                }
                                                if (!foundInQueue) break;
                                            }
                                        }
                                    }

                                    // Stream ready item according to configured cycle mode (`once`, `loop`, `alternating`)
                                    while (readyItemId.length() > 0 &&
                                           !m_workerStopRequested.load(std::memory_order_acquire) &&
                                           m_playSessionToken.load(std::memory_order_acquire) == job.token) {
                                        bool playedOk = streamReadyItemOnce(
                                            baseServerUrl,
                                            readyItemId.c_str(),
                                            job.token,
                                            targetW,
                                            targetH
                                        );
                                        if (!playedOk ||
                                            m_workerStopRequested.load(std::memory_order_acquire) ||
                                            m_playSessionToken.load(std::memory_order_acquire) != job.token) {
                                            break;
                                        }

                                        readWorkerCfg(wcfg);
                                        if (strcasecmp(wcfg.cycleMode, "loop") == 0) {
                                            continue;
                                        } else if (strcasecmp(wcfg.cycleMode, "alternating") == 0) {
                                            // Yield display back to Pixelcade marquee (TransitionMode::RESUME) for marqueeIntervalSec
                                            m_wantsInGameDisplay.store(false, std::memory_order_release);
                                            uint32_t pauseMs = static_cast<uint32_t>(wcfg.marqueeIntervalSec) * 1000U;
                                            uint32_t pauseT0 = millis();
                                            while (!m_workerStopRequested.load(std::memory_order_acquire) &&
                                                   m_playSessionToken.load(std::memory_order_acquire) == job.token &&
                                                   (millis() - pauseT0) < pauseMs) {
                                                vTaskDelay(pdMS_TO_TICKS(50));
                                            }
                                            continue;
                                        } else {
                                            // `once`: release display back to marquee and clean up queue item
                                            break;
                                        }
                                    }
                                    cleanupActiveQueue();
                                    freeStreamBuffersSafely();
                                }
                            }
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(40));
    }

    cleanupActiveQueue();
    freeStreamBuffersSafely();
#endif
}

void FrontendSyncEngine::handleGameEvent(const String& jsonPayload, uint32_t reqId) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, jsonPayload);
    if (err) {
        LOGE("RetroFrontend", "Failed to parse MQTT JSON payload: %s", err.c_str());
        return;
    }

    const char* status = doc["status"] | "";
    const char* gameRaw = doc["game"] | "";
    const char* systemRaw = doc["system"] | "";
    const char* typeRaw = doc["type"] | "";

    hasReceivedAnyEvent = true;

    if (strcmp(status, "stopped") == 0) {
        LOGI("RetroFrontend", "Received stopped event, releasing any In-Game Showcase and keeping last marquee.");
        cancelInGameShowcaseSession();
        hasPendingEvent = false;
        return;
    }

    String cleanSystem = cleanSystemName(String(systemRaw));
    String cleanGame = cleanSystemName(String(gameRaw));

    bool isPlaying = (strcmp(status, "playing") == 0);
    if (!isPlaying) {
        // Menu navigation ("browsing" or system selection) -> 100% local marquee, cancel any previous in-game Showcase
        cancelInGameShowcaseSession();
    }

    if (!isPlaying && (strcmp(typeRaw, "system") == 0 || cleanGame.length() == 0 || cleanGame.equalsIgnoreCase(cleanSystem))) {
        handleSystemEvent(cleanSystem.length() > 0 ? cleanSystem : String(systemRaw), reqId);
        return;
    }

    isGamePlaying = true;
    waitingDisplayed = false;
    m_activeMarqueePath[0] = '\0';
    if (message) message->deactivate();

    if (isPlaying && gameRaw[0] != '\0') {
        startInGameShowcaseSession(cleanSystem, String(gameRaw));
    }

    String game = String(gameRaw);
    String folder = mapSystemToPixelcadeFolder(cleanSystem);

    String cleanGameNoTags = game;
    int idxParen = cleanGameNoTags.indexOf(" (");
    if (idxParen != -1) cleanGameNoTags = cleanGameNoTags.substring(0, idxParen);
    int idxBrack = cleanGameNoTags.indexOf(" [");
    if (idxBrack != -1) cleanGameNoTags = cleanGameNoTags.substring(0, idxBrack);
    cleanGameNoTags.trim();

    std::vector<String> nameVariants;
    nameVariants.push_back(game);
    if (cleanGameNoTags.length() > 0 && cleanGameNoTags != game) {
        nameVariants.push_back(cleanGameNoTags);
    }
    if (cleanGame.length() > 0 && cleanGame != game && cleanGame != cleanGameNoTags) {
        nameVariants.push_back(cleanGame);
    }

    String foundArtPath = "";
    bool exists = false;

    {
        SdLockGuard guard(pdMS_TO_TICKS(500));
        if (guard) {
            for (const String& nameVar : nameVariants) {
                String testPaths[] = {
                    "/pixelcade/" + folder + "/" + nameVar,
                    "/" + folder + "/" + nameVar,
                    "/pixelcade/console/" + folder + "/" + nameVar,
                    "/pixelcade/console/" + nameVar,
                    "/console/" + nameVar
                };
                for (const auto& basePath : testPaths) {
                    if (sd.exists(basePath + ".png")) {
                        foundArtPath = basePath + ".png";
                        exists = true;
                        break;
                    }
                    if (sd.exists(basePath + ".gif")) {
                        foundArtPath = basePath + ".gif";
                        exists = true;
                        break;
                    }
                }
                if (exists) break;
            }
        }
    }

    if (reqId != currentRequestId) return;

    if (exists && foundArtPath.length() > 0) {
        LOGI("RetroFrontend", "Playing cached Pixelcade art: %s", foundArtPath.c_str());
        copySafeCStr(m_activeMarqueePath, sizeof(m_activeMarqueePath), foundArtPath.c_str());
        gif->playGif(foundArtPath.c_str());
        if (message) {
            message->deactivate();
        }
        return;
    }

    LOGI("RetroFrontend", "No cached artwork for %s, displaying text title and downloading...", game.c_str());
    if (message) {
        String clean = cleanSystemName(game);
        clean.replace("-", " ");
        clean.replace("_", " ");
        MessageConfig cfg = { clean, 0x07FF, 1, clean.length() > 8 ? "rtl" : "none", 40, 0 };
        message->displayMessage(cfg);
    }

    String downloadedPath = "";
    bool downloaded = false;
    for (const String& nameVar : nameVariants) {
        if (downloadPixelcadeArt(folder, nameVar + ".png", downloadedPath, reqId)) {
            downloaded = true;
            break;
        }
        if (reqId != currentRequestId) return;

        if (downloadPixelcadeArt(folder, nameVar + ".gif", downloadedPath, reqId)) {
            downloaded = true;
            break;
        }
        if (reqId != currentRequestId) return;
    }

    if (reqId != currentRequestId) return;

    if (downloaded && downloadedPath.length() > 0) {
        LOGI("RetroFrontend", "Playing downloaded Pixelcade art: %s", downloadedPath.c_str());
        copySafeCStr(m_activeMarqueePath, sizeof(m_activeMarqueePath), downloadedPath.c_str());
        gif->playGif(downloadedPath.c_str());
        if (message) {
            message->deactivate();
        }
        return;
    }

    // Fallback: If no game marquee is found, fallback to system marquee (e.g. /console/snes.png)
    if (cleanSystem.length() > 0) {
        LOGI("RetroFrontend", "No game marquee found for %s (%s), falling back to system marquee...", game.c_str(), cleanSystem.c_str());
        handleSystemEvent(cleanSystem, reqId);
    }
}

void FrontendSyncEngine::handleSystemEvent(const String& systemId, uint32_t reqId) {
    isGamePlaying = true;
    waitingDisplayed = false;
    if (message) message->deactivate();

    std::vector<SystemVariant> variants = getSystemNameVariantsMapped(systemMappings, systemId);

    String foundArtPath = "";
    bool exists = false;

    {
        SdLockGuard guard(pdMS_TO_TICKS(500));
        if (guard) {
            for (const auto& v : variants) {
                String testPaths[] = {
                    "/pixelcade/" + v.folder + "/" + v.name,
                    "/" + v.folder + "/" + v.name,
                    "/pixelcade/" + v.name,
                    "/" + v.name
                };
                for (const auto& basePath : testPaths) {
                    if (sd.exists(basePath + ".png")) {
                        foundArtPath = basePath + ".png";
                        exists = true;
                        break;
                    }
                    if (sd.exists(basePath + ".gif")) {
                        foundArtPath = basePath + ".gif";
                        exists = true;
                        break;
                    }
                }
                if (exists) break;
            }
        }
    }

    if (reqId != currentRequestId) return;

    if (exists && foundArtPath.length() > 0) {
        LOGI("RetroFrontend", "Playing cached Pixelcade system art: %s", foundArtPath.c_str());
        copySafeCStr(m_activeMarqueePath, sizeof(m_activeMarqueePath), foundArtPath.c_str());
        gif->playGif(foundArtPath.c_str());
        if (message) {
            message->deactivate();
        }
        return;
    }

    LOGI("RetroFrontend", "No cached artwork for system %s, displaying text title and downloading...", systemId.c_str());
    if (message) {
        String clean = cleanSystemName(systemId);
        clean.replace("-", " ");
        clean.replace("_", " ");
        clean.toUpperCase();
        MessageConfig cfg = { clean, 0x07FF, 1, clean.length() > 8 ? "rtl" : "none", 40, 0 };
        message->displayMessage(cfg);
    }

    String downloadedPath = "";
    bool downloaded = false;
    for (const auto& v : variants) {
        if (downloadPixelcadeArt(v.folder, v.name + ".png", downloadedPath, reqId)) {
            downloaded = true;
            break;
        }
        if (reqId != currentRequestId) return;

        if (downloadPixelcadeArt(v.folder, v.name + ".gif", downloadedPath, reqId)) {
            downloaded = true;
            break;
        }
        if (reqId != currentRequestId) return;
    }

    if (reqId != currentRequestId) return;

    if (downloaded && downloadedPath.length() > 0) {
        LOGI("RetroFrontend", "Playing downloaded Pixelcade system art: %s", downloadedPath.c_str());
        copySafeCStr(m_activeMarqueePath, sizeof(m_activeMarqueePath), downloadedPath.c_str());
        gif->playGif(downloadedPath.c_str());
        if (message) {
            message->deactivate();
        }
    }
}

String FrontendSyncEngine::cleanSystemName(const String& rawSystem) {
    String s = rawSystem;
    s.trim();
    
    String sNorm = s;
    sNorm.toLowerCase();
    sNorm.replace("_", " ");
    sNorm.replace("-", " ");
    
    const char* prefixes[] = {
        "arcade manufacturer ",
        "arcade system ",
        "arcade genre ",
        "arcade collection ",
        "manufacturer ",
        "system ",
        "genre ",
        "collection "
    };
    
    for (const char* p : prefixes) {
        if (sNorm.startsWith(p)) {
            int len = strlen(p);
            if (len <= s.length()) {
                String remainder = s.substring(len);
                if (remainder.startsWith("_") || remainder.startsWith("-")) {
                    remainder = remainder.substring(1);
                }
                remainder.trim();
                return remainder;
            }
        }
    }
    return s;
}

#include "BuiltinSystemMaps.h"

std::map<String, std::vector<String>> FrontendSyncEngine::loadMappingsFromSD() {
    std::map<String, std::vector<String>> mappings;

    const char* jsonPaths[] = { "/pixelcade/systems.json", "/systems.json" };
    bool loadedJson = false;

    SdLockGuard guard(pdMS_TO_TICKS(2000));
    if (!guard) return mappings;

    for (const char* p : jsonPaths) {
        if (sd.exists(p)) {
            FsFile f = sd.open(p, FILE_OPEN_READ);
            if (f) {
                SpiRamJsonDocument doc(8192);
                DeserializationError err = deserializeJson(doc, f);
                f.close();
                if (!err && doc.is<JsonObject>()) {
                    JsonObject root = doc.as<JsonObject>();
                    for (JsonPair kv : root) {
                        String key = String(kv.key().c_str());
                        key.trim();
                        key.toLowerCase();
                        std::vector<String> targets;
                        if (kv.value().is<JsonArray>()) {
                            for (JsonVariant val : kv.value().as<JsonArray>()) {
                                if (val.is<const char*>()) {
                                    targets.push_back(String(val.as<const char*>()));
                                }
                            }
                        } else if (kv.value().is<const char*>()) {
                            targets.push_back(String(kv.value().as<const char*>()));
                        }
                        if (key.length() > 0 && targets.size() > 0) {
                            mappings[key] = targets;
                        }
                    }
                    LOGI("RetroFrontend", "Loaded system mappings from SD JSON: %s (%u entries)", p, (unsigned int)mappings.size());
                    loadedJson = true;
                    break;
                }
            }
        }
    }

    if (!loadedJson) {
        const char* csvPaths[] = { "/pixelcade/console.csv", "/console.csv" };
        for (const char* p : csvPaths) {
            if (sd.exists(p)) {
                FsFile f = sd.open(p, FILE_OPEN_READ);
                if (f) {
                    while (f.available()) {
                        String line = f.readStringUntil('\n');
                        line.trim();
                        if (line.length() == 0 || line.startsWith("#")) continue;
                        int commaIdx = line.indexOf(',');
                        if (commaIdx != -1) {
                            String key = line.substring(0, commaIdx);
                            key.trim();
                            key.toLowerCase();
                            String rest = line.substring(commaIdx + 1);
                            std::vector<String> targets;
                            while (rest.length() > 0) {
                                int nextComma = rest.indexOf(',');
                                String target;
                                if (nextComma != -1) {
                                    target = rest.substring(0, nextComma);
                                    rest = rest.substring(nextComma + 1);
                                } else {
                                    target = rest;
                                    rest = "";
                                }
                                target.trim();
                                if (target.length() > 0) {
                                    targets.push_back(target);
                                }
                            }
                            if (key.length() > 0 && targets.size() > 0) {
                                mappings[key] = targets;
                            }
                        }
                    }
                    f.close();
                    LOGI("RetroFrontend", "Loaded system mappings from SD CSV: %s", p);
                    break;
                }
            }
        }
    }
    return mappings;
}

static void appendTargetsFromMappingsOrBuiltin(
    const std::map<String, std::vector<String>>& mappings,
    const String& key,
    std::vector<String>& out) {
    if (key.length() == 0) return;

    // 1. High priority: User / systems.json explicit mappings from SD
    auto it = mappings.find(key);
    if (it != mappings.end()) {
        for (const auto& target : it->second) {
            bool exists = false;
            for (const auto& nv : out) {
                if (nv == target) { exists = true; break; }
            }
            if (!exists) out.push_back(target);
        }
    }

    // 2. Firmware built-in default mappings in flash (.rodata, zero heap allocation)
    for (size_t i = 0; BUILTIN_SYSTEM_MAPS[i].key != nullptr; i++) {
        if (key.equalsIgnoreCase(BUILTIN_SYSTEM_MAPS[i].key)) {
            const char* targetsStr = BUILTIN_SYSTEM_MAPS[i].targets;
            const char* cur = targetsStr;
            while (*cur) {
                const char* comma = strchr(cur, ',');
                String t;
                if (comma) {
                    t = String(cur).substring(0, comma - cur);
                    cur = comma + 1;
                } else {
                    t = String(cur);
                    cur += strlen(cur);
                }
                t.trim();
                if (t.length() > 0) {
                    bool exists = false;
                    for (const auto& nv : out) {
                        if (nv == t) { exists = true; break; }
                    }
                    if (!exists) out.push_back(t);
                }
            }
            break;
        }
    }
}

std::vector<FrontendSyncEngine::SystemVariant> FrontendSyncEngine::getSystemNameVariants(const String& rawSystem) {
    std::map<String, std::vector<String>> emptyMap;
    return getSystemNameVariantsMapped(emptyMap, rawSystem);
}

std::vector<FrontendSyncEngine::SystemVariant> FrontendSyncEngine::getSystemNameVariantsMapped(
    const std::map<String, std::vector<String>>& mappings,
    const String& rawSystem) {
    String clean = cleanSystemName(rawSystem);
    String rawLower = rawSystem;
    rawLower.trim();
    rawLower.toLowerCase();

    std::vector<String> nameVariants;

    String sysLower = clean;
    sysLower.toLowerCase();
    String sysNospace = sysLower;
    sysNospace.replace(" ", "");
    String sysUnderscore = sysLower;
    sysUnderscore.replace(" ", "_");

    // 1. High priority: User / systems.json explicit mappings & flash defaults
    String lookupKeys[] = { rawLower, sysLower, sysNospace, sysUnderscore };
    for (const auto& key : lookupKeys) {
        appendTargetsFromMappingsOrBuiltin(mappings, key, nameVariants);
    }

    // Check embedded keywords in multi-word names (e.g., "Capcom cps1" -> "cps1", "capcom")
    const char* embeddedKeywords[] = { "cps1", "cps2", "cps3", "atomiswave", "naomi", "neogeo" };
    for (const char* kw : embeddedKeywords) {
        if (sysNospace.indexOf(kw) != -1) {
            String kwStr = String(kw);
            appendTargetsFromMappingsOrBuiltin(mappings, kwStr, nameVariants);
            String defZ = "default-z" + kwStr;
            bool existsZ = false;
            for (const auto& nv : nameVariants) {
                if (nv == defZ) { existsZ = true; break; }
            }
            if (!existsZ) nameVariants.push_back(defZ);

            String def = "default-" + kwStr;
            bool existsDef = false;
            for (const auto& nv : nameVariants) {
                if (nv == def) { existsDef = true; break; }
            }
            if (!existsDef) nameVariants.push_back(def);
        }
    }

    // Extract individual words in reverse order (e.g. "cps1" before "capcom")
    int lastSpace = 0;
    std::vector<String> words;
    for (size_t i = 0; i <= sysLower.length(); i++) {
        if (i == sysLower.length() || sysLower[i] == ' ') {
            if (i > (size_t)lastSpace) {
                String w = sysLower.substring(lastSpace, i);
                w.trim();
                if (w.length() > 0 && w != "arcade" && w != "manufacturer" && w != "system" && w != "genre") {
                    words.push_back(w);
                }
            }
            lastSpace = i + 1;
        }
    }
    for (int i = (int)words.size() - 1; i >= 0; i--) {
        const String& w = words[i];
        appendTargetsFromMappingsOrBuiltin(mappings, w, nameVariants);
        String defZ = "default-z" + w;
        bool existsZ = false;
        for (const auto& nv : nameVariants) {
            if (nv == defZ) { existsZ = true; break; }
        }
        if (!existsZ) nameVariants.push_back(defZ);

        String def = "default-" + w;
        bool existsDef = false;
        for (const auto& nv : nameVariants) {
            if (nv == def) { existsDef = true; break; }
        }
        if (!existsDef) nameVariants.push_back(def);
    }

    std::vector<String> baseNames;
    String sysUpper = clean;
    sysUpper.toUpperCase();
    String sysSpace = clean;
    sysSpace.replace("_", " ");

    String sysTitle = clean;
    bool newWord = true;
    for (size_t i = 0; i < sysTitle.length(); i++) {
        if (sysTitle[i] == '_' || sysTitle[i] == ' ') {
            sysTitle[i] = ' ';
            newWord = true;
        } else if (newWord) {
            sysTitle[i] = toupper(sysTitle[i]);
            newWord = false;
        } else {
            sysTitle[i] = tolower(sysTitle[i]);
        }
    }

    baseNames.push_back(clean);
    if (sysLower != clean) baseNames.push_back(sysLower);
    if (sysNospace != clean && sysNospace != sysLower) baseNames.push_back(sysNospace);
    if (sysUnderscore != clean && sysUnderscore != sysLower && sysUnderscore != sysNospace) baseNames.push_back(sysUnderscore);
    if (sysUpper != clean && sysUpper != sysLower) baseNames.push_back(sysUpper);
    if (sysTitle != clean && sysTitle != sysLower && sysTitle != sysUpper) baseNames.push_back(sysTitle);
    if (sysSpace != clean && sysSpace != sysLower && sysSpace != sysUpper) baseNames.push_back(sysSpace);

    if (sysLower == "snes" || sysLower == "supernintendo") {
        baseNames.push_back("Super Nintendo");
        baseNames.push_back("Super Nintendo Entertainment System");
        baseNames.push_back("- Super Nintendo");
    } else if (sysLower == "nes" || sysLower == "famicom") {
        baseNames.push_back("Nintendo Entertainment System");
        baseNames.push_back("3dnes");
    } else if (sysLower == "megadrive" || sysLower == "genesis") {
        baseNames.push_back("genesis");
        baseNames.push_back("Genesis");
        baseNames.push_back("Mega Drive");
        baseNames.push_back("SEGA Genesis");
        baseNames.push_back("- Genesis");
    } else if (sysLower == "mame" || sysLower == "arcade" || sysLower == "fbneo" || sysLower == "fba") {
        baseNames.push_back("arcade");
        baseNames.push_back("Arcade");
        baseNames.push_back("- Arcade");
        baseNames.push_back("MAME");
        baseNames.push_back("mame");
    } else if (sysLower == "n64") {
        baseNames.push_back("Nintendo 64");
    } else if (sysLower == "gb" || sysLower == "gameboy") {
        baseNames.push_back("Game Boy");
    } else if (sysLower == "gba") {
        baseNames.push_back("Game Boy Advance");
    } else if (sysLower == "gbc") {
        baseNames.push_back("Game Boy Color");
    } else if (sysLower == "psx" || sysLower == "ps1") {
        baseNames.push_back("PlayStation");
        baseNames.push_back("Sony PlayStation");
    } else if (sysLower == "dreamcast") {
        baseNames.push_back("Dreamcast");
        baseNames.push_back("SEGA Dreamcast");
    } else if (sysLower == "neogeo") {
        baseNames.push_back("Neo Geo");
        baseNames.push_back("SNK Neo Geo");
    } else if (sysLower == "atari" || sysLower == "atari2600" || sysLower == "atari7800" || sysLower == "atari5200" || sysLower == "atari800" || sysLower == "atarilynx" || sysLower == "atarijaguar" || sysLower == "atarist") {
        baseNames.push_back("atari");
        baseNames.push_back("Atari");
        baseNames.push_back("Atari_2600");
        baseNames.push_back("Atari 2600");
        baseNames.push_back("Atari_7800");
        baseNames.push_back("Atari 7800");
    } else if (sysLower == "mastersystem") {
        baseNames.push_back("Master System");
        baseNames.push_back("SEGA Master System");
    } else if (sysLower == "gamegear") {
        baseNames.push_back("Game Gear");
        baseNames.push_back("SEGA Game Gear");
    } else if (sysLower == "pcengine" || sysLower == "tg16") {
        baseNames.push_back("NEC PC Engine");
        baseNames.push_back("PC Engine");
    } else if (sysLower == "amiga") {
        baseNames.push_back("Commodore Amiga");
        baseNames.push_back("Amiga");
    } else if (sysLower == "c64") {
        baseNames.push_back("COMMODORE_64");
        baseNames.push_back("Commodore 64");
    }

    std::vector<String> uniqueBase;
    for (const auto& n : baseNames) {
        bool found = false;
        for (const auto& un : uniqueBase) {
            if (un == n) { found = true; break; }
        }
        if (!found) uniqueBase.push_back(n);
    }

    String cleanLower = clean;
    cleanLower.toLowerCase();
    String cleanNospace = cleanLower;
    cleanNospace.replace(" ", "");
    String cleanUnderscore = cleanLower;
    cleanUnderscore.replace(" ", "_");
    String cleanKebab = cleanLower;
    cleanKebab.replace(" ", "-");

    // 1. Direct name variants (e.g. cave, atari, capcom, data_east, dataeast)
    nameVariants.push_back(cleanLower);
    nameVariants.push_back(clean);
    nameVariants.push_back(cleanUnderscore);
    nameVariants.push_back(cleanNospace);
    nameVariants.push_back(cleanKebab);

    // 2. default- prefixed variants
    nameVariants.push_back("default-" + clean);
    nameVariants.push_back("default-" + cleanLower);
    nameVariants.push_back("default-" + cleanUnderscore);
    nameVariants.push_back("default-" + cleanNospace);
    nameVariants.push_back("default-" + cleanKebab);
    nameVariants.push_back("default-_" + clean);
    nameVariants.push_back("default-_" + cleanLower);
    nameVariants.push_back("default-_" + cleanUnderscore);

    // 3. Pixelcade z-prefixed board/publisher conventions
    nameVariants.push_back("default-z" + cleanLower);
    nameVariants.push_back("default-z" + cleanNospace);
    nameVariants.push_back("z" + cleanLower);
    nameVariants.push_back("z" + cleanNospace);

    // 4. Publisher classic collections
    nameVariants.push_back("default-arcade_" + cleanUnderscore + "_classics");
    nameVariants.push_back("default-arcade" + cleanNospace + "classics");
    nameVariants.push_back("default-manufacture_" + cleanUnderscore);
    nameVariants.push_back("default-manufacture_" + cleanLower);

    for (const auto& b : uniqueBase) {
        String bLower = b;
        bLower.toLowerCase();
        String bNospace = bLower;
        bNospace.replace(" ", "");
        String bUnderscore = bLower;
        bUnderscore.replace(" ", "_");

        nameVariants.push_back(b);
        nameVariants.push_back(bLower);
        nameVariants.push_back(bUnderscore);
        nameVariants.push_back("default-" + b);
        nameVariants.push_back("default-" + bLower);
        nameVariants.push_back("default-_" + b);
        nameVariants.push_back("default-z" + bLower);
        nameVariants.push_back("default-z" + bNospace);
        nameVariants.push_back("default-arcade_" + bUnderscore + "_classics");
        nameVariants.push_back("default-arcade" + bNospace + "classics");
        nameVariants.push_back("default-manufacture_" + bUnderscore);
        nameVariants.push_back("default-manufacture_" + bLower);
    }

    std::vector<String> finalNames;
    for (const auto& n : nameVariants) {
        bool found = false;
        for (const auto& fn : finalNames) {
            if (fn == n) { found = true; break; }
        }
        if (!found) finalNames.push_back(n);
    }

    // Folder search is exclusively /console/
    std::vector<SystemVariant> list;
    for (const auto& n : finalNames) {
        list.push_back({ "console", n });
    }

    return list;
}

String FrontendSyncEngine::mapSystemToPixelcadeFolder(const String& systemId) {
    String sLower = systemId;
    sLower.trim();
    sLower.toLowerCase();
    sLower.replace(" ", "");
    sLower.replace("_", "");
    sLower.replace("-", "");

    struct Mapping { const char* systemId; const char* folder; };
    static const Mapping table[] = {
        {"mame", "mame"}, {"fbneo", "mame"}, {"fba", "mame"}, {"arcade", "mame"},
        {"cave", "mame"}, {"capcom", "mame"}, {"cps1", "mame"}, {"cps2", "mame"}, {"cps3", "mame"},
        {"konami", "mame"}, {"taito", "mame"}, {"dataeast", "mame"}, {"midway", "mame"},
        {"irem", "mame"}, {"namco", "mame"}, {"toaplan", "mame"}, {"technos", "mame"},
        {"sammy", "mame"}, {"atomiswave", "mame"}, {"naomi", "mame"}, {"neogeo", "neogeo"}, {"snk", "mame"},
        {"nes", "nes"}, {"famicom", "nes"},
        {"snes", "snes"}, {"supernintendo", "snes"},
        {"n64", "n64"}, {"nintendo64", "n64"},
        {"gb", "gb"}, {"gameboy", "gb"},
        {"gba", "gba"}, {"gameboyadvance", "gba"},
        {"gbc", "gbc"}, {"gameboycolor", "gbc"},
        {"megadrive", "genesis"}, {"genesis", "genesis"},
        {"mastersystem", "mastersystem"}, {"gamegear", "gamegear"},
        {"psx", "psx"}, {"ps1", "psx"}, {"playstation", "psx"},
        {"ps2", "ps2"}, {"psp", "psp"},
        {"dreamcast", "dreamcast"}, {"saturn", "saturn"},
        {"pcengine", "pcengine"}, {"tg16", "pcengine"},
        {"atari2600", "atari2600"}, {"atari5200", "atari5200"}, {"atari7800", "atari7800"}
    };
    for (const auto& m : table) {
        if (sLower == m.systemId) return String(m.folder);
    }
    return systemId;
}

bool FrontendSyncEngine::downloadPixelcadeArt(const String& folder, const String& filename, String& outPath, uint32_t reqId) {

    String baseUrl = "https://raw.githubusercontent.com/red77290/pixelcade/master/" + folder + "/";
    // URL encode the filename spaces if any
    String urlFileName = filename;
    urlFileName.replace(" ", "%20");
    urlFileName.replace("!", "%21");
    urlFileName.replace("'", "%27");
    urlFileName.replace("(", "%28");
    urlFileName.replace(")", "%29");
    urlFileName.replace("&", "%26");
    String url = baseUrl + urlFileName;
    
    String dirPath = "/pixelcade/" + folder;
    String savePath = dirPath + "/" + filename;

    auto writeStreamToSd = [&](Stream& stream, int len, const String& dPath, const String& sPath) -> bool {
        SdLockGuard sdGuard(pdMS_TO_TICKS(15000));
        if (!sdGuard) {
            LOGW("RetroFrontend", "Could not acquire sdMutex for artwork download: %s", sPath.c_str());
            return false;
        }

        if (!sd.exists("/pixelcade")) {
            sd.mkdir("/pixelcade");
        }
        if (!sd.exists(dPath.c_str())) {
            sd.mkdir(dPath.c_str());
        }

        FsFile file = sd.open(sPath.c_str(), FILE_OPEN_WRITE);
        if (!file) {
            LOGE("RetroFrontend", "Failed to open file for writing: %s", sPath.c_str());
            return false;
        }

        uint8_t buff[512] = { 0 };
        int bytesWrittenTotal = 0;
        unsigned long lastLog = millis();
        uint32_t startMs = millis();

        while (len > 0 || len == -1) {
            size_t size = stream.available();
            if (size) {
                int toRead = (size > sizeof(buff)) ? sizeof(buff) : size;
                if (len > 0 && toRead > len) toRead = len;
                int c = stream.readBytes(reinterpret_cast<char*>(buff), toRead);
                if (c > 0) {
                    file.write(buff, c);
                    bytesWrittenTotal += c;
                    if (len > 0) len -= c;
                    startMs = millis();
                }
            } else {
                if (millis() - startMs > 4000) break;
                delay(1);
            }

            esp_task_wdt_reset();

            if (millis() - lastLog > 2000) {
                LOGI("RetroFrontend", "Downloading... %d bytes written", bytesWrittenTotal);
                lastLog = millis();
            }

            if (internalBroker) internalBroker->loop();
            if (mqttClient.connected()) mqttClient.loop();

            if (reqId != currentRequestId) {
                LOGI("RetroFrontend", "User scrolled to a new game, aborting download of %s", sPath.c_str());
                file.close();
                sd.remove(sPath.c_str());
                return false;
            }
        }

        file.close();

        if (bytesWrittenTotal > 0) {
            bool validFile = false;
            if (sd.exists(sPath.c_str())) {
                FsFile checkFile = sd.open(sPath.c_str(), FILE_OPEN_READ);
                if (checkFile) {
                    validFile = (checkFile.size() > 100);
                    checkFile.close();
                }
            }
            if (validFile) {
                outPath = sPath;
                LOGI("RetroFrontend", "Successfully downloaded and saved to %s", sPath.c_str());
                return true;
            }
            sd.remove(sPath.c_str());
        }
        return false;
    };

    bool isHttps = url.startsWith("https://");
    bool downloaded = false;

    if (isHttps) {
        net::SecureHttpOptions options;
        options.requestTimeoutMs = 6000;
        options.handshakeTimeoutSec = 4;
        options.userAgent = "ArcadeMatrix-ESP32";
        options.followRedirects = true;

        LOGI("RetroFrontend", "Downloading %s via SecureHttpClient", url.c_str());
        auto response = net::SecureHttpClient::get(url, options);
        if (response.ok()) {
            downloaded = writeStreamToSd(response.stream(), response.contentLength(), dirPath, savePath);
        } else {
            LOGW("RetroFrontend", "HTTPS GET failed with code %d (%s)", response.statusCode(), response.errorMessage());
        }
    } else {
        HTTPClient http;
        http.setTimeout(4000);
        http.setUserAgent("ArcadeMatrix-ESP32");
        WiFiClient plainClient;
        if (http.begin(plainClient, url)) {
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                downloaded = writeStreamToSd(*http.getStreamPtr(), http.getSize(), dirPath, savePath);
            }
            http.end();
            plainClient.stop();
        }
    }

    return downloaded;
}
