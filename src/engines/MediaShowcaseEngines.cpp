#include "MediaShowcaseEngines.h"
#include "WebUiSyncEngine.h"
#include "../core/drawing/IDrawingSurface.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#include "../core/net/RawMqttSocket.h"
#include "../core/net/SecureHttpClient.h"
#include "../core/net/ShowcaseDiscovery.h"
#include "../hal/HardwareHAL.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#if defined(ESP32)
#include <esp_heap_caps.h>
#if !defined(UNIT_TEST)
#include <WiFi.h>
#endif
#endif

extern ConfigLoader config;

namespace {

uint16_t parseHexColor565(const char* hex, uint16_t fallback) {
    if (!hex) return fallback;
    if (*hex == '#') hex++;
    if (strlen(hex) < 6U) return fallback;
    char buf[7];
    memcpy(buf, hex, 6U);
    buf[6] = '\0';
    uint32_t rgb = static_cast<uint32_t>(strtoul(buf, nullptr, 16));
    uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFFU);
    uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFFU);
    uint8_t b = static_cast<uint8_t>(rgb & 0xFFU);
    return static_cast<uint16_t>(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

void copySafeStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0U) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1U);
    dst[cap - 1U] = '\0';
}

void appendRawToBuf(char* dst, size_t cap, size_t& pos, const char* str) {
    if (!dst || cap == 0U || !str || pos >= cap - 1U) return;
    while (*str != '\0' && pos < cap - 1U) {
        dst[pos++] = *str++;
    }
    dst[pos] = '\0';
}

void appendCharToBuf(char* dst, size_t cap, size_t& pos, char c) {
    if (!dst || cap == 0U || pos >= cap - 1U) return;
    dst[pos++] = c;
    dst[pos] = '\0';
}

void appendJsonEscapedToBuf(char* dst, size_t cap, size_t& pos, const char* str) {
    if (!dst || cap == 0U || !str) return;
    while (*str != '\0' && pos < cap - 1U) {
        char c = *str++;
        if (c == '"' || c == '\\') {
            if (pos + 2U >= cap) break;
            dst[pos++] = '\\';
            dst[pos++] = c;
        } else if (static_cast<uint8_t>(c) >= 0x20U) {
            dst[pos++] = c;
        }
    }
    dst[pos] = '\0';
}

void appendUintToBuf(char* dst, size_t cap, size_t& pos, uint32_t val) {
    char num[16];
    snprintf(num, sizeof(num), "%u", static_cast<unsigned>(val));
    appendRawToBuf(dst, cap, pos, num);
}

void appendCsvAsJsonArrayToBuf(char* dst, size_t cap, size_t& pos, const char* csv) {
    appendCharToBuf(dst, cap, pos, '[');
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
                if (!(len == 3U && strncasecmp(start, "all", 3) == 0)) {
                    if (!first) appendCharToBuf(dst, cap, pos, ',');
                    appendCharToBuf(dst, cap, pos, '"');
                    for (size_t i = 0U; i < len && pos < cap - 2U; ++i) {
                        char c = start[i];
                        if (c != '"' && c != '\\') {
                            dst[pos++] = c;
                        }
                    }
                    dst[pos] = '\0';
                    appendCharToBuf(dst, cap, pos, '"');
                    first = false;
                }
            }
        }
    }
    appendCharToBuf(dst, cap, pos, ']');
}

bool decodeBase64Fixed(const char* b64, uint8_t* dst, size_t expectedBytes) {
    if (!b64 || !dst || expectedBytes == 0U) return false;
    size_t outIdx = 0U;
    uint32_t buf = 0U;
    uint8_t bits = 0U;
    for (const char* p = b64; *p != '\0'; ++p) {
        char c = *p;
        if (c == '=') break;
        uint8_t val = 0U;
        if (c >= 'A' && c <= 'Z') val = static_cast<uint8_t>(c - 'A');
        else if (c >= 'a' && c <= 'z') val = static_cast<uint8_t>(c - 'a' + 26);
        else if (c >= '0' && c <= '9') val = static_cast<uint8_t>(c - '0' + 52);
        else if (c == '+' || c == '-') val = 62U;
        else if (c == '/' || c == '_') val = 63U;
        else if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        else return false;

        buf = (buf << 6) | val;
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            if (outIdx < expectedBytes) {
                dst[outIdx++] = static_cast<uint8_t>((buf >> bits) & 0xFFU);
            }
        }
    }
    return (outIdx == expectedBytes);
}

