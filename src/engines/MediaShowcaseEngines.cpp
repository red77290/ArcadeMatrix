#include "MediaShowcaseEngines.h"
#include "../core/drawing/IDrawingSurface.h"
#include "../core/ConfigLoader.h"
#include "../core/I18n.h"
#include "../core/Logger.h"
#include "../core/net/SecureHttpClient.h"
#include "../hal/HardwareHAL.h"
#include <ArduinoJson.h>
#include <cstring>
#include <cstdlib>
#if defined(ESP32)
#include <esp_heap_caps.h>
#if !defined(UNIT_TEST)
#include <WiFi.h>
#include <WiFiUdp.h>
#endif
#endif

extern ConfigLoader config;

namespace {

uint16_t parseHexColor565(const char* hex, uint16_t fallback) {
    if (!hex) return fallback;
    if (*hex == '#') hex++;
    if (strlen(hex) < 6) return fallback;
    char buf[7];
    memcpy(buf, hex, 6);
    buf[6] = '\0';
    uint32_t rgb = static_cast<uint32_t>(strtoul(buf, nullptr, 16));
    uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(rgb & 0xFF);
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void copySafeStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

void appendCsvAsJsonArray(String& out, const char* csv) {
    out += "[";
    if (csv && csv[0] != '\0') {
        bool first = true;
        const char* p = csv;
        while (*p != '\0') {
            while (*p == ' ' || *p == ',') ++p;
            if (*p == '\0') break;
            const char* start = p;
            while (*p != '\0' && *p != ',') ++p;
            const char* end = p;
            while (end > start && *(end - 1) == ' ') --end;
            if (end > start) {
                size_t len = static_cast<size_t>(end - start);
                if (!(len == 3 && strncasecmp(start, "all", 3) == 0)) {
                    if (!first) out += ",";
                    out += "\"";
                    for (size_t i = 0; i < len; ++i) {
                        char c = start[i];
                        if (c != '"' && c != '\\') out += c;
                    }
                    out += "\"";
                    first = false;
                }
            }
        }
    }
    out += "]";
}

bool decodeBase64Fixed(const char* b64, uint8_t* dst, size_t expectedBytes) {
    if (!b64 || !dst || expectedBytes == 0) return false;
    size_t outIdx = 0;
    uint32_t buf = 0;
    uint8_t bits = 0;
    for (const char* p = b64; *p != '\0'; ++p) {
        char c = *p;
        if (c == '=') break;
        uint8_t val = 0;
        if (c >= 'A' && c <= 'Z') val = static_cast<uint8_t>(c - 'A');
        else if (c >= 'a' && c <= 'z') val = static_cast<uint8_t>(c - 'a' + 26);
        else if (c >= '0' && c <= '9') val = static_cast<uint8_t>(c - '0' + 52);
        else if (c == '+' || c == '-') val = 62;
        else if (c == '/' || c == '_') val = 63;
        else if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        else return false;

        buf = (buf << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (outIdx < expectedBytes) {
                dst[outIdx++] = static_cast<uint8_t>((buf >> bits) & 0xFF);
            }
        }
    }
    return (outIdx == expectedBytes);
}

bool parseHostAndPort(const char* rawUrl, uint16_t defaultPort, char* outHost, size_t hostCap, uint16_t& outPort) {
    if (!rawUrl || !outHost || hostCap == 0) return false;
    while (*rawUrl == ' ') rawUrl++;
    if (*rawUrl == '\0') return false;

    const char* p = rawUrl;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "mqtt://", 7) == 0) p += 7;
    else if (strncmp(p, "tcp://", 6) == 0) p += 6;

    const char* slash = strchr(p, '/');
    size_t authLen = slash ? static_cast<size_t>(slash - p) : strlen(p);
    if (authLen == 0) return false;

    char auth[96];
    size_t copyLen = (authLen < sizeof(auth) - 1) ? authLen : (sizeof(auth) - 1);
    memcpy(auth, p, copyLen);
    auth[copyLen] = '\0';

    char* colon = strrchr(auth, ':');
    outPort = defaultPort;
    if (colon) {
        *colon = '\0';
        int parsed = atoi(colon + 1);
        if (parsed > 0 && parsed <= 65535) {
            outPort = static_cast<uint16_t>(parsed);
        }
    }
    if (auth[0] == '\0') return false;
    copySafeStr(outHost, hostCap, auth);
    return true;
}