bool readExactFromStream(Stream& st, uint8_t* dst, size_t len, const std::atomic<bool>& wantActive, uint32_t timeoutMs = 2500U) {
    size_t got = 0U;
    uint32_t lastProgress = millis();
    while (got < len) {
        if (!wantActive.load(std::memory_order_acquire)) return false;
        int avail = st.available();
        if (avail > 0) {
            size_t toRead = len - got;
            if (static_cast<size_t>(avail) < toRead) toRead = static_cast<size_t>(avail);
            size_t r = st.readBytes(dst + got, toRead);
            if (r > 0U) {
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

#if defined(ESP32)
inline size_t showcaseFree8BitHeap() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

inline size_t showcaseLargest8BitBlock() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

inline bool shouldYieldShowcaseToWebUi(bool hasPsram) {
    return !hasPsram && WebUiSyncEngine::isRequested();
}
#else
inline bool shouldYieldShowcaseToWebUi(bool) {
    return false;
}
#endif

void initShowcaseCommonDescriptor(EngineDescriptor& desc, const char* id, const char* name, const char* category) {
    desc.metadata = {id, name, category, FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 60;
    desc.requirements.preferredColorDepth = 8;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 14000;
    desc.requirements.internalContiguousBytes = 8192;
    desc.requirements.psramBytes = 0;
    desc.requirements.scalesRgb565FrameWithGeometry = true;
}

} // namespace

MediaShowcaseBase::MediaShowcaseBase(ShowcaseDomainId domain) : m_domain(domain) {
    const char* slug = domainSlug();
    net::ShowcaseDiscovery::formatClientId(slug, m_width, m_height, false, m_sessionId, sizeof(m_sessionId));
    m_liveCard.badgeColor565 = defaultAccent565();
    m_liveCard.hasData = false;
    m_renderCard = m_liveCard;
}

MediaShowcaseBase::~MediaShowcaseBase() {
    shutdownForDestruction();
}

bool MediaShowcaseBase::resolveMqttBroker(const ConfigSlot& cfg, char* outHost, size_t hostCap, uint16_t& outPort) {
    uint16_t showcasePort = 8890;
    char showcaseHost[64] = "";
    bool hasServerUrl = net::parseEndpointHostAndPort(cfg.serverUrl, 8890, showcaseHost, sizeof(showcaseHost), showcasePort);

    if (cfg.useShowcaseBroker) {
        if (!hasServerUrl) {
            return false;
        }
        copySafeStr(outHost, hostCap, showcaseHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
            return false;
        }
        outPort = 1883;
        return true;
    }
    if (strcasecmp(cfg.mqttBrokerSource, "custom") == 0 && cfg.mqttCustomUrl[0] != '\0') {
        if (net::parseEndpointHostAndPort(cfg.mqttCustomUrl, 1883, outHost, hostCap, outPort)) {
            if (net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
                return true;
            }
            return false;
        }
    }
    if (cfg.globalMqttHost[0] != '\0') {
        copySafeStr(outHost, hostCap, cfg.globalMqttHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
            return false;
        }
        outPort = (cfg.globalMqttPort > 0) ? cfg.globalMqttPort : 1883;
        return true;
    }
    if (hasServerUrl) {
        copySafeStr(outHost, hostCap, showcaseHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
            return false;
        }
        outPort = 1883;
        return true;
    }
    return false;
}

bool MediaShowcaseBase::allocateBuffers() {
    freeBuffers();
    uint16_t posterW = 32;
    uint16_t posterH = 32;
    computeCardPosterDims(m_width, m_height, posterW, posterH);
    size_t posterBytes = static_cast<size_t>(posterW) * static_cast<size_t>(posterH) * sizeof(uint16_t);
    if (posterBytes == 0U) posterBytes = 32U * 32U * sizeof(uint16_t);

    m_framePixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    size_t frameBytes = m_framePixels * sizeof(uint16_t);

#if defined(ESP32)
    m_hasPsram = hardwareHAL.capabilities().hasPsram;
    if (m_hasPsram) {
        m_poster565 = static_cast<uint16_t*>(heap_caps_malloc(posterBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_frame565 = static_cast<uint16_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_frame565Back = static_cast<uint16_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_psramMqttRx = static_cast<char*>(heap_caps_malloc(PSRAM_MQTT_RX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_ioScratch = static_cast<char*>(heap_caps_malloc(PSRAM_IO_SCRATCH_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (m_ioScratch) {
            m_ioScratchCap = PSRAM_IO_SCRATCH_BYTES;
        } else {
            m_ioScratch = static_cast<char*>(malloc(IO_SCRATCH_BYTES));
            m_ioScratchCap = m_ioScratch ? IO_SCRATCH_BYTES : 0U;
        }
        if (!m_poster565) {
            m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
        }
        if (!m_frame565) {
            m_frame565 = static_cast<uint16_t*>(malloc(frameBytes));
        }
    } else {
        m_frame565 = nullptr;
        m_frame565Back = nullptr;
        m_posterAliasesFrame = false;
        m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
        m_ioScratch = static_cast<char*>(malloc(IO_SCRATCH_BYTES));
        m_ioScratchCap = m_ioScratch ? IO_SCRATCH_BYTES : 0U;
    }
#else
    m_hasPsram = true;
    m_frame565 = static_cast<uint16_t*>(malloc(frameBytes));
    m_frame565Back = static_cast<uint16_t*>(malloc(frameBytes));
    m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
    m_posterAliasesFrame = false;
    m_ioScratch = static_cast<char*>(malloc(PSRAM_IO_SCRATCH_BYTES));
    m_ioScratchCap = m_ioScratch ? PSRAM_IO_SCRATCH_BYTES : 0U;
    m_psramMqttRx = static_cast<char*>(malloc(PSRAM_MQTT_RX_BYTES));
#endif

    if (m_frame565) {
        memset(m_frame565, 0, frameBytes);
    }
    if (m_frame565Back) {
        memset(m_frame565Back, 0, frameBytes);
    }
    m_posterCapBytes = posterBytes;
    if (m_poster565 && !m_posterAliasesFrame) {
        memset(m_poster565, 0, posterBytes);
    }
    if (m_ioScratch) {
        m_ioScratch[0] = '\0';
    }
    if (m_psramMqttRx) {
        m_psramMqttRx[0] = '\0';
    }
    return (m_poster565 != nullptr);
}

void MediaShowcaseBase::waitRenderBlitQuiescent() const {
    std::atomic_thread_fence(std::memory_order_seq_cst);
#if defined(ESP32) && !defined(UNIT_TEST)
    uint32_t t0 = millis();
    while ((m_renderBlitSeq.load(std::memory_order_seq_cst) & 1U) != 0U &&
           (millis() - t0) < 50U) {
        vTaskDelay(1);
    }
#endif
}

bool MediaShowcaseBase::acquireStepCFrameBuffer() {
    if (m_frame565 != nullptr) {
        return true;
    }
    const size_t frameBytes = m_framePixels * sizeof(uint16_t);
    if (frameBytes == 0U) {
        return false;
    }
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!m_hasPsram && m_poster565 != nullptr && !m_posterAliasesFrame) {
        // Card Step B has finished: reclaim the 2 KB poster buffer before allocating the
        // 8 KB video frame buffer so adjacent SRAM blocks coalesce and Wi-Fi RX keeps +2 KB headroom.
        if (m_liveCard.hasPoster) {
            m_seq.fetch_add(1U, std::memory_order_acq_rel);
            m_liveCard.hasPoster = false;
            m_seq.fetch_add(1U, std::memory_order_release);
        }
        uint16_t* oldPoster = m_poster565;
        m_poster565 = nullptr;
        waitRenderBlitQuiescent();
        free(oldPoster);
    }
    if (!m_hasPsram &&
        (frameBytes > 16384U ||
         showcaseFree8BitHeap() < (frameBytes + 2560U) ||
         showcaseLargest8BitBlock() < frameBytes)) {
        LOGW("Showcase", "Step C frame buffer skipped (%u B): free8=%u largest8=%u",
             static_cast<unsigned>(frameBytes),
             static_cast<unsigned>(showcaseFree8BitHeap()),
             static_cast<unsigned>(showcaseLargest8BitBlock()));
        return false;
    }
#endif
    uint16_t* buf = static_cast<uint16_t*>(malloc(frameBytes));
    if (!buf) {
        return false;
    }
    memset(buf, 0, frameBytes);
    m_frame565 = buf;
    return true;
}

void MediaShowcaseBase::releaseStepCFrameBuffer() {
    if (!m_frame565 || m_hasPsram) {
        return;
    }
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
    if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) == 0U) {
        m_frameSeq.fetch_add(1U, std::memory_order_acq_rel);
    }
    m_publishedFramePtr.store(nullptr, std::memory_order_release);
    if (m_liveCard.hasVideoFrame) {
        m_seq.fetch_add(1, std::memory_order_acq_rel);
        m_liveCard.hasVideoFrame = false;
        m_seq.fetch_add(1, std::memory_order_release);
    }
    uint16_t* oldBuf = m_frame565;
    m_frame565 = nullptr;
    waitRenderBlitQuiescent();
    free(oldBuf);
    if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
        m_frameSeq.fetch_add(1, std::memory_order_release);
    }
    if (m_wantActive.load(std::memory_order_acquire) &&
        !m_poster565 &&
        !m_posterAliasesFrame &&
        m_posterCapBytes > 0U) {
        m_poster565 = static_cast<uint16_t*>(malloc(m_posterCapBytes));
        if (m_poster565) {
            memset(m_poster565, 0, m_posterCapBytes);
        }
    }
}

void MediaShowcaseBase::freeBuffers() {
    m_publishedFramePtr.store(nullptr, std::memory_order_release);
    waitRenderBlitQuiescent();
    if (m_psramMqttRx) {
        free(m_psramMqttRx);
        m_psramMqttRx = nullptr;
    }
    if (m_ioScratch) {
        free(m_ioScratch);
        m_ioScratch = nullptr;
    }
    m_ioScratchCap = 0U;
    if (m_poster565) {
        if (!m_posterAliasesFrame) {
            free(m_poster565);
        }
        m_poster565 = nullptr;
    }
    m_posterAliasesFrame = false;
    if (m_frame565Back) {
        free(m_frame565Back);
        m_frame565Back = nullptr;
    }
    if (m_frame565) {
        free(m_frame565);
        m_frame565 = nullptr;
    }
    m_posterCapBytes = 0U;
    m_framePixels = 0U;
}

void MediaShowcaseBase::clearLiveCardState() {
    m_publishedFramePtr.store(nullptr, std::memory_order_release);
    m_seq.fetch_add(1, std::memory_order_acq_rel);
    m_liveCard.hasData = false;
    m_liveCard.hasPoster = false;
    m_liveCard.hasVideoFrame = false;
    m_seq.fetch_add(1, std::memory_order_release);
    if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
        m_frameSeq.fetch_add(1, std::memory_order_release);
    }
    m_consumedFrameSeq.store(m_frameSeq.load(std::memory_order_relaxed), std::memory_order_release);
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
    net::ShowcaseDiscovery::formatClientId(domainSlug(), m_width, m_height, false, m_sessionId, sizeof(m_sessionId));

    if (config) {
        onConfigChanged(config);
    }
    return EngineError::OK;
}

void MediaShowcaseBase::copySafeCsvTokens(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0U) return;
    if (!src || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len < cap) {
        memcpy(dst, src, len + 1U);
        return;
    }
    size_t maxCopy = cap - 1U;
    size_t cutPos = 0U;
    bool foundComma = false;
    for (size_t i = 0U; i < maxCopy; ++i) {
        if (src[i] == ',') {
            cutPos = i;
            foundComma = true;
        }
    }
    if (foundComma) {
        while (cutPos > 0U && (src[cutPos - 1U] == ' ' || src[cutPos - 1U] == ',')) {
            --cutPos;
        }
        memcpy(dst, src, cutPos);
        dst[cutPos] = '\0';
    } else {
        memcpy(dst, src, maxCopy);
        dst[maxCopy] = '\0';
    }
}

void MediaShowcaseBase::onConfigChanged(const EngineConfig* config) {
    if (!config) return;
    uint8_t cur = m_cfgIdx.load(std::memory_order_relaxed);
    uint8_t next = (cur + 1U) & 1U;
    ConfigSlot& slot = m_cfg[next];

    {
        ConfigSnapshotGuard guard = ::config.acquireSnapshot();
        const auto& snap = guard.get();
        const auto& sc = snap.showcase;

        slot.enabled = sc.enabled;
        String srv = sc.server_url;
        srv.trim();
        if (srv.isEmpty()) {
            srv = config->getString("server_url", "http://dmdmediashowcase.local:8890");
        }
        srv = normalizeMdnsEndpointUrl(srv, true);
        copySafeStr(slot.serverUrl, sizeof(slot.serverUrl), srv.c_str());

        String rawMode = sc.transport_mode.isEmpty() ? config->getString("transport_mode", "mqtt_tcp") : sc.transport_mode;
        String tMode = normalizeShowcaseTransportMode(rawMode);
        copySafeStr(slot.transportMode, sizeof(slot.transportMode), tMode.c_str());

        slot.useShowcaseBroker = sc.server_url.isEmpty() ? config->getBool("use_showcase_broker", true) : sc.use_showcase_broker;

        String bSource = sc.mqtt_broker_source.isEmpty() ? config->getString("mqtt_broker_source", "global") : sc.mqtt_broker_source;
        copySafeStr(slot.mqttBrokerSource, sizeof(slot.mqttBrokerSource), bSource.c_str());

        String cUrl = sc.mqtt_custom_url.isEmpty() ? config->getString("mqtt_custom_url", "") : sc.mqtt_custom_url;
        cUrl = normalizeMdnsEndpointUrl(cUrl, false);
        copySafeStr(slot.mqttCustomUrl, sizeof(slot.mqttCustomUrl), cUrl.c_str());

        copySafeStr(slot.globalMqttHost, sizeof(slot.globalMqttHost), snap.mqtt.broker.c_str());
        slot.globalMqttPort = (snap.mqtt.port > 0 && snap.mqtt.port <= 65535)
            ? static_cast<uint16_t>(snap.mqtt.port)
            : 1883;
    }

    copySafeStr(slot.lang, sizeof(slot.lang), config->getString("lang", "system").c_str());
    copySafeStr(slot.textScrollSpeed, sizeof(slot.textScrollSpeed), config->getString("text_scroll_speed", "4").c_str());
    {
        String rawVm = config->getString("video_mode", "auto_action");
        rawVm.trim();
        rawVm.toLowerCase();
        const char* normVm = "auto_action";
        if (rawVm == "panorama") {
            normVm = "panorama";
        } else if (rawVm == "center_crop") {
            normVm = "center_crop";
        }
        copySafeStr(slot.videoMode, sizeof(slot.videoMode), normVm);
    }
    copySafeCsvTokens(slot.systems, sizeof(slot.systems), config->getString("systems", "").c_str());
    copySafeCsvTokens(slot.genres, sizeof(slot.genres), config->getString("genres", "").c_str());
    copySafeCsvTokens(slot.decades, sizeof(slot.decades), config->getString("decades", "").c_str());
    copySafeStr(slot.videoType, sizeof(slot.videoType), config->getString("video_type", "any").c_str());

    slot.snapshotPanoramaCount = static_cast<uint8_t>(std::max(0, std::min(5, config->getInt("snapshot_panorama_count", 0))));
    slot.videoMaxDurationS = static_cast<uint8_t>(std::max(2, std::min(120, config->getInt("video_max_duration_s", 8))));
    slot.videoFps = static_cast<uint8_t>(std::max(8, std::min(60, config->getInt("video_fps", 20))));

    slot.showPoster = config->getBool("show_poster", true);
    slot.showSynopsis = config->getBool("show_synopsis", true);
    slot.jacketPanorama = config->getBool("jacket_panorama", true);
    slot.marqueePanorama = config->getBool("marquee_panorama", false);
    slot.showVideo = config->getBool("show_video", true);
    {
        String rawCb = config->getString("color_boost", "auto");
        rawCb.trim();
        rawCb.toLowerCase();
        const char* normCb = "auto";
        if (rawCb == "pixel_art") {
            normCb = "pixel_art";
        } else if (rawCb == "anime") {
            normCb = "anime";
        } else if (rawCb == "cinema" || rawCb == "false" || rawCb == "0" || rawCb == "off") {
            normCb = "cinema";
        }
        copySafeStr(slot.colorBoost, sizeof(slot.colorBoost), normCb);
    }

    uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
    m_fallbackCycleMs = std::max<uint32_t>(30000U, static_cast<uint32_t>(slot.videoMaxDurationS + 25) * budget * 1000U);

    m_cfgIdx.store(next, std::memory_order_release);
    m_cfgGen.fetch_add(1, std::memory_order_acq_rel);
    m_lastPublishedCfgGen = 0;
    m_consecutiveNetFailures = 0;
    m_consecutiveWarmSkips.store(0, std::memory_order_release);
    if (m_wantActive.load(std::memory_order_acquire)) {
        m_skipTurnImmediately.store(false, std::memory_order_release);
        m_turnDone.store(false, std::memory_order_release);
        m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
        clearLiveCardState();
        m_lastRenderedCardSeq = 0xFFFFFFFFU;
        m_lastRenderedFrameSeq = 0xFFFFFFFFU;
        m_lastRenderedPhase = 0xFFU;
        m_dirty = true;
        startWorkerTask();
    }
}

void MediaShowcaseBase::activate() {
    m_pausedByPreemption = false;
    m_activatedAtMs = millis();
    m_phaseStartedAtMs = m_activatedAtMs;
    m_lastScrollStepMs = m_activatedAtMs;
    m_consecutiveNetFailures = 0;
    m_itemsPlayedInTurn.store(0, std::memory_order_release);
    m_skipTurnImmediately.store(false, std::memory_order_release);
    m_warmingFallback.store(false, std::memory_order_release);
    m_turnDone.store(false, std::memory_order_release);
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
    clearLiveCardState();
    m_lastRenderedCardSeq = 0xFFFFFFFFU;
    m_lastRenderedFrameSeq = 0xFFFFFFFFU;
    m_lastRenderedPhase = 0xFFU;
    m_dirty = true;
    m_renderedNewFrame = false;

    size_t wantPixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    if (!m_poster565 || !m_ioScratch || m_framePixels != wantPixels) {
        allocateBuffers();
    }
    if (!startWorkerTask()) {
#if defined(ESP32) && !defined(UNIT_TEST)
        LOGE("Showcase", "Failed to spawn ShowcaseWkr (free8=%u largest8=%u); yielding rotation slot",
             static_cast<unsigned>(showcaseFree8BitHeap()),
             static_cast<unsigned>(showcaseLargest8BitBlock()));
        m_statusState.store(2, std::memory_order_release);
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
        }
#endif
    }
}

void MediaShowcaseBase::pause() {
    m_pausedByPreemption = true;
    deactivate();
}

void MediaShowcaseBase::resume() {
    m_lastRenderedCardSeq = 0xFFFFFFFFU;
    m_lastRenderedFrameSeq = 0xFFFFFFFFU;
    m_lastRenderedPhase = 0xFFU;
    m_dirty = true;
    if (m_pausedByPreemption) {
        m_pausedByPreemption = false;
        activate();
    }
}

void MediaShowcaseBase::deactivate() {
    m_wantActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
    stopWorkerTaskBounded(1500);
    clearLiveCardState();
    if (!m_taskAlive.load(std::memory_order_acquire)) {
        freeBuffers();
    }
    m_dirty = false;
    m_renderedNewFrame = false;
}

bool MediaShowcaseBase::shutdownForDestruction() {
    m_wantActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
    stopWorkerTaskBounded(2000);
    clearLiveCardState();
    const bool stopped = !m_taskAlive.load(std::memory_order_acquire);
    if (stopped) {
        freeBuffers();
    }
    m_dirty = false;
    m_renderedNewFrame = false;
    return stopped;
}

uint32_t MediaShowcaseBase::configuredMaxStreamMs(const ConfigSlot& cfg) {
    uint32_t videoSec = cfg.showVideo
        ? std::max<uint32_t>(2U, std::min<uint32_t>(120U, static_cast<uint32_t>(cfg.videoMaxDurationS)))
        : 0U;
    uint32_t panCount = (cfg.jacketPanorama ? 1U : 0U)
        + (cfg.marqueePanorama ? 1U : 0U)
        + std::min<uint32_t>(5U, static_cast<uint32_t>(cfg.snapshotPanoramaCount));
    uint32_t totalSec = std::max<uint32_t>(2U, panCount * 4U + videoSec);
    return totalSec * 1000U;
}

uint32_t MediaShowcaseBase::computeTurnSafetyTimeoutMs() const {
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
    uint32_t perItemMs = configuredMaxStreamMs(cfg) + 45000U;
    return std::max<uint32_t>(45000U, perItemMs * budget);
}

bool MediaShowcaseBase::isFinished() const {
    if (m_skipTurnImmediately.load(std::memory_order_acquire)) {
        return true;
    }
    if (m_turnDone.load(std::memory_order_acquire)) {
        return true;
    }
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    if (!cfg.enabled && m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
        return true;
    }
    if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
        return (millis() - m_activatedAtMs) >= computeTurnSafetyTimeoutMs();
    }
    return false;
}

void MediaShowcaseBase::finishTurnItem() {
    clearLiveCardState();
    uint32_t played = m_itemsPlayedInTurn.fetch_add(1, std::memory_order_acq_rel) + 1U;
    uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
    if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1 && played >= budget) {
        m_turnDone.store(true, std::memory_order_release);
    }
}

bool MediaShowcaseBase::startWorkerTask() {
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    if (!cfg.enabled) {
        m_statusState.store(3, std::memory_order_release);
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
        }
        return true;
    }
    m_wantActive.store(true, std::memory_order_release);
#if defined(ESP32) && !defined(UNIT_TEST)
    if (m_taskAlive.load(std::memory_order_acquire)) return true;
    m_taskAlive.store(true, std::memory_order_release);
    uint32_t stackBytes = m_hasPsram ? 8192U : 4608U;
    BaseType_t ok = xTaskCreatePinnedToCore(
        workerTaskEntry,
        "ShowcaseWkr",
        stackBytes,
        this,
        2,
        &m_task,
        0
    );
    if (ok != pdPASS) {
        m_taskAlive.store(false, std::memory_order_release);
        m_task = nullptr;
        return false;
    }
#endif
    return true;
}

void MediaShowcaseBase::stopWorkerTaskBounded(uint32_t timeoutMs) {
#if defined(ESP32) && !defined(UNIT_TEST)
    const bool hadTask = (m_task != nullptr) || m_taskAlive.load(std::memory_order_acquire);
    uint32_t start = millis();
    while (m_taskAlive.load(std::memory_order_acquire) && (millis() - start) < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (hadTask && !m_taskAlive.load(std::memory_order_acquire)) {
        vTaskDelay(pdMS_TO_TICKS(20));
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
    while (m_wantActive.load(std::memory_order_acquire)) {
#if defined(ESP32) && !defined(UNIT_TEST)
        if (!m_hasPsram &&
            (WebUiSyncEngine::isRequested() ||
             WebUiSyncEngine::isReadyForServing() ||
             NetworkBudget::getDmdNetActivity() == NetworkBudget::DmdNetActivity::WebUiSync)) {
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }
#endif
        uint32_t startGen = m_cfgGen.load(std::memory_order_acquire);
        const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
        const char* domain = domainSlug();

        if (!cfg.enabled || strlen(cfg.serverUrl) < 8U || strncmp(cfg.serverUrl, "http", 4) != 0) {
            m_statusState.store(3, std::memory_order_release);
            clearLiveCardState();
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
            return;
        }

        char reqLangBuf[12] = "en";
        if (cfg.lang[0] != '\0' && strcasecmp(cfg.lang, "system") != 0) {
            copySafeStr(reqLangBuf, sizeof(reqLangBuf), cfg.lang);
        } else {
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            const String& sysLang = guard.get().system.lang;
            if (sysLang.length() > 0) {
                copySafeStr(reqLangBuf, sizeof(reqLangBuf), sysLang.c_str());
            }
        }

        net::ShowcaseDiscovery::formatClientId(domain, m_width, m_height, false, m_sessionId, sizeof(m_sessionId));

        bool useMqttMode = (strcasecmp(cfg.transportMode, "mqtt_udp") == 0 ||
                            strcasecmp(cfg.transportMode, "mqtt_tcp") == 0);
        if (useMqttMode) {
            char httpFallbackItemId[MAX_ITEM_ID_LEN] = "";
            bool handled = runMqttUdpTurn(cfg, domain, reqLangBuf, httpFallbackItemId, sizeof(httpFallbackItemId));
            if (handled && httpFallbackItemId[0] != '\0') {
                if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen) {
                    char fallbackPath[256];
                    snprintf(
                        fallbackPath,
                        sizeof(fallbackPath),
                        "/api/v1/%s/stream/%s.rgb565.stream?purge=true&paced=1&codec=dmdz1&client_id=%s",
                        domain,
                        httpFallbackItemId,
                        m_sessionId
                    );
                    streamUnifiedRgb565Http(cfg.serverUrl, fallbackPath);
                }
                finishTurnItem();
            } else if (!handled && m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen) {
                if (m_consecutiveNetFailures < 10U) {
                    ++m_consecutiveNetFailures;
                }
                if (shouldReportNetworkOffline(m_consecutiveNetFailures)) {
                    m_statusState.store(2, std::memory_order_release);
                    m_lastPublishedCfgGen = 0;
                    clearLiveCardState();
                    if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                        m_skipTurnImmediately.store(true, std::memory_order_release);
                    }
                } else if (m_statusState.load(std::memory_order_acquire) != 0U) {
                    m_statusState.store(1, std::memory_order_release);
                }
#if defined(ESP32)
                for (int w = 0; w < 12 && m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen; ++w) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
#endif
            }
        } else if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen) {
            runHttpPollTurn(cfg, domain, reqLangBuf);
        }
        if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) != startGen) {
            m_skipTurnImmediately.store(false, std::memory_order_release);
            m_turnDone.store(false, std::memory_order_release);
            clearLiveCardState();
            continue;
        }
        if (!m_turnDone.load(std::memory_order_acquire) &&
            !m_skipTurnImmediately.load(std::memory_order_acquire) &&
            m_wantActive.load(std::memory_order_acquire)) {
#if defined(ESP32)
            vTaskDelay(pdMS_TO_TICKS(10));
#endif
            continue;
        }
        break;
    }
}