bool readExactFromStream(Stream& st, uint8_t* dst, size_t len, const std::atomic<bool>& wantActive, uint32_t timeoutMs = 2500) {
    size_t got = 0;
    uint32_t lastProgress = millis();
    while (got < len) {
        if (!wantActive.load(std::memory_order_acquire)) return false;
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

bool drainFromStream(Stream& st, size_t len, const std::atomic<bool>& wantActive, uint32_t timeoutMs = 1500) {
    uint8_t sink[64];
    size_t rem = len;
    while (rem > 0) {
        size_t chunk = (rem < sizeof(sink)) ? rem : sizeof(sink);
        if (!readExactFromStream(st, sink, chunk, wantActive, timeoutMs)) {
            return false;
        }
        rem -= chunk;
    }
    return true;
}

#if defined(ESP32) && !defined(UNIT_TEST)
size_t encodeMqttRemainingLength(uint8_t* out, size_t remLen) {
    size_t idx = 0;
    do {
        uint8_t enc = static_cast<uint8_t>(remLen % 128);
        remLen /= 128;
        if (remLen > 0) enc |= 0x80;
        out[idx++] = enc;
    } while (remLen > 0 && idx < 4);
    return idx;
}

bool sendMqttPublishPacket(WiFiClient& cli, const char* topic, const char* payload, bool retain) {
    if (!cli.connected() || !topic || !payload) return false;
    uint16_t tLen = static_cast<uint16_t>(strlen(topic));
    size_t pLen = strlen(payload);
    size_t remLen = 2 + tLen + pLen;
    uint8_t hdr[8];
    hdr[0] = static_cast<uint8_t>(0x30 | (retain ? 0x01 : 0x00));
    size_t rlBytes = encodeMqttRemainingLength(hdr + 1, remLen);
    cli.write(hdr, 1 + rlBytes);
    uint8_t tLenBe[2] = {static_cast<uint8_t>((tLen >> 8) & 0xFF), static_cast<uint8_t>(tLen & 0xFF)};
    cli.write(tLenBe, 2);
    cli.write(reinterpret_cast<const uint8_t*>(topic), tLen);
    if (pLen > 0) {
        cli.write(reinterpret_cast<const uint8_t*>(payload), pLen);
    }
    return true;
}

bool sendMqttSubscribePacket(WiFiClient& cli, uint16_t pktId, const char* topic) {
    if (!cli.connected() || !topic) return false;
    uint16_t tLen = static_cast<uint16_t>(strlen(topic));
    size_t remLen = 2 + 2 + tLen + 1;
    uint8_t hdr[8];
    hdr[0] = 0x82;
    size_t rlBytes = encodeMqttRemainingLength(hdr + 1, remLen);
    cli.write(hdr, 1 + rlBytes);
    uint8_t pidBe[2] = {static_cast<uint8_t>((pktId >> 8) & 0xFF), static_cast<uint8_t>(pktId & 0xFF)};
    cli.write(pidBe, 2);
    uint8_t tLenBe[2] = {static_cast<uint8_t>((tLen >> 8) & 0xFF), static_cast<uint8_t>(tLen & 0xFF)};
    cli.write(tLenBe, 2);
    cli.write(reinterpret_cast<const uint8_t*>(topic), tLen);
    uint8_t qos = 0;
    cli.write(&qos, 1);
    return true;
}

bool sendMqttConnectPacket(WiFiClient& cli, const char* clientId) {
    uint16_t cidLen = static_cast<uint16_t>(strlen(clientId));
    size_t remLen = 10 + 2 + cidLen;
    uint8_t hdr[8];
    hdr[0] = 0x10;
    size_t rlBytes = encodeMqttRemainingLength(hdr + 1, remLen);
    cli.write(hdr, 1 + rlBytes);
    const uint8_t varHdr[10] = {0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02, 0x00, 0x1E};
    cli.write(varHdr, 10);
    uint8_t cidBe[2] = {static_cast<uint8_t>((cidLen >> 8) & 0xFF), static_cast<uint8_t>(cidLen & 0xFF)};
    cli.write(cidBe, 2);
    cli.write(reinterpret_cast<const uint8_t*>(clientId), cidLen);
    return true;
}
#endif

} // namespace

MediaShowcaseBase::MediaShowcaseBase(ShowcaseDomainId domain) : m_domain(domain) {
    const char* slug = domainSlug();
    snprintf(m_sessionId, sizeof(m_sessionId), "esp32_%s", slug);
    m_liveCard.badgeColor565 = defaultAccent565();
    m_liveCard.hasData = false;
    m_renderCard = m_liveCard;
}

MediaShowcaseBase::~MediaShowcaseBase() {
    shutdownForDestruction();
}

const char* MediaShowcaseBase::domainSlug() const {
    switch (m_domain) {
        case ShowcaseDomainId::Games:  return "games";
        case ShowcaseDomainId::Movies: return "movies";
        case ShowcaseDomainId::Series: return "series";
        case ShowcaseDomainId::Anime:  return "anime";
        default:                       return "games";
    }
}

const char* MediaShowcaseBase::domainBadgeLabel() const {
    switch (m_domain) {
        case ShowcaseDomainId::Games:  return "RETRO GAMES";
        case ShowcaseDomainId::Movies: return "MOVIES";
        case ShowcaseDomainId::Series: return "TV SERIES";
        case ShowcaseDomainId::Anime:  return "ANIME";
        default:                       return "SHOWCASE";
    }
}

uint16_t MediaShowcaseBase::defaultAccent565() const {
    switch (m_domain) {
        case ShowcaseDomainId::Games:  return 0xFD20; // Arcade Gold/Orange
        case ShowcaseDomainId::Movies: return 0x07FF; // Cinema Cyan
        case ShowcaseDomainId::Series: return 0xF945; // Series Crimson
        case ShowcaseDomainId::Anime:  return 0xF81F; // Anime Magenta
        default:                       return 0xFD20;
    }
}

uint32_t MediaShowcaseBase::synopsisPxPerSec(const char* speedStr) {
    if (!speedStr || speedStr[0] == '\0') return 32U;
    if (strcmp(speedStr, "1") == 0) return 14U;
    if (strcmp(speedStr, "2") == 0 || strcasecmp(speedStr, "slow") == 0) return 18U;
    if (strcmp(speedStr, "3") == 0) return 24U;
    if (strcmp(speedStr, "4") == 0 || strcasecmp(speedStr, "normal") == 0) return 32U;
    if (strcmp(speedStr, "5") == 0) return 42U;
    if (strcmp(speedStr, "6") == 0 || strcasecmp(speedStr, "fast") == 0) return 56U;
    if (strcmp(speedStr, "7") == 0) return 75U;
    if (strcmp(speedStr, "8") == 0) return 100U;
    if (strcmp(speedStr, "9") == 0) return 130U;
    if (strcmp(speedStr, "10") == 0) return 160U;
    return 32U;
}

void MediaShowcaseBase::computeCardPosterDims(uint16_t panelW, uint16_t panelH, uint16_t& outW, uint16_t& outH) {
    if (panelH > panelW) {
        uint16_t side = (panelW >= 64) ? 40 : 24;
        outW = side;
        outH = side;
    } else if (panelW == panelH) {
        outW = 32;
        outH = 32;
    } else if (panelH >= 64) {
        outW = 48;
        outH = 64;
    } else {
        outW = 32;
        outH = 32;
    }
}

bool MediaShowcaseBase::resolveMqttBroker(const ConfigSlot& cfg, char* outHost, size_t hostCap, uint16_t& outPort) {
    if (cfg.useShowcaseBroker) {
        uint16_t dummyPort = 8080;
        if (!parseHostAndPort(cfg.serverUrl, 8080, outHost, hostCap, dummyPort)) {
            return false;
        }
        outPort = 1883;
        return true;
    }
    if (strcasecmp(cfg.mqttBrokerSource, "custom") == 0 && cfg.mqttCustomUrl[0] != '\0') {
        if (parseHostAndPort(cfg.mqttCustomUrl, 1883, outHost, hostCap, outPort)) {
            return true;
        }
    }
    {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const auto& snap = guard.get();
        if (snap.mqtt.broker.length() > 0) {
            copySafeStr(outHost, hostCap, snap.mqtt.broker.c_str());
            outPort = (snap.mqtt.port > 0 && snap.mqtt.port <= 65535)
                ? static_cast<uint16_t>(snap.mqtt.port)
                : 1883;
            return true;
        }
    }
    uint16_t dummyPort = 8080;
    if (parseHostAndPort(cfg.serverUrl, 8080, outHost, hostCap, dummyPort)) {
        outPort = 1883;
        return true;
    }
    return false;
}

bool MediaShowcaseBase::allocateBuffers() {
    freeBuffers();
    size_t posterBytes = 48 * 64 * sizeof(uint16_t);
    m_framePixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    size_t frameBytes = m_framePixels * sizeof(uint16_t);

#if defined(ESP32)
    m_hasPsram = hardwareHAL.capabilities().hasPsram;
    if (m_hasPsram) {
        m_poster565 = static_cast<uint16_t*>(heap_caps_malloc(posterBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_frame565 = static_cast<uint16_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!m_poster565) {
        m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
    }
    if (!m_frame565 && m_hasPsram) {
        m_frame565 = static_cast<uint16_t*>(malloc(frameBytes));
    }
#else
    m_hasPsram = true;
    m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
    m_frame565 = static_cast<uint16_t*>(malloc(frameBytes));
#endif

    if (m_poster565) memset(m_poster565, 0, posterBytes);
    if (m_frame565) memset(m_frame565, 0, frameBytes);
    return (m_poster565 != nullptr);
}

void MediaShowcaseBase::freeBuffers() {
    if (m_poster565) {
        free(m_poster565);
        m_poster565 = nullptr;
    }
    if (m_frame565) {
        free(m_frame565);
        m_frame565 = nullptr;
    }
    m_framePixels = 0;
}

void MediaShowcaseBase::clearLiveCardState() {
    m_seq.fetch_add(1, std::memory_order_acq_rel);
    m_liveCard.hasData = false;
    m_liveCard.hasPoster = false;
    m_liveCard.hasVideoFrame = false;
    m_seq.fetch_add(1, std::memory_order_release);
    m_renderCard.hasData = false;
    m_renderCard.hasPoster = false;
    m_renderCard.hasVideoFrame = false;
}

EngineError MediaShowcaseBase::initialize(EngineContext* context, const EngineConfig* config) {
    if (!context || !context->getSurface()) {
        return EngineError::InitializationFailed;
    }
    m_surface = context->getSurface();
    DisplayGeometry geom = context->getGeometry();
    m_width = geom.width > 0 ? geom.width : 128;
    m_height = geom.height > 0 ? geom.height : 32;

    if (config) {
        onConfigChanged(config);
    }
    if (!allocateBuffers()) {
        return EngineError::MissingResource;
    }
    return EngineError::OK;
}

void MediaShowcaseBase::onConfigChanged(const EngineConfig* config) {
    if (!config) return;
    uint8_t cur = m_cfgIdx.load(std::memory_order_relaxed);
    uint8_t next = (cur + 1) & 1;
    ConfigSlot& slot = m_cfg[next];

    String srv = config->getString("server_url", "http://127.0.0.1:8080");
    srv.trim();
    if (srv.endsWith("/")) srv.remove(srv.length() - 1);
    copySafeStr(slot.serverUrl, sizeof(slot.serverUrl), srv.c_str());

    copySafeStr(slot.transportMode, sizeof(slot.transportMode), config->getString("transport_mode", "mqtt_udp").c_str());
    slot.useShowcaseBroker = config->getBool("use_showcase_broker", true);
    copySafeStr(slot.mqttBrokerSource, sizeof(slot.mqttBrokerSource), config->getString("mqtt_broker_source", "global").c_str());
    copySafeStr(slot.mqttCustomUrl, sizeof(slot.mqttCustomUrl), config->getString("mqtt_custom_url", "").c_str());
    copySafeStr(slot.lang, sizeof(slot.lang), config->getString("lang", "system").c_str());
    copySafeStr(slot.textScrollSpeed, sizeof(slot.textScrollSpeed), config->getString("text_scroll_speed", "4").c_str());
    copySafeStr(slot.videoMode, sizeof(slot.videoMode), config->getString("video_mode", "auto_action").c_str());
    copySafeStr(slot.systems, sizeof(slot.systems), config->getString("systems", "").c_str());
    copySafeStr(slot.genres, sizeof(slot.genres), config->getString("genres", "").c_str());
    copySafeStr(slot.decades, sizeof(slot.decades), config->getString("decades", "").c_str());
    copySafeStr(slot.videoType, sizeof(slot.videoType), config->getString("video_type", "any").c_str());

    slot.snapshotPanoramaCount = static_cast<uint8_t>(std::max(0, std::min(5, config->getInt("snapshot_panorama_count", 0))));
    slot.videoMaxDurationS = static_cast<uint8_t>(std::max(2, std::min(30, config->getInt("video_max_duration_s", 8))));
    slot.videoFps = static_cast<uint8_t>(std::max(8, std::min(30, config->getInt("video_fps", 15))));

    slot.showPoster = config->getBool("show_poster", true);
    slot.showSynopsis = config->getBool("show_synopsis", true);
    slot.jacketPanorama = config->getBool("jacket_panorama", true);
    slot.marqueePanorama = config->getBool("marquee_panorama", false);
    slot.showVideo = config->getBool("show_video", true);
    slot.colorBoost = config->getBool("color_boost", true);

    m_fallbackCycleMs = std::max<uint32_t>(22000U, static_cast<uint32_t>(slot.videoMaxDurationS + 24) * 1000U);

    m_cfgIdx.store(next, std::memory_order_release);
    m_cfgGen.fetch_add(1, std::memory_order_acq_rel);
}

void MediaShowcaseBase::activate() {
    m_activatedAtMs = millis();
    m_phaseStartedAtMs = m_activatedAtMs;
    m_lastScrollStepMs = m_activatedAtMs;
    m_skipTurnImmediately.store(false, std::memory_order_release);
    m_warmingFallback.store(false, std::memory_order_release);
    m_turnDone.store(false, std::memory_order_release);
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
    // Strict Consume-Once & Zero-Cache: start every turn with a clean slate
    clearLiveCardState();
    m_dirty = true;

    if (!m_poster565) {
        allocateBuffers();
    }
    startWorkerTask();
}

void MediaShowcaseBase::deactivate() {
    m_wantActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
    stopWorkerTaskBounded(1500);
    clearLiveCardState();
    freeBuffers();
}

bool MediaShowcaseBase::shutdownForDestruction() {
    m_wantActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
    stopWorkerTaskBounded(2000);
    clearLiveCardState();
    freeBuffers();
    return !m_taskAlive.load(std::memory_order_acquire);
}

bool MediaShowcaseBase::isFinished() const {
    if (m_skipTurnImmediately.load(std::memory_order_acquire)) {
        return true;
    }
    if (m_turnDone.load(std::memory_order_acquire)) {
        return true;
    }
    return (millis() - m_activatedAtMs) >= m_fallbackCycleMs;
}

void MediaShowcaseBase::startWorkerTask() {
    m_wantActive.store(true, std::memory_order_release);
#if defined(ESP32) && !defined(UNIT_TEST)
    if (m_taskAlive.load(std::memory_order_acquire)) return;
    m_taskAlive.store(true, std::memory_order_release);
    BaseType_t ok = xTaskCreatePinnedToCore(
        workerTaskEntry,
        "ShowcaseWkr",
        8192,
        this,
        1,
        &m_task,
        0 // Strictly Core 0
    );
    if (ok != pdPASS) {
        m_taskAlive.store(false, std::memory_order_release);
        m_task = nullptr;
    }
#endif
}

void MediaShowcaseBase::stopWorkerTaskBounded(uint32_t timeoutMs) {
#if defined(ESP32) && !defined(UNIT_TEST)
    uint32_t start = millis();
    while (m_taskAlive.load(std::memory_order_acquire) && (millis() - start) < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    m_task = nullptr;
#else
    (void)timeoutMs;
#endif
}

void MediaShowcaseBase::workerTaskEntry(void* arg) {
    auto* self = static_cast<MediaShowcaseBase*>(arg);
    if (self) {
        self->runWorkerLoop();
        self->m_taskAlive.store(false, std::memory_order_release);
    }
#if defined(ESP32)
    vTaskDelete(nullptr);
#endif
}

void MediaShowcaseBase::runWorkerLoop() {
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    const char* domain = domainSlug();

    if (strlen(cfg.serverUrl) < 8 || strncmp(cfg.serverUrl, "http", 4) != 0) {
        m_statusState.store(3, std::memory_order_release); // Unconfigured
        clearLiveCardState();
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
        }
        return;
    }

    String reqLang = String(cfg.lang);
    if (reqLang == "system" || reqLang.length() == 0) {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        reqLang = guard.get().system.lang;
        if (reqLang.length() == 0) reqLang = "en";
    }

    snprintf(m_sessionId, sizeof(m_sessionId), "esp32_%s_%ux%u", domain, m_width, m_height);

    if (strcasecmp(cfg.transportMode, "mqtt_udp") == 0) {
        if (runMqttUdpTurn(cfg, domain, reqLang.c_str())) {
            return;
        }
    }
    if (m_wantActive.load(std::memory_order_acquire)) {
        runHttpPollTurn(cfg, domain, reqLang.c_str());
    }
}

bool MediaShowcaseBase::parseReadyPayloadInPlace(
    char* jsonBuf,
    bool showPoster,
    String& outItemId,
    bool& outHasStream,
    uint32_t& outStreamMaxMs,
    bool& outExplicitWarming
) {
    outExplicitWarming = false;
    if (!jsonBuf || jsonBuf[0] == '\0') return false;

    // ArduinoJson v6 zero-copy mode: passing mutable `char*` avoids duplicating
    // large strings (`poster_rgb565_b64`, `synopsis`) inside the JsonDocument pool.
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, jsonBuf) != DeserializationError::Ok) {
        return false;
    }

    bool rdy = doc["ready"] | false;
    if (!rdy) {
        outExplicitWarming = true;
        return false;
    }

    JsonObject cardObj = doc["card"].as<JsonObject>();
    if (cardObj.isNull()) {
        outExplicitWarming = true;
        return false;
    }

    const char* domain = domainSlug();
    CardSnapshot nextCard;
    copySafeStr(nextCard.title, sizeof(nextCard.title), cardObj["title"] | "UNKNOWN");
    copySafeStr(nextCard.year, sizeof(nextCard.year), cardObj["year"] | "");
    const char* badge = cardObj["badge"] | domain;
    copySafeStr(nextCard.badge, sizeof(nextCard.badge), badge);
    copySafeStr(nextCard.rating, sizeof(nextCard.rating), cardObj["rating"] | "");
    copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), cardObj["synopsis"] | "");
    nextCard.badgeColor565 = parseHexColor565(cardObj["badge_color"] | nullptr, defaultAccent565());
    computeCardPosterDims(m_width, m_height, nextCard.posterW, nextCard.posterH);
    nextCard.hasData = true;
    nextCard.hasPoster = false;
    nextCard.hasVideoFrame = false;

    const char* rawItemId = doc["item_id"] | (cardObj["item_id"] | (cardObj["id"] | ""));
    outItemId = String(rawItemId);
    outHasStream = (cardObj["has_panoramas"] | false) || (cardObj["has_video"] | false);
    float totDur = cardObj["stream_duration_s"] | (cardObj["total_stream_duration_s"] | 8.0f);
    if (totDur < 2.0f) totDur = 2.0f;
    outStreamMaxMs = static_cast<uint32_t>((totDur + 4.0f) * 1000.0f);

    const char* posterB64 = cardObj["poster_rgb565_b64"] | "";
    size_t expectedPosterBytes = static_cast<size_t>(nextCard.posterW) * static_cast<size_t>(nextCard.posterH) * 2U;
    if (showPoster && m_poster565 && expectedPosterBytes <= (48U * 64U * 2U)) {
        if (posterB64[0] != '\0' && decodeBase64Fixed(posterB64, reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes)) {
            nextCard.hasPoster = true;
        }
    }

    m_seq.fetch_add(1, std::memory_order_acq_rel);
    m_liveCard = nextCard;
    m_seq.fetch_add(1, std::memory_order_release);
    m_statusState.store(0, std::memory_order_release);
    m_phaseStartedAtMs = millis();
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
    return true;
}