bool MediaShowcaseBase::parseReadyPayloadInPlaceBuf(
    char* jsonBuf,
    bool showPoster,
    char* outItemId,
    size_t itemIdCap,
    bool& outHasStream,
    uint32_t& outStreamMaxMs,
    bool& outExplicitWarming
) {
    outExplicitWarming = false;
    if (outItemId && itemIdCap > 0U) outItemId[0] = '\0';
    if (!jsonBuf || jsonBuf[0] == '\0') return false;

    const char* domain = domainSlug();
    CardSnapshot nextCard;
    const char* posterB64 = "";

    {
        StaticJsonDocument<JSON_OBJECT_SIZE(44)> doc;
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

        copySafeStr(nextCard.title, sizeof(nextCard.title), cardObj["title"] | "UNKNOWN");
        copySafeStr(nextCard.year, sizeof(nextCard.year), cardObj["year"] | "");
        const char* badge = cardObj["badge"] | domain;
        copySafeStr(nextCard.badge, sizeof(nextCard.badge), badge);
        copySafeStr(nextCard.rating, sizeof(nextCard.rating), cardObj["rating"] | "");
        copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), cardObj["synopsis"] | "");
        nextCard.badgeColor565 = parseHexColor565(cardObj["badge_color"] | nullptr, defaultAccent565());
        computeCardPosterDims(m_width, m_height, nextCard.posterW, nextCard.posterH);
        uint16_t srvPw = cardObj["poster_width"] | 0;
        uint16_t srvPh = cardObj["poster_height"] | 0;
        if (srvPw > 0 && srvPh > 0 && (static_cast<size_t>(srvPw) * static_cast<size_t>(srvPh) * 2U) <= m_posterCapBytes) {
            nextCard.posterW = srvPw;
            nextCard.posterH = srvPh;
        }
        nextCard.hasData = true;
        nextCard.hasPoster = false;
        nextCard.hasVideoFrame = false;

        const char* rawItemId = doc["item_id"] | (cardObj["item_id"] | (cardObj["id"] | ""));
        if (outItemId && itemIdCap > 0U) {
            copySafeStr(outItemId, itemIdCap, rawItemId);
        }
        outHasStream = (cardObj["has_panoramas"] | false) || (cardObj["has_video"] | false);
        float totDur = cardObj["stream_duration_s"] | (cardObj["total_stream_duration_s"] | 8.0f);
        if (totDur < 2.0f) totDur = 2.0f;
        outStreamMaxMs = static_cast<uint32_t>((totDur + 4.0f) * 1000.0f);

        posterB64 = cardObj["poster_rgb565_b64"] | "";
    }

    size_t expectedPosterBytes = static_cast<size_t>(nextCard.posterW) * static_cast<size_t>(nextCard.posterH) * 2U;
    if (showPoster && !m_poster565 && posterB64[0] != '\0') {
        if (m_ioScratch != nullptr && m_posterCapBytes >= expectedPosterBytes && m_posterCapBytes > 0U) {
            m_poster565 = static_cast<uint16_t*>(malloc(m_posterCapBytes));
        } else if (jsonBuf != m_ioScratch) {
            allocateBuffers();
        }
    }
    if (showPoster && m_poster565 && expectedPosterBytes <= m_posterCapBytes) {
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

bool MediaShowcaseBase::parseReadyPayloadInPlace(
    char* jsonBuf,
    bool showPoster,
    String& outItemId,
    bool& outHasStream,
    uint32_t& outStreamMaxMs,
    bool& outExplicitWarming
) {
    char itemBuf[MAX_ITEM_ID_LEN] = "";
    bool ok = parseReadyPayloadInPlaceBuf(
        jsonBuf,
        showPoster,
        itemBuf,
        sizeof(itemBuf),
        outHasStream,
        outStreamMaxMs,
        outExplicitWarming
    );
    outItemId = itemBuf;
    return ok;
}

bool MediaShowcaseBase::runMqttUdpTurn(
    const ConfigSlot& cfg,
    const char* domain,
    const char* reqLang,
    char* outHttpFallbackItemId,
    size_t fallbackItemCap
) {
    if (outHttpFallbackItemId && fallbackItemCap > 0U) {
        outHttpFallbackItemId[0] = '\0';
    }
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!m_ioScratch) {
        return false;
    }

    char mqttHost[64] = "";
    uint16_t mqttPort = 1883;
    if (!resolveMqttBroker(cfg, mqttHost, sizeof(mqttHost), mqttPort)) {
        return false;
    }
    uint16_t showcaseHttpPort = 8890;
    {
        char dummyScheme[12] = "";
        char dummyHost[64] = "";
        (void)net::ShowcaseDiscovery::parseEndpoint(
            cfg.serverUrl,
            8890,
            dummyScheme,
            sizeof(dummyScheme),
            dummyHost,
            sizeof(dummyHost),
            showcaseHttpPort
        );
    }

    uint32_t startGen = m_cfgGen.load(std::memory_order_acquire);
    uint16_t localUdpPort = static_cast<uint16_t>(5568U + static_cast<uint16_t>(m_domain));
    const char* statusTopic = "dmd_showcase/server/status";
    char readyTopic[96];
    char eventsTopic[96];
    snprintf(readyTopic, sizeof(readyTopic), "dmd_showcase/clients/%s/%s/ready", m_sessionId, domain);
    snprintf(eventsTopic, sizeof(eventsTopic), "dmd_showcase/clients/%s/%s/events", m_sessionId, domain);

    IPAddress ipAddr = WiFi.localIP();
    char localIpBuf[20];
    snprintf(localIpBuf, sizeof(localIpBuf), "%u.%u.%u.%u", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);

    uint8_t prevSkips = m_consecutiveWarmSkips.load(std::memory_order_acquire);
    uint32_t playedSoFar = m_itemsPlayedInTurn.load(std::memory_order_acquire);
    bool soloSlot = (m_rotationSlotCount.load(std::memory_order_relaxed) <= 1U);
    uint32_t warmSettleMs = soloSlot
        ? 2200U
        : ((prevSkips >= 1U && playedSoFar == 0U) ? 350U : 0U);
    uint32_t maxWaitMs = soloSlot ? 2600U : 900U;

    bool gotReadyItem = false;
    bool sawReadyFalse = false;
    bool sawServerOnline = false;
    char itemIdBuf[MAX_ITEM_ID_LEN] = "";
    bool itemHasStream = false;
    uint32_t streamMaxMs = configuredMaxStreamMs(cfg) + 4000U;

    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

    // =========================================================================
    // STEP A: Scoped Pre-Stream MQTT Session (`net::RawMqttSocket`, zero heap, zero TIME_WAIT)
    // =========================================================================
    {
        NetworkBudget::ScopedDmdNetActivity mqttAct(NetworkBudget::DmdNetActivity::Mqtt);
        net::RawMqttSocket mqttCli;
        if (!net::connectMqttBrokerSession(mqttCli, mqttHost, mqttPort, m_sessionId, m_mqttConnSeq, m_wantActive, 1500, 1000)) {
            if (!m_wantActive.load(std::memory_order_acquire)) {
                return true;
            }
            return false;
        }
        m_consecutiveNetFailures = 0;
        if (cfg.useShowcaseBroker) {
            net::ShowcaseDiscovery::touchVerifiedHost(mqttHost, showcaseHttpPort);
        }

        if (m_lastPublishedCfgGen != startGen) {
            uint16_t posterW = 32;
            uint16_t posterH = 32;
            computeCardPosterDims(m_width, m_height, posterW, posterH);
            uint8_t effFps = resolveEffectiveVideoFps(cfg.videoFps, m_hasPsram);

            char configTopic[80];
            snprintf(configTopic, sizeof(configTopic), "dmd_showcase/clients/%s/config", m_sessionId);

            size_t pos = 0U;
            m_ioScratch[0] = '\0';
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "{\"dmd_id\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_sessionId);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"client_ip\":\"");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, localIpBuf);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"udp_ip\":\"");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, localIpBuf);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"transport\":\"mqtt_tcp\",\"width\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_width);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"height\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_height);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"poster_width\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, posterW);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"poster_height\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, posterH);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"udp_port\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, localUdpPort);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"lang\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, reqLang);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"domains\":{\"");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, domain);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\":{\"systems\":");
            appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.systems);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"genres\":");
            appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.genres);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"decades\":");
            appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.decades);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_type\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoType);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"show_poster\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showPoster ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"show_synopsis\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showSynopsis ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"text_scroll_speed\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.textScrollSpeed);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"jacket_panorama\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.jacketPanorama ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"marquee_panorama\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.marqueePanorama ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"snapshot_panorama_count\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.snapshotPanoramaCount);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"show_video\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showVideo ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_max_duration_s\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoMaxDurationS);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_fps\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, effFps);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_mode\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoMode);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"color_boost\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.colorBoost);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\"}}}");

            if (net::sendMqttPublishPacket(mqttCli, configTopic, m_ioScratch, true, m_wantActive, m_ioScratch, IO_SCRATCH_BYTES)) {
                m_lastPublishedCfgGen = startGen;
            }
        }

        net::sendMqttSubscribePacket(mqttCli, 1, statusTopic, m_wantActive);
        net::sendMqttSubscribePacket(mqttCli, 2, readyTopic, m_wantActive);

        char* primaryRxBuf = m_psramMqttRx
            ? m_psramMqttRx
            : reinterpret_cast<char*>(m_frame565);
        size_t primaryRxCap = m_psramMqttRx
            ? PSRAM_MQTT_RX_BYTES
            : (m_frame565 ? (m_framePixels * sizeof(uint16_t)) : 0U);

        uint32_t waitReadyStart = millis();
        uint32_t readyFalseAtMs = 0;

        while (m_wantActive.load(std::memory_order_acquire) &&
               !shouldYieldShowcaseToWebUi(m_hasPsram) &&
               m_cfgGen.load(std::memory_order_acquire) == startGen &&
               (millis() - waitReadyStart) < maxWaitMs &&
               !gotReadyItem) {
            if (sawReadyFalse && (millis() - readyFalseAtMs) >= warmSettleMs) {
                break;
            }
            if (mqttCli.available() >= 2U) {
                int rawHdr = mqttCli.readByte();
                if (rawHdr < 0) {
                    break;
                }
                uint8_t fixedHdr = static_cast<uint8_t>(rawHdr);
                uint32_t remLen = 0U;
                uint32_t mult = 1U;
                for (int i = 0; i < 4; ++i) {
                    uint8_t b = 0U;
                    if (!mqttCli.readExact(&b, 1U, m_wantActive, 1000U)) break;
                    remLen += static_cast<uint32_t>(b & 0x7FU) * mult;
                    mult *= 128U;
                    if ((b & 0x80U) == 0U) break;
                }
                const uint32_t maxMqttPacketBytes = m_hasPsram ? 32768U : 14336U;
                if ((fixedHdr & 0xF0U) != 0x30U || remLen < 2U || remLen >= maxMqttPacketBytes) {
                    if (remLen > 0U) {
                        mqttCli.drain(remLen, m_wantActive, 1500U);
                    }
                    continue;
                }

                uint8_t tLenBe[2];
                if (!mqttCli.readExact(tLenBe, 2U, m_wantActive, 1000U)) {
                    break;
                }
                uint16_t tLen = static_cast<uint16_t>((tLenBe[0] << 8) | tLenBe[1]);
                if (static_cast<uint32_t>(tLen) + 2U > remLen) {
                    mqttCli.drain(remLen - 2U, m_wantActive, 1000U);
                    continue;
                }
                char topicBuf[112] = "";
                size_t toReadT = (tLen < sizeof(topicBuf) - 1U) ? tLen : (sizeof(topicBuf) - 1U);
                if (toReadT > 0U) {
                    mqttCli.readExact(reinterpret_cast<uint8_t*>(topicBuf), toReadT, m_wantActive, 1000U);
                }
                topicBuf[toReadT] = '\0';
                if (tLen > toReadT) {
                    mqttCli.drain(tLen - toReadT, m_wantActive, 1000U);
                }
                size_t payloadLen = remLen - 2U - static_cast<size_t>(tLen);

                char* payloadBuf = nullptr;
                bool freePayloadAfter = false;
                if (payloadLen + 1U <= IO_SCRATCH_BYTES) {
                    payloadBuf = m_ioScratch;
                } else if (primaryRxBuf && (payloadLen + 1U) <= primaryRxCap) {
                    payloadBuf = primaryRxBuf;
                } else if (showcaseLargest8BitBlock() >= (payloadLen + 2048U)) {
                    payloadBuf = static_cast<char*>(malloc(payloadLen + 1U));
                    freePayloadAfter = (payloadBuf != nullptr);
                }
                if (!payloadBuf) {
                    mqttCli.drain(payloadLen, m_wantActive, 1500U);
                    continue;
                }
                const bool borrowsFrameBuf = (payloadBuf == reinterpret_cast<char*>(m_frame565) && m_frame565 != nullptr);
                if (borrowsFrameBuf && (m_frameSeq.load(std::memory_order_relaxed) & 1U) == 0U) {
                    m_frameSeq.fetch_add(1, std::memory_order_acq_rel);
                }
                auto releaseBorrowedFrameBuf = [&](bool preserveAliasedPoster) {
                    if (!borrowsFrameBuf) return;
                    const size_t totalFrameBytes = m_framePixels * sizeof(uint16_t);
                    const size_t keepBytes = (preserveAliasedPoster && m_posterAliasesFrame && m_liveCard.hasPoster)
                        ? std::min(totalFrameBytes, m_posterCapBytes)
                        : 0U;
                    if (totalFrameBytes > keepBytes) {
                        memset(reinterpret_cast<uint8_t*>(m_frame565) + keepBytes, 0, totalFrameBytes - keepBytes);
                    }
                    if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
                        m_frameSeq.fetch_add(1, std::memory_order_release);
                    }
                };
                if (!mqttCli.readExact(reinterpret_cast<uint8_t*>(payloadBuf), payloadLen, m_wantActive, 2000U)) {
                    releaseBorrowedFrameBuf(false);
                    if (freePayloadAfter) free(payloadBuf);
                    break;
                }
                payloadBuf[payloadLen] = '\0';

                if (strcmp(topicBuf, statusTopic) == 0) {
                    StaticJsonDocument<192> sDoc;
                    if (deserializeJson(sDoc, payloadBuf) == DeserializationError::Ok) {
                        bool online = sDoc["online"] | false;
                        if (!online) {
                            if (freePayloadAfter) free(payloadBuf);
                            releaseBorrowedFrameBuf(false);
                            m_statusState.store(2, std::memory_order_release);
                            m_lastPublishedCfgGen = 0;
                            clearLiveCardState();
                            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                                m_skipTurnImmediately.store(true, std::memory_order_release);
                            }
                            net::sendMqttDisconnectAndStop(mqttCli, m_wantActive);
                            return true;
                        }
                        sawServerOnline = true;
                    }
                } else if (strcmp(topicBuf, readyTopic) == 0) {
                    bool explicitWarming = false;
                    if (parseReadyPayloadInPlaceBuf(payloadBuf, cfg.showPoster, itemIdBuf, sizeof(itemIdBuf), itemHasStream, streamMaxMs, explicitWarming)) {
                        gotReadyItem = true;
                    } else if (explicitWarming) {
                        m_statusState.store(1, std::memory_order_release);
                        if (!sawReadyFalse) {
                            sawReadyFalse = true;
                            readyFalseAtMs = millis();
                        }
                    }
                }
                releaseBorrowedFrameBuf(true);
                if (freePayloadAfter) {
                    free(payloadBuf);
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        if (!gotReadyItem) {
            net::sendMqttDisconnectAndStop(mqttCli, m_wantActive);
            if (shouldYieldShowcaseToWebUi(m_hasPsram)) {
                return true;
            }
            if (!sawReadyFalse) {
                m_lastPublishedCfgGen = 0;
            }
            if (sawReadyFalse || sawServerOnline) {
                m_statusState.store(1, std::memory_order_release);
                if (playedSoFar == 0U && prevSkips < 10U) {
                    m_consecutiveWarmSkips.store(static_cast<uint8_t>(prevSkips + 1U), std::memory_order_release);
                } else if (playedSoFar > 0U) {
                    m_consecutiveWarmSkips.store(0, std::memory_order_release);
                }
                if (!soloSlot) {
                    for (int w = 0;
                         w < 56 &&
                         m_wantActive.load(std::memory_order_acquire) &&
                         !shouldYieldShowcaseToWebUi(m_hasPsram) &&
                         m_cfgGen.load(std::memory_order_acquire) == startGen;
                         ++w) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    m_skipTurnImmediately.store(true, std::memory_order_release);
                } else {
                    for (int w = 0;
                         w < 10 &&
                         m_wantActive.load(std::memory_order_acquire) &&
                         !shouldYieldShowcaseToWebUi(m_hasPsram) &&
                         m_cfgGen.load(std::memory_order_acquire) == startGen;
                         ++w) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                }
                return true;
            }
            return false;
        }

        net::sendMqttUnsubscribePacket(mqttCli, 3, readyTopic, m_wantActive);
        char startCardCmd[256];
        snprintf(
            startCardCmd,
            sizeof(startCardCmd),
            "{\"action\":\"start_card\",\"item_id\":\"%s\",\"transport\":\"mqtt_tcp\",\"client_ip\":\"%s\",\"udp_ip\":\"%s\"}",
            itemIdBuf,
            localIpBuf,
            localIpBuf
        );
        net::sendMqttPublishPacket(mqttCli, eventsTopic, startCardCmd, false, m_wantActive, m_ioScratch, IO_SCRATCH_BYTES);
        net::sendMqttDisconnectAndStop(mqttCli, m_wantActive);
    }

    m_consecutiveWarmSkips.store(0, std::memory_order_release);

    // =========================================================================
    // STEP B: Zero-Socket Organic Card Dwell
    // =========================================================================
    uint32_t synGlyphs = static_cast<uint32_t>(countUtf8Glyphs(m_liveCard.synopsis));
    uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
    uint32_t cardWaitMs = 4500U;
    if (cfg.showSynopsis && synGlyphs > 0U) {
        uint32_t textW = synGlyphs * 6U;
        uint32_t availW = static_cast<uint32_t>(
            engines::showcase::ShowcaseCardRenderer::computeCardTextViewportWidth(m_width, m_height, cfg.showPoster)
        );
        if (textW > availW) {
            uint32_t scrollDistPx = (textW - availW) + (availW / 2U);
            uint32_t synScrollMs = 900U + (scrollDistPx * 1000U) / speedPx + 700U;
            if (synScrollMs > cardWaitMs) cardWaitMs = synScrollMs;
        }
    }
    uint32_t cardT0 = millis();
    while (m_wantActive.load(std::memory_order_acquire) &&
           !shouldYieldShowcaseToWebUi(m_hasPsram) &&
           m_cfgGen.load(std::memory_order_acquire) == startGen &&
           (millis() - cardT0) < cardWaitMs) {
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    // =========================================================================
    // STEP C: Credit-Acked WebSocket / Paced TCP Stream Handoff (`DMDZ` / `DMDS`)
    // =========================================================================
    if (m_wantActive.load(std::memory_order_acquire) &&
        !shouldYieldShowcaseToWebUi(m_hasPsram) &&
        m_cfgGen.load(std::memory_order_acquire) == startGen &&
        itemHasStream &&
        outHttpFallbackItemId &&
        fallbackItemCap > 0U) {
        copySafeStr(outHttpFallbackItemId, fallbackItemCap, itemIdBuf);
        return true;
    }

    finishTurnItem();
    return true;
#else
    (void)cfg;
    (void)domain;
    (void)reqLang;
    (void)outHttpFallbackItemId;
    (void)fallbackItemCap;
    return false;
#endif
}

void MediaShowcaseBase::runHttpPollTurn(const ConfigSlot& cfg, const char* domain, const char* reqLang) {
    if (!m_ioScratch) return;

    auto handleTransientHttpFailure = [this]() {
        if (m_consecutiveNetFailures < 10U) {
            ++m_consecutiveNetFailures;
        }
        if (shouldReportNetworkOffline(m_consecutiveNetFailures)) {
            m_statusState.store(2, std::memory_order_release);
            clearLiveCardState();
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
        } else if (m_statusState.load(std::memory_order_acquire) != 0U) {
            m_statusState.store(1, std::memory_order_release);
        }
#if defined(ESP32)
        for (int w = 0; w < 10 && m_wantActive.load(std::memory_order_acquire); ++w) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
#endif
    };

    char baseServerUrl[112] = "";
    char resolvedHost[64] = "";
    uint16_t resolvedPort = 8890;
    {
        bool resolvedOnline = false;
        if (!net::ShowcaseDiscovery::resolveUrl(
                cfg.serverUrl,
                baseServerUrl,
                sizeof(baseServerUrl),
                resolvedHost,
                sizeof(resolvedHost),
                resolvedPort,
                resolvedOnline,
                nullptr,
                0,
                true,
                false) ||
            net::ShowcaseDiscovery::isAutoDiscoverableHost(resolvedHost)) {
            handleTransientHttpFailure();
            return;
        }
    }

    net::SecureHttpOptions httpOpts;
    httpOpts.ownerId = net::OWNER_SHOWCASE;
    httpOpts.requestTimeoutMs = 3500;
    httpOpts.handshakeTimeoutSec = 2;
    httpOpts.keepAlive = false;

    uint16_t pw = 32;
    uint16_t ph = 32;
    computeCardPosterDims(m_width, m_height, pw, ph);
    uint8_t effFps = resolveEffectiveVideoFps(cfg.videoFps, m_hasPsram);

    size_t pos = 0U;
    m_ioScratch[0] = '\0';
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "{\"client_id\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_sessionId);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"width\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_width);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"height\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_height);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"poster_width\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, pw);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"poster_height\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ph);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"systems\":");
    appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.systems);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"genres\":");
    appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.genres);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"decades\":");
    appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.decades);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_type\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoType);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"include_poster\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showPoster ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"include_synopsis\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showSynopsis ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"text_scroll_speed\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.textScrollSpeed);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"jacket_panorama\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.jacketPanorama ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"marquee_panorama\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.marqueePanorama ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"snapshot_panorama_count\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.snapshotPanoramaCount);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"show_video\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showVideo ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_fps\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, effFps);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_max_duration_s\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoMaxDurationS);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_mode\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoMode);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"color_boost\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.colorBoost);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"lang\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, reqLang);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\"}");

    {
        char cfgUrl[160];
        snprintf(cfgUrl, sizeof(cfgUrl), "%s/api/v1/%s/config", baseServerUrl, domain);
        auto cfgResp = net::SecureHttpClient::post(cfgUrl, "application/json", m_ioScratch, httpOpts);
        if (!cfgResp.ok()) {
            handleTransientHttpFailure();
            return;
        }
    }

    if (!m_wantActive.load(std::memory_order_acquire)) return;

    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

    char unifiedStreamPath[224] = "";
    char posterPathBuf[224] = "";
    CardSnapshot nextCard;
    bool parsedReady = false;
    bool serverWarming = false;
    {
        char nextUrl[192];
        snprintf(nextUrl, sizeof(nextUrl), "%s/api/v1/%s/next?client_id=%s", baseServerUrl, domain, m_sessionId);
        auto nextResp = net::SecureHttpClient::get(nextUrl, httpOpts);
        if (!nextResp.ok()) {
            handleTransientHttpFailure();
            return;
        }
        m_consecutiveNetFailures = 0;
        net::ShowcaseDiscovery::touchVerifiedHost(resolvedHost, resolvedPort);

        char* rxBuf = m_psramMqttRx ? m_psramMqttRx : reinterpret_cast<char*>(m_frame565);
        size_t rxCap = m_psramMqttRx ? PSRAM_MQTT_RX_BYTES : (m_frame565 ? (m_framePixels * sizeof(uint16_t)) : 0U);
        size_t respBytes = nextResp.contentLength();

        auto extractNextRoot = [&](JsonObjectConst rootObj) {
            const char* status = rootObj["status"] | "warming";
            JsonObjectConst item = rootObj["item"].as<JsonObjectConst>();
            if (strcmp(status, "ready") == 0 && !item.isNull()) {
                copySafeStr(nextCard.title, sizeof(nextCard.title), item["title"] | "UNKNOWN");
                copySafeStr(nextCard.year, sizeof(nextCard.year), item["year"] | "");
                copySafeStr(nextCard.badge, sizeof(nextCard.badge), item["badge"] | domain);
                copySafeStr(nextCard.rating, sizeof(nextCard.rating), item["rating"] | "");
                copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), item["synopsis"] | "");
                nextCard.badgeColor565 = parseHexColor565(item["badge_color"] | nullptr, defaultAccent565());
                nextCard.posterW = pw;
                nextCard.posterH = ph;
                nextCard.hasData = true;
                JsonObjectConst assetsObj = item["assets"].as<JsonObjectConst>();
                copySafeStr(unifiedStreamPath, sizeof(unifiedStreamPath), assetsObj["unified_stream_rgb565"] | "");
                copySafeStr(posterPathBuf, sizeof(posterPathBuf), assetsObj["poster_rgb565"] | (item["poster_rgb565_url"] | ""));
                const char* posterB64 = item["poster_rgb565_b64"] | "";
                size_t expectedPosterBytes = static_cast<size_t>(pw) * static_cast<size_t>(ph) * 2U;
                if (cfg.showPoster && m_poster565 && expectedPosterBytes <= m_posterCapBytes && posterB64[0] != '\0') {
                    if (decodeBase64Fixed(posterB64, reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes)) {
                        nextCard.hasPoster = true;
                    }
                }
                parsedReady = true;
            } else {
                serverWarming = true;
            }
        };

        const bool rxBorrowsFrame = (rxBuf == reinterpret_cast<char*>(m_frame565) && m_frame565 != nullptr);
        bool freeRxBufAfter = false;