bool MediaShowcaseBase::runMqttUdpTurn(const ConfigSlot& cfg, const char* domain, const char* reqLang) {
#if defined(ESP32) && !defined(UNIT_TEST)
    char mqttHost[64] = "";
    uint16_t mqttPort = 1883;
    if (!resolveMqttBroker(cfg, mqttHost, sizeof(mqttHost), mqttPort)) {
        return false;
    }

    WiFiClient mqttCli;
    mqttCli.setTimeout(2);
    if (!mqttCli.connect(mqttHost, mqttPort, 1500)) {
        return false;
    }

    if (!sendMqttConnectPacket(mqttCli, m_sessionId)) {
        mqttCli.stop();
        return false;
    }

    // Wait briefly for CONNACK (0x20)
    uint32_t connWait = millis();
    while (mqttCli.available() < 4 && (millis() - connWait) < 1000) {
        if (!m_wantActive.load(std::memory_order_acquire)) {
            mqttCli.stop();
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (mqttCli.available() >= 4) {
        uint8_t connAck[4];
        mqttCli.readBytes(connAck, 4);
        if (connAck[0] != 0x20 || connAck[3] != 0x00) {
            mqttCli.stop();
            return false;
        }
    } else {
        mqttCli.stop();
        return false;
    }

    uint16_t localUdpPort = static_cast<uint16_t>(5568U + static_cast<uint16_t>(m_domain));
    WiFiUDP udp;
    udp.begin(localUdpPort);

    char statusTopic[48] = "dmd_showcase/server/status";
    char readyTopic[96];
    char eventsTopic[96];
    char configTopic[96];
    snprintf(readyTopic, sizeof(readyTopic), "dmd_showcase/clients/%s/%s/ready", m_sessionId, domain);
    snprintf(eventsTopic, sizeof(eventsTopic), "dmd_showcase/clients/%s/%s/events", m_sessionId, domain);
    snprintf(configTopic, sizeof(configTopic), "dmd_showcase/clients/%s/config", m_sessionId);

    String localIp = WiFi.localIP().toString();
    String cfgPayload;
    cfgPayload.reserve(640);
    cfgPayload += "{\"dmd_id\":\"";
    cfgPayload += m_sessionId;
    cfgPayload += "\",\"client_ip\":\"";
    cfgPayload += localIp;
    cfgPayload += "\",\"udp_ip\":\"";
    cfgPayload += localIp;
    cfgPayload += "\",\"width\":";
    cfgPayload += String(m_width);
    cfgPayload += ",\"height\":";
    cfgPayload += String(m_height);
    cfgPayload += ",\"udp_port\":";
    cfgPayload += String(localUdpPort);
    cfgPayload += ",\"lang\":\"";
    cfgPayload += reqLang;
    cfgPayload += "\",\"domains\":{\"";
    cfgPayload += domain;
    cfgPayload += "\":{\"systems\":";
    appendCsvAsJsonArray(cfgPayload, cfg.systems);
    cfgPayload += ",\"genres\":";
    appendCsvAsJsonArray(cfgPayload, cfg.genres);
    cfgPayload += ",\"decades\":";
    appendCsvAsJsonArray(cfgPayload, cfg.decades);
    cfgPayload += ",\"video_type\":\"";
    cfgPayload += cfg.videoType;
    cfgPayload += "\",\"show_poster\":";
    cfgPayload += (cfg.showPoster ? "true" : "false");
    cfgPayload += ",\"show_synopsis\":";
    cfgPayload += (cfg.showSynopsis ? "true" : "false");
    cfgPayload += ",\"text_scroll_speed\":\"";
    cfgPayload += cfg.textScrollSpeed;
    cfgPayload += "\",\"jacket_panorama\":";
    cfgPayload += (cfg.jacketPanorama ? "true" : "false");
    cfgPayload += ",\"marquee_panorama\":";
    cfgPayload += (cfg.marqueePanorama ? "true" : "false");
    cfgPayload += ",\"snapshot_panorama_count\":";
    cfgPayload += String(cfg.snapshotPanoramaCount);
    cfgPayload += ",\"show_video\":";
    cfgPayload += (cfg.showVideo ? "true" : "false");
    cfgPayload += ",\"video_max_duration_s\":";
    cfgPayload += String(cfg.videoMaxDurationS);
    cfgPayload += ",\"video_fps\":";
    cfgPayload += String(cfg.videoFps);
    cfgPayload += ",\"video_mode\":\"";
    cfgPayload += cfg.videoMode;
    cfgPayload += "\",\"color_boost\":";
    cfgPayload += (cfg.colorBoost ? "true" : "false");
    cfgPayload += "}}}";

    // Publish retained client config FIRST so the Showcase orchestrator can clean up
    // any mid-broadcast pre-reboot slot and re-publish `ready: true` immediately.
    sendMqttPublishPacket(mqttCli, configTopic, cfgPayload.c_str(), true);
    sendMqttSubscribePacket(mqttCli, 1, statusTopic);
    sendMqttSubscribePacket(mqttCli, 2, readyTopic);
    sendMqttSubscribePacket(mqttCli, 3, eventsTopic);

    // Wait up to 1800ms for retained `ready` message
    uint32_t waitReadyStart = millis();
    bool gotReadyItem = false;
    bool sawReadyFalse = false;
    uint32_t readyFalseAtMs = 0;
    String itemIdStr;
    bool itemHasStream = false;
    uint32_t streamMaxMs = static_cast<uint32_t>(cfg.videoMaxDurationS + 10) * 1000U;

    while (m_wantActive.load(std::memory_order_acquire) && (millis() - waitReadyStart) < 1800U && !gotReadyItem) {
        if (sawReadyFalse && (millis() - readyFalseAtMs) >= 600U) {
            // Allow 600ms after an initial `ready: false` in case the server is re-publishing
            // `ready: true` in response to our `config` message after a mid-broadcast reboot.
            break;
        }
        if (mqttCli.available() >= 2) {
            uint8_t fixedHdr = static_cast<uint8_t>(mqttCli.read());
            uint32_t remLen = 0;
            uint32_t mult = 1;
            for (int i = 0; i < 4; ++i) {
                uint8_t b = 0;
                if (!readExactFromStream(mqttCli, &b, 1, m_wantActive, 1000)) break;
                remLen += static_cast<uint32_t>(b & 0x7F) * mult;
                mult *= 128;
                if ((b & 0x80) == 0) break;
            }
            // Non-PUBLISH packets (e.g. SUBACK 0x90, PINGRESP 0xD0) or oversized packets
            // MUST drain their `remLen` payload bytes to preserve MQTT TCP stream framing!
            if ((fixedHdr & 0xF0) != 0x30 || remLen < 2 || remLen >= 14336) {
                if (remLen > 0) {
                    drainFromStream(mqttCli, remLen, m_wantActive, 1500);
                }
                continue;
            }

            uint8_t tLenBe[2];
            if (!readExactFromStream(mqttCli, tLenBe, 2, m_wantActive, 1000)) {
                break;
            }
            uint16_t tLen = static_cast<uint16_t>((tLenBe[0] << 8) | tLenBe[1]);
            if (static_cast<uint32_t>(tLen) + 2U > remLen) {
                drainFromStream(mqttCli, remLen - 2U, m_wantActive, 1000);
                continue;
            }
            char topicBuf[128] = "";
            size_t toReadT = (tLen < sizeof(topicBuf) - 1) ? tLen : (sizeof(topicBuf) - 1);
            if (toReadT > 0) {
                readExactFromStream(mqttCli, reinterpret_cast<uint8_t*>(topicBuf), toReadT, m_wantActive, 1000);
            }
            topicBuf[toReadT] = '\0';
            if (tLen > toReadT) {
                drainFromStream(mqttCli, tLen - toReadT, m_wantActive, 1000);
            }
            size_t payloadLen = remLen - 2U - static_cast<size_t>(tLen);
            char* payloadBuf = nullptr;
            if (m_hasPsram) {
                payloadBuf = static_cast<char*>(heap_caps_malloc(payloadLen + 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            }
            if (!payloadBuf) {
                payloadBuf = static_cast<char*>(malloc(payloadLen + 1U));
            }
            if (!payloadBuf) {
                drainFromStream(mqttCli, payloadLen, m_wantActive, 1500);
                continue;
            }
            if (!readExactFromStream(mqttCli, reinterpret_cast<uint8_t*>(payloadBuf), payloadLen, m_wantActive, 2000)) {
                free(payloadBuf);
                break;
            }
            payloadBuf[payloadLen] = '\0';

            if (strcmp(topicBuf, statusTopic) == 0) {
                DynamicJsonDocument doc(256);
                if (deserializeJson(doc, payloadBuf) == DeserializationError::Ok) {
                    bool online = doc["online"] | false;
                    if (!online) {
                        free(payloadBuf);
                        m_statusState.store(2, std::memory_order_release);
                        clearLiveCardState();
                        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                            m_skipTurnImmediately.store(true, std::memory_order_release);
                        }
                        udp.stop();
                        mqttCli.stop();
                        return true;
                    }
                }
                free(payloadBuf);
            } else if (strcmp(topicBuf, readyTopic) == 0) {
                bool explicitWarming = false;
                if (parseReadyPayloadInPlace(payloadBuf, cfg.showPoster, itemIdStr, itemHasStream, streamMaxMs, explicitWarming)) {
                    gotReadyItem = true;
                } else if (explicitWarming) {
                    m_statusState.store(1, std::memory_order_release);
                    if (!sawReadyFalse) {
                        sawReadyFalse = true;
                        readyFalseAtMs = millis();
                    }
                }
                free(payloadBuf);
            } else {
                free(payloadBuf);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }

    if (!gotReadyItem) {
        udp.stop();
        mqttCli.stop();
        if (sawReadyFalse) {
            m_statusState.store(1, std::memory_order_release);
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
            return true;
        }
        return false;
    }

    // Consume-Once: immediately notify server that Card display started (`start_card`)
    String startCardEv = String("{\"action\":\"start_card\",\"item_id\":\"") + itemIdStr + "\",\"client_ip\":\"" + localIp + "\",\"udp_ip\":\"" + localIp + "\"}";
    sendMqttPublishPacket(mqttCli, eventsTopic, startCardEv.c_str(), false);

    // Phase 1: Wait for organic full-synopsis scroll on the Card
    uint32_t synChars = static_cast<uint32_t>(strlen(m_liveCard.synopsis));
    uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
    uint32_t cardWaitMs = 4500U;
    if (cfg.showSynopsis && synChars > 0) {
        uint32_t scrollDistPx = synChars * 6U + 64U;
        uint32_t synScrollMs = 900U + (scrollDistPx * 1000U) / speedPx + 700U;
        if (synScrollMs > cardWaitMs) cardWaitMs = synScrollMs;
    }
    uint32_t cardT0 = millis();
    while (m_wantActive.load(std::memory_order_acquire) && (millis() - cardT0) < cardWaitMs) {
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    // Phase 2 & 3: Request real-time `ffmpeg -re` UDP stream (`start_stream`) and reassemble `DMDF` chunks
    if (m_wantActive.load(std::memory_order_acquire) && itemHasStream && m_frame565) {
        m_phase.store(static_cast<uint8_t>(Phase::LiveStream), std::memory_order_release);
        String startStreamEv = String("{\"action\":\"start_stream\",\"item_id\":\"") + itemIdStr + "\",\"udp_port\":" + String(localUdpPort) + ",\"client_ip\":\"" + localIp + "\",\"udp_ip\":\"" + localIp + "\"}";
        sendMqttPublishPacket(mqttCli, eventsTopic, startStreamEv.c_str(), false);

        uint32_t streamT0 = millis();
        uint32_t lastUdpPktMs = 0;
        uint16_t curFrameSeq = 0xFFFF;
        uint64_t chunkMask = 0;
        size_t maxBufBytes = m_framePixels * sizeof(uint16_t);
        uint8_t udpPkt[1040];

        while (m_wantActive.load(std::memory_order_acquire) && (millis() - streamT0) < streamMaxMs) {
            int pktSize = udp.parsePacket();
            if (pktSize > 10 && pktSize <= static_cast<int>(sizeof(udpPkt))) {
                int rLen = udp.read(udpPkt, sizeof(udpPkt));
                if (rLen > 10 && memcmp(udpPkt, "DMDF", 4) == 0) {
                    uint16_t seq = static_cast<uint16_t>(udpPkt[4] | (udpPkt[5] << 8));
                    uint8_t chunkIdx = udpPkt[6];
                    uint8_t totalChunks = udpPkt[7] > 0 ? udpPkt[7] : 1;
                    uint16_t byteOff = static_cast<uint16_t>(udpPkt[8] | (udpPkt[9] << 8));
                    size_t payloadBytes = static_cast<size_t>(rLen - 10);

                    if (seq != curFrameSeq) {
                        if (chunkMask != 0) {
                            m_seq.fetch_add(1, std::memory_order_acq_rel);
                            m_liveCard.hasVideoFrame = true;
                            m_seq.fetch_add(1, std::memory_order_release);
                        }
                        curFrameSeq = seq;
                        chunkMask = 0;
                    }
                    if (chunkIdx < totalChunks && totalChunks <= 64 && (static_cast<size_t>(byteOff) + payloadBytes) <= maxBufBytes) {
                        memcpy(reinterpret_cast<uint8_t*>(m_frame565) + byteOff, udpPkt + 10, payloadBytes);
                        chunkMask |= (1ULL << chunkIdx);
                        uint64_t expectedMask = (totalChunks == 64) ? ~0ULL : ((1ULL << totalChunks) - 1ULL);
                        if (chunkMask == expectedMask) {
                            chunkMask = 0;
                            m_seq.fetch_add(1, std::memory_order_acq_rel);
                            m_liveCard.hasVideoFrame = true;
                            m_seq.fetch_add(1, std::memory_order_release);
                        }
                        lastUdpPktMs = millis();
                    }
                }
            } else {
                uint32_t nowMs = millis();
                if (lastUdpPktMs > 0 && (nowMs - lastUdpPktMs) > 1500U) {
                    break; // Stream completed or server stopped
                }
                if (lastUdpPktMs == 0 && (nowMs - streamT0) > 850U) {
                    // Docker Desktop / Rancher Desktop VM NAT blocked container-to-LAN UDP:
                    // seamlessly fall back to streaming the unified `.rgb565.stream` (`DMDS`) over HTTP TCP!
                    String fallbackPath = String("/api/v1/") + domain + "/stream/" + itemIdStr + ".rgb565.stream?purge=false";
                    streamUnifiedRgb565Http(cfg.serverUrl, fallbackPath.c_str());
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
    }

    if (itemIdStr.length() > 0) {
        String stopEv = String("{\"action\":\"stop_stream\",\"item_id\":\"") + itemIdStr + "\"}";
        sendMqttPublishPacket(mqttCli, eventsTopic, stopEv.c_str(), false);
    }

    udp.stop();
    mqttCli.stop();
    clearLiveCardState();
    m_turnDone.store(true, std::memory_order_release);
    return true;
#else
    (void)cfg;
    (void)domain;
    (void)reqLang;
    return false;
#endif
}

void MediaShowcaseBase::runHttpPollTurn(const ConfigSlot& cfg, const char* domain, const char* reqLang) {
    net::SecureHttpOptions httpOpts;
    httpOpts.ownerId = net::OWNER_SHOWCASE;
    httpOpts.requestTimeoutMs = 3500;
    httpOpts.keepAlive = false;

    uint16_t pw = 32;
    uint16_t ph = 32;
    computeCardPosterDims(m_width, m_height, pw, ph);

    // 1. Register client config (`POST /api/v1/{domain}/config`)
    String cfgUrl = String(cfg.serverUrl) + "/api/v1/" + domain + "/config";
    String postBody;
    postBody.reserve(640);
    postBody += "{\"client_id\":\"";
    postBody += m_sessionId;
    postBody += "\",\"width\":";
    postBody += String(m_width);
    postBody += ",\"height\":";
    postBody += String(m_height);
    postBody += ",\"poster_width\":";
    postBody += String(pw);
    postBody += ",\"poster_height\":";
    postBody += String(ph);
    postBody += ",\"systems\":";
    appendCsvAsJsonArray(postBody, cfg.systems);
    postBody += ",\"genres\":";
    appendCsvAsJsonArray(postBody, cfg.genres);
    postBody += ",\"decades\":";
    appendCsvAsJsonArray(postBody, cfg.decades);
    postBody += ",\"video_type\":\"";
    postBody += cfg.videoType;
    postBody += "\",\"include_poster\":";
    postBody += (cfg.showPoster ? "true" : "false");
    postBody += ",\"include_synopsis\":";
    postBody += (cfg.showSynopsis ? "true" : "false");
    postBody += ",\"text_scroll_speed\":\"";
    postBody += cfg.textScrollSpeed;
    postBody += "\",\"jacket_panorama\":";
    postBody += (cfg.jacketPanorama ? "true" : "false");
    postBody += ",\"marquee_panorama\":";
    postBody += (cfg.marqueePanorama ? "true" : "false");
    postBody += ",\"snapshot_panorama_count\":";
    postBody += String(cfg.snapshotPanoramaCount);
    postBody += ",\"show_video\":";
    postBody += (cfg.showVideo ? "true" : "false");
    postBody += ",\"video_fps\":";
    postBody += String(cfg.videoFps);
    postBody += ",\"video_max_duration_s\":";
    postBody += String(cfg.videoMaxDurationS);
    postBody += ",\"video_mode\":\"";
    postBody += cfg.videoMode;
    postBody += "\",\"color_boost\":";
    postBody += (cfg.colorBoost ? "true" : "false");
    postBody += ",\"lang\":\"";
    postBody += reqLang;
    postBody += "\"}";

    {
        auto cfgResp = net::SecureHttpClient::post(cfgUrl, "application/json", postBody, httpOpts);
        if (!cfgResp.ok()) {
            m_statusState.store(2, std::memory_order_release); // NetworkOffline
            clearLiveCardState();
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
            return;
        }
    }

    if (!m_wantActive.load(std::memory_order_acquire)) return;

    // 2. Poll single Consume-Once item (`GET /api/v1/{domain}/next?client_id=...`)
    String nextUrl = String(cfg.serverUrl) + "/api/v1/" + domain + "/next?client_id=" + m_sessionId;
    auto nextResp = net::SecureHttpClient::get(nextUrl, httpOpts);
    if (!nextResp.ok()) {
        m_statusState.store(2, std::memory_order_release);
        clearLiveCardState();
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
        }
        return;
    }

    DynamicJsonDocument nextDoc(m_hasPsram ? 14336 : 6144);
    if (deserializeJson(nextDoc, nextResp.stream()) != DeserializationError::Ok) {
        m_statusState.store(2, std::memory_order_release);
        clearLiveCardState();
        return;
    }

    const char* status = nextDoc["status"] | "warming";
    JsonObject item = nextDoc["item"].as<JsonObject>();
    if (strcmp(status, "ready") != 0 || item.isNull()) {
        m_statusState.store(1, std::memory_order_release); // ServerWarming
        clearLiveCardState();
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
        }
        return;
    }

    CardSnapshot nextCard;
    copySafeStr(nextCard.title, sizeof(nextCard.title), item["title"] | "UNKNOWN");
    copySafeStr(nextCard.year, sizeof(nextCard.year), item["year"] | "");
    const char* badge = item["badge"] | domain;
    copySafeStr(nextCard.badge, sizeof(nextCard.badge), badge);
    copySafeStr(nextCard.rating, sizeof(nextCard.rating), item["rating"] | "");
    copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), item["synopsis"] | "");
    nextCard.badgeColor565 = parseHexColor565(item["badge_color"] | nullptr, defaultAccent565());
    nextCard.posterW = pw;
    nextCard.posterH = ph;
    nextCard.hasData = true;
    nextCard.hasPoster = false;
    nextCard.hasVideoFrame = false;

    JsonObject assetsObj = item["assets"].as<JsonObject>();
    const char* posterB64 = item["poster_rgb565_b64"] | "";
    const char* posterPath = assetsObj["poster_rgb565"] | (item["poster_rgb565_url"] | "");
    size_t expectedPosterBytes = static_cast<size_t>(pw) * static_cast<size_t>(ph) * 2U;
    if (cfg.showPoster && m_poster565 && expectedPosterBytes <= (48U * 64U * 2U)) {
        if (posterB64[0] != '\0' && decodeBase64Fixed(posterB64, reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes)) {
            nextCard.hasPoster = true;
        } else if (posterPath[0] != '\0') {
            String posterUrl = (strncmp(posterPath, "http", 4) == 0)
                ? String(posterPath)
                : (String(cfg.serverUrl) + posterPath);
            auto pResp = net::SecureHttpClient::get(posterUrl, httpOpts);
            if (pResp.ok() && readExactFromStream(pResp.stream(), reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes, m_wantActive)) {
                nextCard.hasPoster = true;
            }
        }
    }

    m_seq.fetch_add(1, std::memory_order_acq_rel);
    m_liveCard = nextCard;
    m_seq.fetch_add(1, std::memory_order_release);
    m_statusState.store(0, std::memory_order_release);
    m_phaseStartedAtMs = millis();
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

    // Phase 1: Organic Rich Card dwell
    uint32_t synChars = static_cast<uint32_t>(strlen(nextCard.synopsis));
    uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
    uint32_t cardWaitMs = 4500U;
    if (cfg.showSynopsis && synChars > 0) {
        uint32_t scrollDistPx = synChars * 6U + 64U;
        uint32_t synScrollMs = 900U + (scrollDistPx * 1000U) / speedPx + 700U;
        if (synScrollMs > cardWaitMs) cardWaitMs = synScrollMs;
    }
    uint32_t t0 = millis();
    while (m_wantActive.load(std::memory_order_acquire) && (millis() - t0) < cardWaitMs) {
#if defined(ESP32)
        vTaskDelay(pdMS_TO_TICKS(40));
#else
        break;
#endif
    }

    // Phase 2 & 3: Stream unified `.rgb565.stream` (`DMDS`) into `m_frame565`
    const char* unifiedStreamPath = assetsObj["unified_stream_rgb565"] | "";
    if (m_wantActive.load(std::memory_order_acquire) && m_frame565 && unifiedStreamPath[0] != '\0') {
        streamUnifiedRgb565Http(cfg.serverUrl, unifiedStreamPath);
    }

    clearLiveCardState();
    m_turnDone.store(true, std::memory_order_release);
}

bool MediaShowcaseBase::streamUnifiedRgb565Http(const char* serverUrl, const char* streamPath) {
    if (!serverUrl || !streamPath || streamPath[0] == '\0' || !m_frame565) {
        return false;
    }
    net::SecureHttpOptions httpOpts;
    httpOpts.ownerId = net::OWNER_SHOWCASE;
    httpOpts.requestTimeoutMs = 4500;
    httpOpts.keepAlive = false;

    String fullUrl = (strncmp(streamPath, "http", 4) == 0)
        ? String(streamPath)
        : (String(serverUrl) + streamPath);
    auto sResp = net::SecureHttpClient::get(fullUrl, httpOpts);
    if (!sResp.ok()) {
        return false;
    }

    Stream& st = sResp.stream();
    uint8_t hdr[16];
    if (!readExactFromStream(st, hdr, sizeof(hdr), m_wantActive) || memcmp(hdr, "DMDS", 4) != 0) {
        return false;
    }
    uint8_t fps = hdr[5] > 0 ? hdr[5] : 15;
    uint16_t w = static_cast<uint16_t>(hdr[6] | (hdr[7] << 8));
    uint16_t h = static_cast<uint16_t>(hdr[8] | (hdr[9] << 8));
    uint16_t totalFrames = static_cast<uint16_t>(hdr[10] | (hdr[11] << 8));
    uint16_t delayMs = static_cast<uint16_t>(hdr[12] | (hdr[13] << 8));
    uint32_t frameDelayMs = (delayMs > 0) ? delayMs : (1000U / fps);
    size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2U;
    size_t maxBufBytes = m_framePixels * sizeof(uint16_t);
    if (frameBytes == 0 || frameBytes > maxBufBytes || totalFrames == 0) {
        return false;
    }

    m_phase.store(static_cast<uint8_t>(Phase::LiveStream), std::memory_order_release);
    for (uint16_t f = 0; f < totalFrames; ++f) {
        if (!m_wantActive.load(std::memory_order_acquire)) break;
        uint32_t fStart = millis();
        if (!readExactFromStream(st, reinterpret_cast<uint8_t*>(m_frame565), frameBytes, m_wantActive)) {
            break;
        }
        m_seq.fetch_add(1, std::memory_order_acq_rel);
        m_liveCard.hasVideoFrame = true;
        m_seq.fetch_add(1, std::memory_order_release);
#if defined(ESP32)
        uint32_t took = millis() - fStart;
        if (took < frameDelayMs) {
            vTaskDelay(pdMS_TO_TICKS(frameDelayMs - took));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
#endif
    }
    return true;
}

void MediaShowcaseBase::update(EngineContext* context) {
    (void)context;
    // Zero-copy RAII snapshot read on Core 1 (Invariants 1 & 2)
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const auto& snap = guard.get();
    m_rotationSlotCount.store(static_cast<uint8_t>(snap.rotation.size()), std::memory_order_relaxed);

    // Lock-free seqlock check for updated CardSnapshot from Core 0 worker
    uint32_t s1 = m_seq.load(std::memory_order_acquire);
    if ((s1 & 1U) == 0U) {
        CardSnapshot temp = m_liveCard;
        uint32_t s2 = m_seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            m_renderCard = temp;
            m_dirty = true;
        }
    }

    uint32_t now = millis();
    if (now - m_lastScrollStepMs >= 33) {
        m_lastScrollStepMs = now;
        m_dirty = true;
    }
}

void MediaShowcaseBase::drawScrollingLine(
    IDrawingSurface* s,
    const char* text,
    int minX,
    int maxX,
    int y,
    uint16_t color,
    uint32_t elapsedMs
) {
    if (!s || !text || text[0] == '\0') return;
    int availW = maxX - minX;
    int textW = static_cast<int>(strlen(text)) * 6;

    s->setTextSize(1);
    s->setTextWrap(false);
    s->setTextColor(color);

    if (textW <= availW) {
        s->setCursor(minX, y);
        s->print(text);
        return;
    }

    int totalScroll = textW - availW + 18;
    int step = static_cast<int>((elapsedMs / 45U) % static_cast<uint32_t>(totalScroll + 20));
    int offset = std::max(0, step - 10);
    s->setCursor(minX - offset, y);
    s->print(text);
}

void MediaShowcaseBase::drawSinglePassSynopsis(
    IDrawingSurface* s,
    const char* text,
    int minX,
    int maxX,
    int y,
    uint16_t color,
    uint32_t elapsedMs,
    uint32_t speedPxPerSec
) {
    if (!s || !text || text[0] == '\0') return;
    int availW = maxX - minX;
    int textW = static_cast<int>(strlen(text)) * 6;

    s->setTextSize(1);
    s->setTextWrap(false);
    s->setTextColor(color);

    if (textW <= availW) {
        s->setCursor(minX, y);
        s->print(text);
        return;
    }

    uint32_t activeScrollMs = (elapsedMs > 900U) ? (elapsedMs - 900U) : 0U;
    int scrollPx = static_cast<int>((activeScrollMs * speedPxPerSec) / 1000U);
    s->setCursor((minX + 6) - scrollPx, y);
    s->print(text);
}

void MediaShowcaseBase::drawWaitingPixelArt(IDrawingSurface* s, int mW, int mH, uint32_t elapsedMs) {
    uint32_t frameStep = elapsedMs / 180U;
    bool blinkOn = ((elapsedMs / 450U) % 2U) == 0U;
    uint16_t accent = defaultAccent565();

    switch (m_domain) {
        case ShowcaseDomainId::Games: {
            // 1. Arcade Attract-Mode: Parallax Starfield + Walking 11x8 Space Invader + Pellets
            uint16_t starBright = s->color565(90, 125, 190);
            uint16_t starDim = s->color565(45, 65, 110);
            for (int i = 0; i < 12; ++i) {
                int sx = static_cast<int>((i * 23U + elapsedMs / (20U + (i % 3) * 10U)) % std::max(1, mW));
                int sy = 10 + ((i * 11) % std::max(1, mH - 12));
                s->drawPixel(sx, sy, (i & 1) ? starBright : starDim);
            }
            static const uint16_t kInvaderF0[8] = {
                0b00100000100, 0b00010001000, 0b00111111100, 0b01101110110,
                0b11111111111, 0b10111111101, 0b10100000101, 0b00011011000
            };
            static const uint16_t kInvaderF1[8] = {
                0b00100000100, 0b10010001001, 0b10111111101, 0b11101110111,
                0b11111111111, 0b01111111110, 0b00100000100, 0b01000000010
            };
            const uint16_t* sprite = ((frameStep & 1U) == 0U) ? kInvaderF0 : kInvaderF1;
            int invY = (mH >= 64) ? (mH / 2 - 10) : 11;
            int invX = 3 + static_cast<int>((elapsedMs / 40U) % std::max(8, mW - 18));
            uint16_t invCol = s->color565(0, 255, 128);
            for (int ry = 0; ry < 8; ++ry) {
                uint16_t bits = sprite[ry];
                for (int rx = 0; rx < 11; ++rx) {
                    if ((bits >> (10 - rx)) & 1U) {
                        s->drawPixel(invX + rx, invY + ry, invCol);
                    }
                }
            }
            for (int p = 1; p <= 3; ++p) {
                int px = (invX + 14 + p * 8) % std::max(4, mW - 2);
                s->fillRect(px, invY + 3, 2, 2, 0xFFE0);
            }
            break;
        }
        case ShowcaseDomainId::Movies: {
            // 2. 35mm Cinema Projector: Perforated film strips + 2 spinning reels + light beam
            int holeOff = static_cast<int>((elapsedMs / 60U) % 6U);
            uint16_t stripCol = s->color565(70, 75, 95);
            uint16_t goldCol = s->color565(225, 190, 90);
            s->drawFastHLine(0, 0, mW, stripCol);
            s->drawFastHLine(0, 3, mW, stripCol);
            s->drawFastHLine(0, mH - 4, mW, stripCol);
            s->drawFastHLine(0, mH - 1, mW, stripCol);
            for (int hx = -holeOff; hx < mW; hx += 6) {
                s->fillRect(hx, 1, 3, 2, goldCol);
                s->fillRect(hx, mH - 3, 3, 2, goldCol);
            }
            int cy = mH / 2;
            int rRad = (mH >= 64) ? 6 : 4;
            int r1X = 8;
            int r2X = 8 + rRad * 2 + 2;
            s->drawCircle(r1X, cy - 1, rRad, accent);
            s->drawCircle(r2X, cy - 1, rRad, accent);
            int spoke = static_cast<int>(frameStep % 4U);
            int dx = (spoke == 0) ? (rRad - 1) : (spoke == 2) ? 0 : (spoke == 1 ? (rRad - 2) : -(rRad - 2));
            int dy = (spoke == 0) ? 0 : (rRad - 2);
            s->drawLine(r1X - dx, cy - 1 - dy, r1X + dx, cy - 1 + dy, 0xFFFF);
            s->drawLine(r2X - dx, cy - 1 - dy, r2X + dx, cy - 1 + dy, 0xFFFF);
            int lensX = r2X + rRad + 2;
            uint16_t beamCol = ((elapsedMs / 90U) % 3U == 0U) ? s->color565(190, 165, 55) : s->color565(255, 215, 80);
            s->drawLine(lensX, cy, mW - 4, 6, beamCol);
            s->drawLine(lensX, cy, mW - 4, mH - 6, beamCol);
            break;
        }
        case ShowcaseDomainId::Series: {
            // 3. Retro CRT Television + Rabbit-Ear Antennae + Concentric RF Waves + Scanline
            int tvW = (mW >= 128) ? 20 : 14;
            int tvH = (mH >= 64) ? 14 : 11;
            int tvX = 5;
            int tvY = (mH / 2) - (tvH / 2) + 3;
            s->drawRoundRect(tvX, tvY, tvW, tvH, 2, accent);
            int antCx = tvX + tvW / 2;
            s->drawLine(antCx, tvY, antCx - 4, tvY - 4, 0xCE79);
            s->drawLine(antCx, tvY, antCx + 4, tvY - 4, 0xCE79);
            int waveR = 2 + static_cast<int>((elapsedMs / 140U) % 5U);
            s->drawCircle(antCx, tvY - 4, waveR, 0x07FF);
            int innerW = std::max(4, tvW - 4);
            int innerH = std::max(3, tvH - 4);
            static const uint16_t kBars[4] = {0xFFE0, 0x07FF, 0x07E0, 0xF81F};
            for (int b = 0; b < 4; ++b) {
                int bx0 = tvX + 2 + (b * innerW) / 4;
                int bx1 = tvX + 2 + ((b + 1) * innerW) / 4;
                s->fillRect(bx0, tvY + 2, std::max(1, bx1 - bx0), innerH, kBars[b]);
            }
            int scanY = tvY + 2 + static_cast<int>((elapsedMs / 70U) % innerH);
            s->drawFastHLine(tvX + 2, scanY, innerW, 0xFFFF);
            break;
        }
        case ShowcaseDomainId::Anime:
        default: {
            // 4. Anime Shuriken / Mecha Energy Core + Speed-Lines + Floating Sakura Petals
            uint16_t lineCol = s->color565(75, 30, 95);
            for (int i = 0; i < 6; ++i) {
                int ly = 9 + (i * std::max(6, mH - 12)) / 6;
                int len = 8 + (i * 5) % 12;
                int lx = std::max(0, mW - static_cast<int>((elapsedMs / 14U + i * 19U) % (mW + len)) - len);
                s->drawFastHLine(lx, ly, std::min(len, mW - lx), lineCol);
            }
            int sx = 12;
            int sy = (mH / 2) + 2;
            int arm = (mH >= 64) ? 6 : 4;
            if ((frameStep & 1U) == 0U) {
                s->drawFastHLine(sx - arm, sy, arm * 2 + 1, 0x07FF);
                s->drawFastVLine(sx, sy - arm, arm * 2 + 1, 0x07FF);
                s->drawLine(sx - 2, sy - 2, sx + 2, sy + 2, accent);
                s->drawLine(sx - 2, sy + 2, sx + 2, sy - 2, accent);
            } else {
                s->drawLine(sx - arm, sy - arm, sx + arm, sy + arm, 0x07FF);
                s->drawLine(sx - arm, sy + arm, sx + arm, sy - arm, 0x07FF);
                s->drawFastHLine(sx - 2, sy, 5, accent);
                s->drawFastVLine(sx, sy - 2, 5, accent);
            }
            s->drawPixel(sx, sy, 0xFFFF);
            uint16_t sakuraCol = s->color565(255, 130, 190);
            for (int p = 0; p < 4; ++p) {
                int px = static_cast<int>((26U + p * 19U + elapsedMs / 45U) % std::max(1, mW));
                int py = 10 + static_cast<int>((p * 7U + elapsedMs / 85U) % std::max(1, mH - 12));
                s->fillRect(px, py, 2, 2, sakuraCol);
            }
            break;
        }
    }

    // Header badge + localized waiting message
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    Lang sysLang = I18n::parseLang(guard.get().system.lang.c_str());
    uint8_t st = m_statusState.load(std::memory_order_relaxed);
    const char* statusText = (st == 3)
        ? I18n::getShowcaseUnconfiguredLabel(sysLang)
        : (st == 1)
            ? I18n::getShowcaseWarmingLabel(sysLang)
            : I18n::getShowcaseOfflineLabel(sysLang);

    s->setTextSize(1);
    s->setTextColor(accent);
    s->setCursor(28, 1);
    s->print(domainBadgeLabel());

    if (blinkOn) {
        int textY = std::max(12, (mH / 2) + 1);
        drawScrollingLine(s, statusText, 28, mW - 2, textY, 0xFFFF, elapsedMs);
    }
}

void MediaShowcaseBase::drawCardPhase(IDrawingSurface* s, const CardSnapshot& card, uint32_t elapsedMs) {
    int mW = s->width();
    int mH = s->height();
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];

    // If no live item is active, render the domain-themed Pixel-Art waiting animation
    if (!card.hasData) {
        drawWaitingPixelArt(s, mW, mH, elapsedMs);
        return;
    }

    uint32_t cardElapsedMs = (millis() >= m_phaseStartedAtMs) ? (millis() - m_phaseStartedAtMs) : elapsedMs;
    uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);

    // 1. Vertical TATE Mode (e.g. 64x128, 64x256, 32x128)
    if (mH > mW) {
        s->fillRect(0, 0, mW, 9, 0x0000);
        s->setTextSize(1);
        s->setTextColor(card.badgeColor565);
        s->setCursor(2, 1);
        s->print(card.badge);
        if (card.rating[0] != '\0') {
            int rw = static_cast<int>(strlen(card.rating)) * 6;
            s->setTextColor(0xFFE0);
            s->setCursor(std::max(2, mW - rw - 2), 1);
            s->print(card.rating);
        }
        int posterBottom = 11;
        if (cfg.showPoster) {
            int pw = static_cast<int>(card.posterW);
            int ph = static_cast<int>(card.posterH);
            int px = std::max(0, (mW - pw) / 2);
            int py = 11;
            if (card.hasPoster && m_poster565) {
                s->blit565(m_poster565, px, py, pw, ph);
            } else {
                s->drawRect(px, py, pw, ph, card.badgeColor565);
            }
            posterBottom = py + ph + 3;
        }
        drawScrollingLine(s, card.title, 2, mW - 2, posterBottom, 0xFFFF, cardElapsedMs);
        int sepY = posterBottom + 10;
        if (sepY < mH - 10) {
            s->drawFastHLine(2, sepY, mW - 4, s->color565(35, 45, 65));
            if (cfg.showSynopsis && card.synopsis[0] != '\0') {
                drawSinglePassSynopsis(s, card.synopsis, 2, mW - 2, sepY + 3, 0xAD75, cardElapsedMs, speedPx);
            }
        }
        return;
    }

    // 2. Square Panel (e.g. 64x64)
    if (mW == mH) {
        int rightX = cfg.showPoster ? 34 : 2;
        if (cfg.showPoster) {
            if (card.hasPoster && m_poster565) {
                s->blit565(m_poster565, 0, 0, 32, 32);
            } else {
                s->drawRect(0, 0, 32, 32, card.badgeColor565);
            }
        }
        s->setTextSize(1);
        s->setTextColor(card.badgeColor565);
        s->setCursor(rightX, 2);
        s->print(card.badge);
        if (card.year[0] != '\0') {
            s->setTextColor(s->color565(160, 175, 195));
            s->setCursor(rightX, 12);
            s->print(card.year);
        }
        if (card.rating[0] != '\0') {
            s->setTextColor(0xFFE0);
            s->setCursor(rightX, 22);
            s->print(card.rating);
        }
        drawScrollingLine(s, card.title, 2, mW - 2, 35, 0xFFFF, cardElapsedMs);
        s->drawFastHLine(2, 45, mW - 4, s->color565(35, 45, 65));
        if (cfg.showSynopsis && card.synopsis[0] != '\0') {
            drawSinglePassSynopsis(s, card.synopsis, 2, mW - 2, 49, 0xAD75, cardElapsedMs, speedPx);
        }
        return;
    }

    // 3. Tall / XL Landscape (e.g. 128x64, 256x64) -> 48x64 mini-jacket + spacious metadata
    if (mH >= 64) {
        int textX = cfg.showPoster ? (static_cast<int>(card.posterW) + 3) : 3;
        drawScrollingLine(s, card.title, textX, mW - 2, 13, 0xFFFF, cardElapsedMs);
        s->drawFastHLine(textX, 34, mW - textX - 2, s->color565(35, 45, 65));
        if (cfg.showSynopsis && card.synopsis[0] != '\0') {
            drawSinglePassSynopsis(s, card.synopsis, textX, mW - 2, 42, 0xAD75, cardElapsedMs, speedPx);
        }
        if (cfg.showPoster) {
            s->fillRect(0, 0, static_cast<int>(card.posterW) + 2, mH, 0x0000);
            if (card.hasPoster && m_poster565) {
                s->blit565(m_poster565, 0, 0, card.posterW, card.posterH);
            } else {
                s->drawRect(0, 0, card.posterW, card.posterH, card.badgeColor565);
            }
        }
        s->fillRect(textX, 0, mW - textX, 11, 0x0000);
        int curX = textX;
        if (card.badge[0] != '\0') {
            int bw = static_cast<int>(strlen(card.badge)) * 6 + 4;
            s->fillRoundRect(curX, 1, bw, 9, 2, s->color565(20, 24, 36));
            s->drawRoundRect(curX, 1, bw, 9, 2, card.badgeColor565);
            s->setTextSize(1);
            s->setTextColor(card.badgeColor565);
            s->setCursor(curX + 2, 2);
            s->print(card.badge);
            curX += bw + 4;
        }
        if (card.year[0] != '\0') {
            s->setTextColor(s->color565(160, 175, 195));
            s->setCursor(textX, 24);
            s->print(card.year);
        }
        if (card.rating[0] != '\0') {
            int rw = static_cast<int>(strlen(card.rating)) * 6;
            s->setTextColor(0xFFE0);
            s->setCursor(std::max(curX, mW - rw - 2), 2);
            s->print(card.rating);
        }
        return;
    }

    // 4. Standard Landscape (128x32)
    int textX = cfg.showPoster ? 34 : 3;

    // Middle Line: Title
    drawScrollingLine(s, card.title, textX, mW - 2, 11, 0xFFFF, cardElapsedMs);

    // Bottom Line: Complete single-pass synopsis
    if (cfg.showSynopsis && card.synopsis[0] != '\0') {
        drawSinglePassSynopsis(s, card.synopsis, textX, mW - 2, 22, 0xAD75, cardElapsedMs, speedPx);
    }

    // Occlude left poster region so scrolling text never bleeds into the 32x32 artwork
    if (cfg.showPoster) {
        s->fillRect(0, 0, 33, 32, 0x0000);
        if (card.hasPoster && m_poster565) {
            s->blit565(m_poster565, 0, 0, 32, 32);
        } else {
            s->fillRoundRect(0, 0, 31, 31, 3, s->color565(16, 20, 32));
            s->drawRoundRect(0, 0, 31, 31, 3, card.badgeColor565);
            s->setTextSize(1);
            s->setTextColor(card.badgeColor565);
            s->setCursor(4, 12);
            char shortBadge[5];
            copySafeStr(shortBadge, sizeof(shortBadge), card.badge);
            s->print(shortBadge);
        }
    }

    // Top Header Bar (Badge + Year + Rating)
    s->fillRect(textX, 0, mW - textX, 10, 0x0000);
    int curX = textX;
    if (card.badge[0] != '\0') {
        int bw = static_cast<int>(strlen(card.badge)) * 6 + 4;
        s->fillRoundRect(curX, 0, bw, 9, 2, s->color565(20, 24, 36));
        s->drawRoundRect(curX, 0, bw, 9, 2, card.badgeColor565);
        s->setTextSize(1);
        s->setTextColor(card.badgeColor565);
        s->setCursor(curX + 2, 1);
        s->print(card.badge);
        curX += bw + 3;
    }

    if (card.year[0] != '\0' && curX + 24 <= mW) {
        s->setTextColor(s->color565(160, 175, 195));
        s->setCursor(curX, 1);
        s->print(card.year);
        curX += static_cast<int>(strlen(card.year)) * 6 + 3;
    }

    if (card.rating[0] != '\0') {
        int rw = static_cast<int>(strlen(card.rating)) * 6;
        int rx = mW - rw - 2;
        if (rx >= curX) {
            s->setTextColor(0xFFE0);
            s->setCursor(rx, 1);
            s->print(card.rating);
        }
    }
}

void MediaShowcaseBase::render(EngineContext* context) {
    IDrawingSurface* s = (context && context->getSurface()) ? context->getSurface() : m_surface;
    if (!s) return;

    s->clear(0);
    uint32_t elapsedMs = millis() - m_activatedAtMs;
    Phase curPhase = static_cast<Phase>(m_phase.load(std::memory_order_acquire));

    if (curPhase == Phase::LiveStream) {
        if (m_renderCard.hasVideoFrame && m_frame565 != nullptr) {
            s->blit565(m_frame565, 0, 0, m_width, m_height);
        } else {
            drawWaitingPixelArt(s, s->width(), s->height(), elapsedMs);
        }
    } else {
        drawCardPhase(s, m_renderCard, elapsedMs);
    }

    m_dirty = false;
}

EngineDescriptor RetroGamesEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"retro_games", "Retro Games Showcase", "arcade", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 30;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 8000;
    desc.requirements.internalContiguousBytes = 12000;
    desc.requirements.psramBytes = 0;

    desc.schema.fields = {
        ConfigField("server_url", ConfigType::STRING, "Showcase Server URL", "HTTP base URL of the DMD Media Showcase server", "http://127.0.0.1:8080", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("transport_mode", ConfigType::ENUM, "Transport Protocol", "Real-Time MQTT + UDP (ffmpeg -re) or HTTP Polling fallback", "mqtt_udp", false, "", "", "", "mqtt_udp,http_poll", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("use_showcase_broker", ConfigType::BOOLEAN, "Use Showcase Embedded MQTT Broker", "Automatically connect to the Showcase server's MQTT broker (<server_ip>:1883) without extra setup", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("mqtt_broker_source", ConfigType::ENUM, "External MQTT Broker Source", "When Embedded Broker is disabled, choose ArcadeMatrix Global MQTT or a Custom Broker URL", "global", false, "", "", "", "global,custom", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("mqtt_custom_url", ConfigType::STRING, "Custom MQTT Broker URL", "Used when External MQTT Broker Source is set to Custom (e.g. mqtt://192.168.1.200:1883)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width cover/jacket from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Marquee / Banner Panorama", "Scroll the arcade marquee or title banner from top to bottom after the jacket", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional gameplay snapshots to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "pixel_art", false, "", "", "", "auto_action,pixel_art,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("systems", ConfigType::LIST, "Arcade & Console Systems", "Select one or more systems dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept),
        ConfigField("genres", ConfigType::LIST, "Game Genres", "Select one or more genres dynamically filtered by your chosen systems (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically filtered by your chosen systems (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new RetroGamesEngine()); };
    return desc;
}

EngineDescriptor MovieShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"movie_showcase", "Movie Showcase", "media", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 30;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 8000;
    desc.requirements.internalContiguousBytes = 12000;
    desc.requirements.psramBytes = 0;

    desc.schema.fields = {
        ConfigField("server_url", ConfigType::STRING, "Showcase Server URL", "HTTP base URL of the DMD Media Showcase server", "http://127.0.0.1:8080", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("transport_mode", ConfigType::ENUM, "Transport Protocol", "Real-Time MQTT + UDP (ffmpeg -re) or HTTP Polling fallback", "mqtt_udp", false, "", "", "", "mqtt_udp,http_poll", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("use_showcase_broker", ConfigType::BOOLEAN, "Use Showcase Embedded MQTT Broker", "Automatically connect to the Showcase server's MQTT broker (<server_ip>:1883) without extra setup", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("mqtt_broker_source", ConfigType::ENUM, "External MQTT Broker Source", "When Embedded Broker is disabled, choose ArcadeMatrix Global MQTT or a Custom Broker URL", "global", false, "", "", "", "global,custom", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("mqtt_custom_url", ConfigType::STRING, "Custom MQTT Broker URL", "Used when External MQTT Broker Source is set to Custom (e.g. mqtt://192.168.1.200:1883)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width movie poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the movie backdrop banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional movie stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "auto_action", false, "", "", "", "auto_action,cinema,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Movie Genres", "Select one or more movie genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/movies/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/movies/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (trailer,scene,any)", "any", false, "", "", "", "any,trailer,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MovieShowcaseEngine()); };
    return desc;
}

EngineDescriptor SeriesShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"series_showcase", "Series Showcase", "media", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 30;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 8000;
    desc.requirements.internalContiguousBytes = 12000;
    desc.requirements.psramBytes = 0;

    desc.schema.fields = {
        ConfigField("server_url", ConfigType::STRING, "Showcase Server URL", "HTTP base URL of the DMD Media Showcase server", "http://127.0.0.1:8080", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("transport_mode", ConfigType::ENUM, "Transport Protocol", "Real-Time MQTT + UDP (ffmpeg -re) or HTTP Polling fallback", "mqtt_udp", false, "", "", "", "mqtt_udp,http_poll", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("use_showcase_broker", ConfigType::BOOLEAN, "Use Showcase Embedded MQTT Broker", "Automatically connect to the Showcase server's MQTT broker (<server_ip>:1883) without extra setup", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("mqtt_broker_source", ConfigType::ENUM, "External MQTT Broker Source", "When Embedded Broker is disabled, choose ArcadeMatrix Global MQTT or a Custom Broker URL", "global", false, "", "", "", "global,custom", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("mqtt_custom_url", ConfigType::STRING, "Custom MQTT Broker URL", "Used when External MQTT Broker Source is set to Custom (e.g. mqtt://192.168.1.200:1883)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width series poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the series banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional episode stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "auto_action", false, "", "", "", "auto_action,cinema,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Series Genres", "Select one or more series genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/series/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/series/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,trailer,any)", "any", false, "", "", "", "any,opening,trailer", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new SeriesShowcaseEngine()); };
    return desc;
}

EngineDescriptor AnimeShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"anime_showcase", "Anime Showcase", "media", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 30;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 8000;
    desc.requirements.internalContiguousBytes = 12000;
    desc.requirements.psramBytes = 0;

    desc.schema.fields = {
        ConfigField("server_url", ConfigType::STRING, "Showcase Server URL", "HTTP base URL of the DMD Media Showcase server", "http://127.0.0.1:8080", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("transport_mode", ConfigType::ENUM, "Transport Protocol", "Real-Time MQTT + UDP (ffmpeg -re) or HTTP Polling fallback", "mqtt_udp", false, "", "", "", "mqtt_udp,http_poll", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("use_showcase_broker", ConfigType::BOOLEAN, "Use Showcase Embedded MQTT Broker", "Automatically connect to the Showcase server's MQTT broker (<server_ip>:1883) without extra setup", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("mqtt_broker_source", ConfigType::ENUM, "External MQTT Broker Source", "When Embedded Broker is disabled, choose ArcadeMatrix Global MQTT or a Custom Broker URL", "global", false, "", "", "", "global,custom", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("mqtt_custom_url", ConfigType::STRING, "Custom MQTT Broker URL", "Used when External MQTT Broker Source is set to Custom (e.g. mqtt://192.168.1.200:1883)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width anime key visual from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the anime banner from top to bottom after the key visual", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional anime stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "anime", false, "", "", "", "auto_action,anime,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Anime Genres", "Select one or more anime genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/anime/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/anime/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,ending,scene,any)", "opening", false, "", "", "", "any,opening,ending,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new AnimeShowcaseEngine()); };
    return desc;
}