#if defined(ESP32)
        if (!rxBuf && respBytes > 0U && respBytes <= 12288U && showcaseLargest8BitBlock() >= (respBytes + 2048U)) {
            rxBuf = static_cast<char*>(malloc(respBytes + 1U));
            if (rxBuf) {
                rxCap = respBytes + 1U;
                freeRxBufAfter = true;
            }
        }
#endif
        if (rxBuf && respBytes > 0U && (respBytes + 1U) <= rxCap) {
            if (rxBorrowsFrame && (m_frameSeq.load(std::memory_order_relaxed) & 1U) == 0U) {
                m_frameSeq.fetch_add(1, std::memory_order_acq_rel);
            }
            const bool readOk = readExactFromStream(nextResp.stream(), reinterpret_cast<uint8_t*>(rxBuf), respBytes, m_wantActive);
            if (readOk) {
                rxBuf[respBytes] = '\0';
                StaticJsonDocument<JSON_OBJECT_SIZE(44)> nextDoc;
                if (deserializeJson(nextDoc, rxBuf) == DeserializationError::Ok) {
                    extractNextRoot(nextDoc.as<JsonObjectConst>());
                }
            }
            if (rxBorrowsFrame) {
                const size_t totalFrameBytes = m_framePixels * sizeof(uint16_t);
                const size_t keepBytes = (readOk && m_posterAliasesFrame && nextCard.hasPoster)
                    ? std::min(totalFrameBytes, m_posterCapBytes)
                    : 0U;
                if (totalFrameBytes > keepBytes) {
                    memset(reinterpret_cast<uint8_t*>(m_frame565) + keepBytes, 0, totalFrameBytes - keepBytes);
                }
                if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
                    m_frameSeq.fetch_add(1, std::memory_order_release);
                }
            }
            if (freeRxBufAfter) {
                free(rxBuf);
                rxBuf = nullptr;
            }
        } else {
            if (freeRxBufAfter && rxBuf) {
                free(rxBuf);
                rxBuf = nullptr;
            }
            DynamicJsonDocument nextDoc(m_hasPsram ? 24576 : 6144);
            if (deserializeJson(nextDoc, nextResp.stream()) == DeserializationError::Ok) {
                extractNextRoot(nextDoc.as<JsonObjectConst>());
            }
        }
    }

    if (!parsedReady) {
        if (serverWarming) {
            m_statusState.store(1, std::memory_order_release);
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
        } else {
            handleTransientHttpFailure();
        }
        return;
    }

    size_t expectedPosterBytes = static_cast<size_t>(pw) * static_cast<size_t>(ph) * 2U;
    if (cfg.showPoster && !nextCard.hasPoster && m_poster565 && expectedPosterBytes <= m_posterCapBytes && posterPathBuf[0] != '\0') {
        char posterUrl[288];
        if (strncmp(posterPathBuf, "http", 4) == 0) {
            copySafeStr(posterUrl, sizeof(posterUrl), posterPathBuf);
        } else {
            snprintf(posterUrl, sizeof(posterUrl), "%s%s", baseServerUrl, posterPathBuf);
        }
        auto pResp = net::SecureHttpClient::get(posterUrl, httpOpts);
        if (pResp.ok() && readExactFromStream(pResp.stream(), reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes, m_wantActive)) {
            nextCard.hasPoster = true;
        }
    }

    m_seq.fetch_add(1, std::memory_order_acq_rel);
    m_liveCard = nextCard;
    m_seq.fetch_add(1, std::memory_order_release);
    m_statusState.store(0, std::memory_order_release);
    m_phaseStartedAtMs = millis();
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

    uint32_t synGlyphs = static_cast<uint32_t>(countUtf8Glyphs(nextCard.synopsis));
    uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
    uint32_t cardWaitMs = 4500U;
    if (cfg.showSynopsis && synGlyphs > 0U) {
        uint32_t textW = synGlyphs * 6U;
        uint32_t availW = static_cast<uint32_t>(
            engines::showcase::ShowcaseCardRenderer::computeCardTextViewportWidth(m_width, m_height, cfg.showPoster)
        );
        if (textW > availW) {
            uint32_t scrollDistPx = (textW - availW) + (availW / 2U);
            uint32_t synScrollMs = 900U + (scrollDistPx * 1000U) / speedPx + 700U;
            if (synScrollMs > cardWaitMs) cardWaitMs = synScrollMs;
        }
    }
    uint32_t startGen = m_cfgGen.load(std::memory_order_acquire);
    uint32_t t0 = millis();
    while (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen && (millis() - t0) < cardWaitMs) {
#if defined(ESP32)
        vTaskDelay(pdMS_TO_TICKS(40));
#else
        break;
#endif
    }

    if (m_wantActive.load(std::memory_order_acquire) &&
        m_cfgGen.load(std::memory_order_acquire) == startGen &&
        unifiedStreamPath[0] != '\0') {
        streamUnifiedRgb565Http(baseServerUrl, unifiedStreamPath);
    }

    finishTurnItem();
}

bool MediaShowcaseBase::streamUnifiedRgb565Http(const char* serverUrl, const char* streamPath) {
    if (!serverUrl || !streamPath || streamPath[0] == '\0') {
        return false;
    }

#if defined(ESP32) && !defined(UNIT_TEST)
    if (!m_ioScratch) {
        return false;
    }
    struct StepCFrameGuard {
        MediaShowcaseBase* self;
        bool releaseOnExit;
        StepCFrameGuard(MediaShowcaseBase* s, bool rel) : self(s), releaseOnExit(rel) {}
        ~StepCFrameGuard() {
            if (releaseOnExit && self) {
                self->releaseStepCFrameBuffer();
            }
        }
    };
    const bool acquiredTempFrame = (!m_hasPsram && m_frame565 == nullptr) ? acquireStepCFrameBuffer() : false;
    StepCFrameGuard stepCGuard(this, acquiredTempFrame);
#endif
    if (!m_frame565) {
        return false;
    }

    m_presentedStreamFrames.store(0, std::memory_order_release);
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];

    struct ShowcaseStreamCtx {
        MediaShowcaseBase* self;
        uint32_t startGen;
    } ctx{this, m_cfgGen.load(std::memory_order_acquire)};

    net::DmdzStreamParams params;
    params.serverUrl = serverUrl;
    params.streamPath = streamPath;
    params.clientId = m_sessionId;
    params.logTag = "Showcase";
    params.ownerId = net::OWNER_SHOWCASE;
    params.wsCredits = m_hasPsram ? 12U : 4U;
    params.panelWidth = m_width;
    params.panelHeight = m_height;
    params.configuredStreamMs = configuredMaxStreamMs(cfg);
    params.maxFramePixels = m_framePixels;
    params.frameBuf0 = m_frame565;
    params.frameBuf1 = m_frame565Back;
    params.ioScratch = m_ioScratch;
    params.ioScratchCap = m_ioScratch ? m_ioScratchCap : 0U;
    params.userCtx = &ctx;

    params.checkStop = [](void* raw) -> net::StreamStopReason {
        const auto* c = static_cast<const ShowcaseStreamCtx*>(raw);
        if (!c->self->m_wantActive.load(std::memory_order_acquire)) {
            return net::StreamStopReason::Deactivated;
        }
        if (shouldYieldShowcaseToWebUi(c->self->m_hasPsram)) {
            return net::StreamStopReason::WebUiYield;
        }
        if (c->self->m_cfgGen.load(std::memory_order_acquire) != c->startGen) {
            return net::StreamStopReason::ConfigChanged;
        }
        return net::StreamStopReason::Continue;
    };

    params.onBeginFrameWrite = [](void* raw, uint16_t frameIdx, uint16_t* dstBuf, bool singleBuffer, uint32_t safeFps) {
        auto* self = static_cast<ShowcaseStreamCtx*>(raw)->self;
        if (singleBuffer) {
#if defined(ESP32) && !defined(UNIT_TEST)
            if (frameIdx > 0U) {
                const uint32_t handoffBudgetMs = std::max<uint32_t>(18U, 1000U / safeFps);
                const uint32_t handoffT0 = millis();
                while (self->m_wantActive.load(std::memory_order_acquire) &&
                       !shouldYieldShowcaseToWebUi(self->m_hasPsram) &&
                       self->m_consumedFrameSeq.load(std::memory_order_acquire) != self->m_frameSeq.load(std::memory_order_relaxed) &&
                       (millis() - handoffT0) < handoffBudgetMs) {
                    vTaskDelay(1);
                }
            }
#else
            (void)safeFps;
#endif
            if ((self->m_frameSeq.load(std::memory_order_relaxed) & 1U) == 0U) {
                self->m_frameSeq.fetch_add(1U, std::memory_order_acq_rel);
            }
            // Wait for any in-flight Core 1 `blit565` to finish before mutating `dstBuf` in-place.
            self->waitRenderBlitQuiescent();
        }
        if (frameIdx == 0U && self->m_posterAliasesFrame) {
            if (self->m_liveCard.hasPoster) {
                self->m_seq.fetch_add(1, std::memory_order_acq_rel);
                self->m_liveCard.hasPoster = false;
                self->m_seq.fetch_add(1, std::memory_order_release);
            }
            const size_t maxBufBytes = self->m_framePixels * sizeof(uint16_t);
            memset(dstBuf, 0, std::min(maxBufBytes, self->m_posterCapBytes));
        }
    };

    params.onFinishFrameWrite = [](void* raw, uint16_t /*frameIdx*/, const uint16_t* dstBuf, uint16_t /*w*/, uint16_t /*h*/, bool okFrame, bool singleBuffer) {
        auto* self = static_cast<ShowcaseStreamCtx*>(raw)->self;
        if (!okFrame) {
            if (singleBuffer) {
                self->m_publishedFramePtr.store(nullptr, std::memory_order_release);
                if (self->m_liveCard.hasVideoFrame) {
                    self->m_seq.fetch_add(1U, std::memory_order_acq_rel);
                    self->m_liveCard.hasVideoFrame = false;
                    self->m_seq.fetch_add(1U, std::memory_order_release);
                }
                if ((self->m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
                    self->m_frameSeq.fetch_sub(1U, std::memory_order_release);
                }
            }
            return;
        }
        self->m_publishedFramePtr.store(dstBuf, std::memory_order_release);
        const uint32_t curSeq = self->m_frameSeq.load(std::memory_order_relaxed);
        self->m_frameSeq.store((curSeq & 1U) ? (curSeq + 1U) : (curSeq + 2U), std::memory_order_release);
        if (!self->m_liveCard.hasVideoFrame) {
            self->m_seq.fetch_add(1, std::memory_order_acq_rel);
            self->m_liveCard.hasVideoFrame = true;
            self->m_seq.fetch_add(1, std::memory_order_release);
        }
        if (self->m_phase.load(std::memory_order_relaxed) != static_cast<uint8_t>(Phase::LiveStream)) {
            self->m_phase.store(static_cast<uint8_t>(Phase::LiveStream), std::memory_order_release);
        }
    };

    params.getPresentedFrames = [](void* raw) -> uint32_t {
        return static_cast<const ShowcaseStreamCtx*>(raw)->self->m_presentedStreamFrames.load(std::memory_order_acquire);
    };

    return net::DmdzStreamClient::stream(params);
}

void MediaShowcaseBase::drawWaitingPixelArt(IDrawingSurface* s, int mW, int mH, uint32_t elapsedMs) {
    engines::showcase::ShowcaseCardRenderer::drawWaitingPixelArt(
        s,
        m_domain,
        m_statusState.load(std::memory_order_relaxed),
        mW,
        mH,
        elapsedMs
    );
}

void MediaShowcaseBase::drawCardPhase(IDrawingSurface* s, const CardSnapshot& card, uint32_t elapsedMs) {
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    engines::showcase::ShowcaseCardRenderer::drawCardPhase(
        s,
        m_domain,
        m_statusState.load(std::memory_order_relaxed),
        card,
        m_poster565,
        cfg.showPoster,
        cfg.showSynopsis,
        m_titleStepPx,
        m_synScrollPx,
        elapsedMs
    );
}

void MediaShowcaseBase::update(EngineContext* context) {
    (void)context;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const auto& snap = guard.get();
    m_rotationSlotCount.store(static_cast<uint8_t>(snap.rotation.size()), std::memory_order_relaxed);

    uint32_t now = millis();
    uint32_t s1 = m_seq.load(std::memory_order_acquire);
    if ((s1 & 1U) == 0U && s1 != m_lastRenderedCardSeq) {
        CardSnapshot temp = m_liveCard;
        uint32_t s2 = m_seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            m_renderCard = temp;
            m_lastRenderedCardSeq = s1;
            m_cardAnimStartMs = now;
            m_cardLastTickMs = now;
            m_synSubpixelRem = 0U;
            m_synScrollPx = 0U;
            m_titleSubpixelRem = 0U;
            m_titleStepPx = 0U;
            m_dirty = true;
        }
    }

    uint8_t curPhase = m_phase.load(std::memory_order_acquire);
    if (curPhase != m_lastRenderedPhase) {
        m_lastRenderedPhase = curPhase;
        m_cardLastTickMs = now;
        m_dirty = true;
    }

    if (curPhase == static_cast<uint8_t>(Phase::LiveStream)) {
        uint32_t fSeq = m_frameSeq.load(std::memory_order_acquire);
        if ((fSeq & 1U) == 0U && fSeq != m_lastRenderedFrameSeq && m_renderCard.hasVideoFrame) {
            m_dirty = true;
        } else if (!m_renderCard.hasVideoFrame && (now - m_lastScrollStepMs) >= 90U) {
            m_lastScrollStepMs = now;
            m_dirty = true;
        }
    } else if (!m_renderCard.hasData) {
        if ((now - m_lastScrollStepMs) >= 30U) {
            m_lastScrollStepMs = now;
            m_dirty = true;
        }
    } else {
        uint32_t dtMs = now - m_cardLastTickMs;
        if (dtMs > 0U) {
            m_cardLastTickMs = now;
            uint32_t frameUnits = (dtMs <= 24U)
                ? 1U
                : std::max<uint32_t>(1U, (std::min<uint32_t>(dtMs, 2000U) * 60U + 500U) / 1000U);

            uint32_t prevTitleStep = m_titleStepPx;
            uint32_t prevSynPx = m_synScrollPx;

            m_titleSubpixelRem += frameUnits * 30U;
            m_titleStepPx += m_titleSubpixelRem / 60U;
            m_titleSubpixelRem %= 60U;

            const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
            if (cfg.showSynopsis && m_renderCard.synopsis[0] != '\0' && (now - m_cardAnimStartMs) >= 900U) {
                uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
                m_synSubpixelRem += frameUnits * speedPx;
                m_synScrollPx += m_synSubpixelRem / 60U;
                m_synSubpixelRem %= 60U;
            }

            if (m_titleStepPx != prevTitleStep || m_synScrollPx != prevSynPx) {
                m_dirty = true;
            } else if ((now - m_lastScrollStepMs) >= 250U) {
                m_lastScrollStepMs = now;
                m_dirty = true;
            }
        }
    }
}

void MediaShowcaseBase::render(EngineContext* context) {
    if (!m_dirty) {
        m_renderedNewFrame = false;
        return;
    }
    IDrawingSurface* s = (context && context->getSurface()) ? context->getSurface() : m_surface;
    if (!s) {
        m_renderedNewFrame = false;
        return;
    }

    struct BlitSeqGuard {
        std::atomic<uint32_t>& seq;
        explicit BlitSeqGuard(std::atomic<uint32_t>& sRef) : seq(sRef) {
            seq.fetch_add(1U, std::memory_order_seq_cst);
        }
        ~BlitSeqGuard() {
            seq.fetch_add(1U, std::memory_order_release);
        }
    } blitGuard{m_renderBlitSeq};

    uint32_t elapsedMs = millis() - m_activatedAtMs;
    Phase curPhase = static_cast<Phase>(m_phase.load(std::memory_order_acquire));

    if (curPhase == Phase::LiveStream) {
        const uint16_t* framePtr = m_publishedFramePtr.load(std::memory_order_acquire);
        if (m_renderCard.hasVideoFrame && framePtr != nullptr) {
            uint32_t fSeq1 = m_frameSeq.load(std::memory_order_acquire);
            if ((fSeq1 & 1U) != 0U) {
                m_renderedNewFrame = false;
                return;
            }
            if (s->width() != static_cast<int>(m_width) || s->height() != static_cast<int>(m_height)) {
                s->clear(0);
            }
            s->blit565(framePtr, 0, 0, m_width, m_height);
            m_lastRenderedFrameSeq = fSeq1;
            m_consumedFrameSeq.store(fSeq1, std::memory_order_release);
            m_presentedStreamFrames.fetch_add(1, std::memory_order_relaxed);
        } else {
            s->clear(0);
            if (m_renderCard.hasData) {
                drawCardPhase(s, m_renderCard, elapsedMs);
            } else {
                drawWaitingPixelArt(s, s->width(), s->height(), elapsedMs);
            }
        }
    } else {
        if (m_posterAliasesFrame && m_renderCard.hasPoster &&
            (m_frameSeq.load(std::memory_order_acquire) & 1U) != 0U) {
            m_renderedNewFrame = false;
            return;
        }
        s->clear(0);
        drawCardPhase(s, m_renderCard, elapsedMs);
    }

    m_dirty = false;
    m_renderedNewFrame = true;
}

EngineDescriptor RetroGamesEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    initShowcaseCommonDescriptor(desc, "retro_games", "Retro Games Showcase", "arcade");

    static constexpr FieldTableEntry kFields[] = {
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width cover/jacket from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Marquee / Banner Panorama", "Scroll the arcade marquee or title banner from top to bottom after the jacket", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional gameplay snapshots to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("systems", ConfigType::LIST, "Arcade & Console Systems", "Select one or more systems dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept),
        ConfigField("genres", ConfigType::LIST, "Game Genres", "Select one or more genres dynamically filtered by your chosen systems (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically filtered by your chosen systems (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept)
    };
    desc.schema.fields.assign(kFields, kFields + (sizeof(kFields) / sizeof(kFields[0])));
    desc.factory = []() { return std::unique_ptr<IEngine>(new RetroGamesEngine()); };
    return desc;
}

EngineDescriptor MovieShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    initShowcaseCommonDescriptor(desc, "movie_showcase", "Movie Showcase", "media");

    static constexpr FieldTableEntry kFields[] = {
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width movie poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the movie backdrop banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional movie stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Movie Genres", "Select one or more movie genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/movies/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/movies/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (trailer,scene,any)", "any", false, "", "", "", "any,trailer,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.schema.fields.assign(kFields, kFields + (sizeof(kFields) / sizeof(kFields[0])));
    desc.factory = []() { return std::unique_ptr<IEngine>(new MovieShowcaseEngine()); };
    return desc;
}

EngineDescriptor SeriesShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    initShowcaseCommonDescriptor(desc, "series_showcase", "Series Showcase", "media");

    static constexpr FieldTableEntry kFields[] = {
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width series poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the series banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional episode stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Series Genres", "Select one or more series genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/series/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/series/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,trailer,any)", "any", false, "", "", "", "any,opening,trailer", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.schema.fields.assign(kFields, kFields + (sizeof(kFields) / sizeof(kFields[0])));
    desc.factory = []() { return std::unique_ptr<IEngine>(new SeriesShowcaseEngine()); };
    return desc;
}

EngineDescriptor AnimeShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    initShowcaseCommonDescriptor(desc, "anime_showcase", "Anime Showcase", "media");

    static constexpr FieldTableEntry kFields[] = {
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width anime key visual from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the anime banner from top to bottom after the key visual", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional anime stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Anime Genres", "Select one or more anime genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/anime/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/anime/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,ending,scene,any)", "opening", false, "", "", "", "any,opening,ending,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.schema.fields.assign(kFields, kFields + (sizeof(kFields) / sizeof(kFields[0])));
    desc.factory = []() { return std::unique_ptr<IEngine>(new AnimeShowcaseEngine()); };
    return desc;
}
