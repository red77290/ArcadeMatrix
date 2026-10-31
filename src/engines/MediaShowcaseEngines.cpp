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
#include <new>
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
    size_t i = 0U;
    while (i + 1U < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
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

#if !defined(ESP32) || defined(UNIT_TEST) || defined(BOARD_HAS_PSRAM)
#define SHOWCASE_SUPPORT_PSRAM_PRELOAD 1
#else
#define SHOWCASE_SUPPORT_PSRAM_PRELOAD 0
#endif

/**
 * @brief Zero-heap `ArduinoJson` allocator backed by caller-owned `m_ioScratch`.
 */
struct ScratchSingleBlockAllocator {
    void* buf = nullptr;
    size_t cap = 0U;
    bool used = false;

    ScratchSingleBlockAllocator() = default;
    ScratchSingleBlockAllocator(void* buffer, size_t capacity, bool initiallyUsed = false)
        : buf(buffer), cap(capacity), used(initiallyUsed) {}

    void* allocate(size_t size) {
        if (!used && buf && size <= cap) {
            used = true;
            return buf;
        }
        return nullptr;
    }

    void deallocate(void* ptr) {
        if (ptr == buf) {
            used = false;
        }
    }

    void* reallocate(void* ptr, size_t newSize) {
        return (ptr == buf && newSize <= cap) ? buf : nullptr;
    }
};

void extractShowcaseItemFromJson(
    JsonObjectConst rootObj,
    JsonObjectConst itemObj,
    const char* domain,
    const char* sessionId,
    uint16_t panelW,
    uint16_t panelH,
    size_t posterCapBytes,
    uint16_t fallbackAccent565,
    engines::showcase::ShowcaseCardData& outCard,
    char* outItemId,
    size_t itemIdCap,
    char* outPosterPath,
    size_t posterPathCap,
    char* outStreamPath,
    size_t streamPathCap,
    bool& outHasStream,
    uint32_t& outStreamMaxMs
) {
    copySafeStr(outCard.title, sizeof(outCard.title), itemObj["title"] | "UNKNOWN");
    copySafeStr(outCard.year, sizeof(outCard.year), itemObj["year"] | "");
    copySafeStr(outCard.badge, sizeof(outCard.badge), itemObj["badge"] | domain);
    copySafeStr(outCard.rating, sizeof(outCard.rating), itemObj["rating"] | "");
    copySafeStr(outCard.synopsis, sizeof(outCard.synopsis), itemObj["synopsis"] | "");
    outCard.badgeColor565 = parseHexColor565(itemObj["badge_color"] | nullptr, fallbackAccent565);
    engines::showcase::ShowcaseCardRenderer::computeCardPosterDims(panelW, panelH, outCard.posterW, outCard.posterH);
    const uint16_t srvPw = itemObj["poster_width"] | 0;
    const uint16_t srvPh = itemObj["poster_height"] | 0;
    if (srvPw > 0U && srvPh > 0U &&
        (static_cast<size_t>(srvPw) * static_cast<size_t>(srvPh) * 2U) <= posterCapBytes) {
        outCard.posterW = srvPw;
        outCard.posterH = srvPh;
    }
    outCard.hasData = true;
    outCard.hasPoster = false;
    outCard.hasVideoFrame = false;

    const char* rawItemId = rootObj["item_id"] | (itemObj["item_id"] | (itemObj["id"] | ""));
    if (outItemId && itemIdCap > 0U) {
        copySafeStr(outItemId, itemIdCap, rawItemId);
    }

    JsonObjectConst assetsObj = itemObj["assets"].as<JsonObjectConst>();
    if (outPosterPath && posterPathCap > 0U) {
        outPosterPath[0] = '\0';
        const char* explicitPoster = assetsObj["poster_rgb565"] | (itemObj["poster_rgb565_url"] | "");
        if (explicitPoster[0] != '\0') {
            copySafeStr(outPosterPath, posterPathCap, explicitPoster);
        } else if (rawItemId[0] != '\0' && sessionId && sessionId[0] != '\0') {
            snprintf(
                outPosterPath,
                posterPathCap,
                "/api/v1/%s/poster/%s.rgb565?client_id=%s",
                domain,
                rawItemId,
                sessionId
            );
        }
    }

    const char* explicitStream = assetsObj["unified_stream_rgb565"] |
                                 (rootObj["stream_url"] | (itemObj["stream_url"] | ""));
    outHasStream = (explicitStream[0] != '\0') ||
                   (itemObj["has_panoramas"] | false) ||
                   (itemObj["has_video"] | false);
    if (outStreamPath && streamPathCap > 0U) {
        outStreamPath[0] = '\0';
        if (explicitStream[0] != '\0') {
            copySafeStr(outStreamPath, streamPathCap, explicitStream);
        } else if (outHasStream && rawItemId[0] != '\0') {
            snprintf(
                outStreamPath,
                streamPathCap,
                "/api/v1/%s/stream/%s.rgb565.stream",
                domain,
                rawItemId
            );
        }
    }

    float totDur = itemObj["stream_duration_s"] |
                   (itemObj["total_stream_duration_s"] | (itemObj["duration_s"] | 8.0f));
    if (totDur < 2.0f) {
        totDur = 2.0f;
    }
    outStreamMaxMs = static_cast<uint32_t>((totDur + 4.0f) * 1000.0f);
}

void appendShowcaseCommonConfigJson(
    char* dst,
    size_t cap,
    size_t& pos,
    const char* sysCsv,
    const char* genCsv,
    const char* decCsv,
    const char* videoType,
    bool showPoster,
    bool showSynopsis,
    bool httpFieldNames,
    const char* textScrollSpeed,
    bool jacketPanorama,
    bool marqueePanorama,
    uint8_t snapshotPanoramaCount,
    bool showVideo,
    uint8_t videoMaxDurationS,
    uint8_t effFps,
    const char* videoMode,
    const char* colorBoost
) {
    appendRawToBuf(dst, cap, pos, "\"systems\":");
    appendCsvAsJsonArrayToBuf(dst, cap, pos, sysCsv);
    appendRawToBuf(dst, cap, pos, ",\"genres\":");
    appendCsvAsJsonArrayToBuf(dst, cap, pos, genCsv);
    appendRawToBuf(dst, cap, pos, ",\"decades\":");
    appendCsvAsJsonArrayToBuf(dst, cap, pos, decCsv);
    appendRawToBuf(dst, cap, pos, ",\"video_type\":\"");
    appendJsonEscapedToBuf(dst, cap, pos, videoType);
    if (httpFieldNames) {
        appendRawToBuf(dst, cap, pos, "\",\"include_poster\":");
        appendRawToBuf(dst, cap, pos, showPoster ? "true" : "false");
        appendRawToBuf(dst, cap, pos, ",\"include_synopsis\":");
    } else {
        appendRawToBuf(dst, cap, pos, "\",\"show_poster\":");
        appendRawToBuf(dst, cap, pos, showPoster ? "true" : "false");
        appendRawToBuf(dst, cap, pos, ",\"show_synopsis\":");
    }
    appendRawToBuf(dst, cap, pos, showSynopsis ? "true" : "false");
    appendRawToBuf(dst, cap, pos, ",\"text_scroll_speed\":\"");
    appendJsonEscapedToBuf(dst, cap, pos, textScrollSpeed);
    appendRawToBuf(dst, cap, pos, "\",\"jacket_panorama\":");
    appendRawToBuf(dst, cap, pos, jacketPanorama ? "true" : "false");
    appendRawToBuf(dst, cap, pos, ",\"marquee_panorama\":");
    appendRawToBuf(dst, cap, pos, marqueePanorama ? "true" : "false");
    appendRawToBuf(dst, cap, pos, ",\"snapshot_panorama_count\":");
    appendUintToBuf(dst, cap, pos, snapshotPanoramaCount);
    appendRawToBuf(dst, cap, pos, ",\"show_video\":");
    appendRawToBuf(dst, cap, pos, showVideo ? "true" : "false");
    appendRawToBuf(dst, cap, pos, ",\"video_max_duration_s\":");
    appendUintToBuf(dst, cap, pos, videoMaxDurationS);
    appendRawToBuf(dst, cap, pos, ",\"video_fps\":");
    appendUintToBuf(dst, cap, pos, effFps);
    appendRawToBuf(dst, cap, pos, ",\"video_mode\":\"");
    appendJsonEscapedToBuf(dst, cap, pos, videoMode);
    appendRawToBuf(dst, cap, pos, "\",\"color_boost\":\"");
    appendJsonEscapedToBuf(dst, cap, pos, colorBoost);
    appendCharToBuf(dst, cap, pos, '"');
}

void populateShowcaseItemSubFilter(JsonObject obj) {
    obj["id"] = true;
    obj["item_id"] = true;
    obj["title"] = true;
    obj["year"] = true;
    obj["badge"] = true;
    obj["badge_color"] = true;
    obj["rating"] = true;
    obj["synopsis"] = true;
    obj["poster_width"] = true;
    obj["poster_height"] = true;
    obj["poster_rgb565_url"] = true;
    obj["has_panoramas"] = true;
    obj["has_video"] = true;
    obj["stream_duration_s"] = true;
    obj["total_stream_duration_s"] = true;
    obj["duration_s"] = true;
    obj["stream_url"] = true;
    JsonObject assets = obj.createNestedObject("assets");
    assets["poster_rgb565"] = true;
    assets["unified_stream_rgb565"] = true;
}

void buildShowcaseMqttReadyFilter(JsonDocument& filter) {
    filter.clear();
    filter["ready"] = true;
    filter["item_id"] = true;
    filter["stream_url"] = true;
    populateShowcaseItemSubFilter(filter.createNestedObject("card"));
}

void buildShowcaseHttpNextFilter(JsonDocument& filter) {
    filter.clear();
    filter["status"] = true;
    filter["item_id"] = true;
    populateShowcaseItemSubFilter(filter.createNestedObject("item"));
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
inline size_t showcaseFree8BitHeap() {
    return 131072U;
}

inline size_t showcaseLargest8BitBlock() {
    return 65536U;
}

inline bool shouldYieldShowcaseToWebUi(bool) {
    return false;
}
#endif

static constexpr FieldTableEntry kCommonShowcaseFields[] = {
    ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
    ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width cover/poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
    ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the marquee or backdrop banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
    ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional stills/snapshots to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
    ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
    ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
    ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
    ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
    ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
    ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
    ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
    ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault)
};

void initShowcaseCommonDescriptor(
    EngineDescriptor& desc,
    const char* id,
    const char* name,
    const char* category,
    const FieldTableEntry* extraFields,
    size_t extraCount
) {
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

    constexpr size_t kCommonCount = sizeof(kCommonShowcaseFields) / sizeof(kCommonShowcaseFields[0]);
    desc.schema.fields.reserve(kCommonCount + extraCount);
    desc.schema.fields.assign(kCommonShowcaseFields, kCommonShowcaseFields + kCommonCount);
    if (extraFields && extraCount > 0U) {
        desc.schema.fields.insert(desc.schema.fields.end(), extraFields, extraFields + extraCount);
    }
}

void copyCfgFieldCStr(const EngineConfig* cfg, const char* key, const char* defVal, char* dst, size_t cap, bool csvTokens = false) {
    String val = cfg->getString(key, defVal);
    if (csvTokens) {
        MediaShowcaseBase::copySafeCsvTokens(dst, cap, val.c_str());
    } else {
        copySafeStr(dst, cap, val.c_str());
    }
}

} // namespace

MediaShowcaseBase::MediaShowcaseBase(ShowcaseDomainId domain) : m_domain(domain) {
    const char* slug = domainSlug();
    net::ShowcaseDiscovery::formatClientId(slug, m_width, m_height, false, m_sessionId, sizeof(m_sessionId));
    m_cfgStreamBaseMs.store(configuredMaxStreamMs(m_cfg), std::memory_order_relaxed);
    m_cfgSynopsisSpeedPx.store(static_cast<uint16_t>(synopsisPxPerSec(m_cfg.textScrollSpeed)), std::memory_order_relaxed);
    m_liveCard.badgeColor565 = defaultAccent565();
    m_liveCard.hasData = false;
    m_renderCard = m_liveCard;
}

MediaShowcaseBase::~MediaShowcaseBase() {
    shutdownForDestruction();
    if (m_cfgExtras) {
        free(m_cfgExtras);
        m_cfgExtras = nullptr;
    }
}

void MediaShowcaseBase::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    uint16_t newW = geometry.width > 0 ? geometry.width : 128;
    uint16_t newH = geometry.height > 0 ? geometry.height : 32;
    if (newW != m_width || newH != m_height) {
        m_width = newW;
        m_height = newH;
        m_lastPublishedCfgGen = 0;
        m_turnEvaluated.store(false, std::memory_order_release);
        m_readyStateReceived.store(false, std::memory_order_release);
        m_hasReadyItem.store(false, std::memory_order_release);
        m_mqttConnectedAtMs.store(0U, std::memory_order_release);
        m_skipTurnImmediately.store(false, std::memory_order_release);
        m_turnDone.store(false, std::memory_order_release);
        m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
        clearLiveCardState();
        m_cfgGen.fetch_add(1, std::memory_order_acq_rel);
    }
    m_streamPlayer.onGeometryChanged(geometry.rotation);
    m_streamPlayer.refreshZeroCopyFromSurfaceReadOnly(m_surface, m_width, m_height);
    m_dirty = true;
}

bool MediaShowcaseBase::resolveMqttBroker(
    const ConfigSlot& cfg,
    const ConfigExtras* extras,
    char* outHost,
    size_t hostCap,
    uint16_t& outPort
) {
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
    const char* customUrl = extras ? extras->mqttCustomUrl : "";
    if (strcasecmp(cfg.mqttBrokerSource, "custom") == 0 && customUrl[0] != '\0') {
        if (net::parseEndpointHostAndPort(customUrl, 1883, outHost, hostCap, outPort)) {
            if (net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
                return true;
            }
            return false;
        }
    }
    const char* globalHost = extras ? extras->globalMqttHost : "";
    if (globalHost[0] != '\0') {
        copySafeStr(outHost, hostCap, globalHost);
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

void MediaShowcaseBase::probeZeroCopyCanvasEligibility() {
    (void)m_streamPlayer.probeZeroCopyOnCore1(m_surface, m_width, m_height);
}

bool MediaShowcaseBase::allocateBuffers() {
    freeBuffers();
    uint16_t posterW = 32;
    uint16_t posterH = 32;
    computeCardPosterDims(m_width, m_height, posterW, posterH);
    size_t posterBytes = static_cast<size_t>(posterW) * static_cast<size_t>(posterH) * sizeof(uint16_t);
    if (posterBytes == 0U) posterBytes = 32U * 32U * sizeof(uint16_t);

    m_framePixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    m_posterAliasesFrame = false;

#if defined(ESP32)
    m_hasPsram = hardwareHAL.capabilities().hasPsram;
#if SHOWCASE_SUPPORT_PSRAM_PRELOAD
    if (m_hasPsram) {
        m_poster565 = static_cast<uint16_t*>(heap_caps_malloc(posterBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_ioScratch = static_cast<char*>(heap_caps_malloc(PSRAM_IO_SCRATCH_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (m_ioScratch) {
            m_ioScratchCap = PSRAM_IO_SCRATCH_BYTES;
        } else {
            m_ioScratch = static_cast<char*>(malloc(IO_SCRATCH_BYTES));
            m_ioScratchCap = m_ioScratch ? IO_SCRATCH_BYTES : 0U;
        }
        (void)m_streamPlayer.ensureDecoderAllocated(true);
        if (!m_poster565) {
            m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
        }
        if (!m_streamPlayer.isZeroCopyEligible()) {
            (void)m_streamPlayer.acquireFallbackFrameBuffer(m_width, m_height, true);
        }
    } else
#endif
    {
        const size_t free8 = showcaseFree8BitHeap();
        const size_t scratchCandidates[3] = {
            IO_SCRATCH_BYTES,
            FALLBACK_IO_SCRATCH_BYTES,
            MIN_IO_SCRATCH_BYTES
        };
        const size_t headroom[3] = {16384U, 8192U, 0U};
        for (size_t i = 0U; i < 3U && !m_ioScratch; ++i) {
            const size_t cand = scratchCandidates[i];
            if (headroom[i] == 0U || free8 >= (posterBytes + cand + headroom[i])) {
                m_ioScratch = static_cast<char*>(malloc(cand));
                if (m_ioScratch) {
                    m_ioScratchCap = cand;
                }
            }
        }
        (void)m_streamPlayer.ensureDecoderAllocated(false);
        m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
    }
#else
    m_hasPsram = true;
    m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
    m_ioScratch = static_cast<char*>(malloc(PSRAM_IO_SCRATCH_BYTES));
    m_ioScratchCap = m_ioScratch ? PSRAM_IO_SCRATCH_BYTES : 0U;
    (void)m_streamPlayer.ensureDecoderAllocated(true);
    if (!m_streamPlayer.isZeroCopyEligible()) {
        (void)m_streamPlayer.acquireFallbackFrameBuffer(m_width, m_height, true);
    }
#endif

    m_posterCapBytes = posterBytes;
    if (m_poster565) {
        memset(m_poster565, 0, posterBytes);
    }
    if (m_ioScratch) {
        m_ioScratch[0] = '\0';
    }
    return (m_poster565 != nullptr && m_ioScratch != nullptr);
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

void MediaShowcaseBase::blankCardBeforeStream() {
    if (m_phase.load(std::memory_order_acquire) != static_cast<uint8_t>(Phase::Card)) {
        return;
    }
    const uint32_t prevBlank = m_blankPresentedSeq.load(std::memory_order_acquire);
    m_phase.store(static_cast<uint8_t>(Phase::StreamBlank), std::memory_order_release);
#if defined(ESP32) && !defined(UNIT_TEST)
    const uint32_t t0 = millis();
    while (m_wantActive.load(std::memory_order_acquire) &&
           !shouldYieldShowcaseToWebUi(m_hasPsram) &&
           m_blankPresentedSeq.load(std::memory_order_acquire) == prevBlank &&
           (millis() - t0) < 40U) {
        vTaskDelay(1);
    }
#else
    (void)prevBlank;
#endif
    waitRenderBlitQuiescent();
}

void MediaShowcaseBase::freePosterAfterBlank() {
    blankCardBeforeStream();
    if (m_liveCard.hasPoster) {
        m_seq.fetch_add(1U, std::memory_order_acq_rel);
        m_liveCard.hasPoster = false;
        m_seq.fetch_add(1U, std::memory_order_release);
    }
    if (m_poster565 && !m_posterAliasesFrame) {
        uint16_t* oldPoster = m_poster565;
        m_poster565 = nullptr;
        waitRenderBlitQuiescent();
        free(oldPoster);
    }
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
        freePosterAfterBlank();
    }
#if SHOWCASE_SUPPORT_PSRAM_PRELOAD
    if (m_hasPsram) {
        uint16_t* pBuf = static_cast<uint16_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (pBuf) {
            memset(pBuf, 0, frameBytes);
            m_frame565 = pBuf;
            return true;
        }
    }
#endif
    if (!m_hasPsram &&
        (frameBytes > 16384U ||
         showcaseFree8BitHeap() < (frameBytes + 2560U) ||
         showcaseLargest8BitBlock() < frameBytes)) {
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
    m_nextStreamFrameDueMs.store(0U, std::memory_order_relaxed);
    if (!m_frame565 || m_hasPsram || m_posterAliasesFrame) {
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
}

void MediaShowcaseBase::freeBuffers() {
    m_streamPlayer.resetCore1StreamState();
    m_nextStreamFrameDueMs.store(0U, std::memory_order_relaxed);
    m_publishedFramePtr.store(nullptr, std::memory_order_release);
    waitRenderBlitQuiescent();
    m_streamPlayer.releaseAll();
    if (m_frame565) {
        free(m_frame565);
        m_frame565 = nullptr;
    }
    if (m_poster565) {
        if (!m_posterAliasesFrame) {
            free(m_poster565);
        }
        m_poster565 = nullptr;
    }
    m_posterAliasesFrame = false;
    if (m_ioScratch) {
        free(m_ioScratch);
        m_ioScratch = nullptr;
    }
    m_ioScratchCap = 0U;
    m_posterCapBytes = 0U;
    m_framePixels = 0U;
}

void MediaShowcaseBase::clearLiveCardState() {
    m_streamPlayer.resetCore1StreamState();
    m_nextStreamFrameDueMs.store(0U, std::memory_order_relaxed);
    m_activeItemStreamMaxMs.store(0U, std::memory_order_relaxed);
    m_publishedFramePtr.store(nullptr, std::memory_order_release);
    m_hasReadyItem.store(false, std::memory_order_release);
    if (m_phase.load(std::memory_order_relaxed) != static_cast<uint8_t>(Phase::Card)) {
        m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
    }
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

EngineError MediaShowcaseBase::initialize(EngineContext* context, const EngineConfig* cfg) {
    if (!context || !context->getSurface()) {
        return EngineError::InitializationFailed;
    }
    m_surface = context->getSurface();
    DisplayGeometry geom = context->getGeometry();
    m_width = geom.width > 0 ? geom.width : 128;
    m_height = geom.height > 0 ? geom.height : 32;
    net::ShowcaseDiscovery::formatClientId(domainSlug(), m_width, m_height, false, m_sessionId, sizeof(m_sessionId));

    {
        ConfigSnapshotGuard guard = ::config.acquireSnapshot();
        m_rotationSlotCount.store(static_cast<uint8_t>(guard.get().rotation.size()), std::memory_order_relaxed);
    }

    if (cfg) {
        onConfigChanged(cfg);
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

void MediaShowcaseBase::onConfigChanged(const EngineConfig* cfg) {
    if (!cfg) return;

    const bool wasActive = m_wantActive.load(std::memory_order_acquire);
    if (wasActive) {
        m_wantActive.store(false, std::memory_order_release);
        net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
        stopWorkerTaskBounded(1500);
    }

    ConfigSlot nextSlot;
    char nextCustomUrl[MAX_URL_LEN] = "";
    char nextGlobalHost[64] = "";
    char nextSystems[MAX_SYSTEMS_FILTER_LEN] = "";
    char nextGenres[MAX_FILTER_LEN] = "";
    char nextDecades[MAX_FILTER_LEN] = "";

    {
        ConfigSnapshotGuard guard = ::config.acquireSnapshot();
        const auto& snap = guard.get();
        const auto& sc = snap.showcase;
        m_rotationSlotCount.store(static_cast<uint8_t>(snap.rotation.size()), std::memory_order_relaxed);

        nextSlot.enabled = sc.enabled;
        String srv = sc.server_url.isEmpty()
            ? cfg->getString("server_url", "http://dmdmediashowcase.local:8890")
            : sc.server_url;
        srv.trim();
        if (srv.isEmpty()) {
            srv = "http://dmdmediashowcase.local:8890";
        }
        srv = normalizeMdnsEndpointUrl(srv, true);
        copySafeStr(nextSlot.serverUrl, sizeof(nextSlot.serverUrl), srv.c_str());

        {
            const String tMode = normalizeShowcaseTransportMode(
                sc.transport_mode.isEmpty() ? cfg->getString("transport_mode", "mqtt_tcp") : sc.transport_mode
            );
            copySafeStr(nextSlot.transportMode, sizeof(nextSlot.transportMode), tMode.c_str());
        }

        nextSlot.useShowcaseBroker = sc.server_url.isEmpty() ? cfg->getBool("use_showcase_broker", true) : sc.use_showcase_broker;

        if (!sc.mqtt_broker_source.isEmpty()) {
            copySafeStr(nextSlot.mqttBrokerSource, sizeof(nextSlot.mqttBrokerSource), sc.mqtt_broker_source.c_str());
        } else {
            copyCfgFieldCStr(cfg, "mqtt_broker_source", "global", nextSlot.mqttBrokerSource, sizeof(nextSlot.mqttBrokerSource));
        }

        {
            const String cUrl = normalizeMdnsEndpointUrl(
                sc.mqtt_custom_url.isEmpty() ? cfg->getString("mqtt_custom_url", "") : sc.mqtt_custom_url,
                false
            );
            copySafeStr(nextCustomUrl, sizeof(nextCustomUrl), cUrl.c_str());
        }

        copySafeStr(nextGlobalHost, sizeof(nextGlobalHost), snap.mqtt.broker.c_str());
        nextSlot.globalMqttPort = (snap.mqtt.port > 0 && snap.mqtt.port <= 65535)
            ? static_cast<uint16_t>(snap.mqtt.port)
            : 1883;
    }

    copyCfgFieldCStr(cfg, "lang", "system", nextSlot.lang, sizeof(nextSlot.lang));
    copyCfgFieldCStr(cfg, "text_scroll_speed", "4", nextSlot.textScrollSpeed, sizeof(nextSlot.textScrollSpeed));
    {
        char vmBuf[24] = "";
        copyCfgFieldCStr(cfg, "video_mode", "auto_action", vmBuf, sizeof(vmBuf));
        const char* p = vmBuf;
        while (*p == ' ') ++p;
        const char* normVm = "auto_action";
        if (strncasecmp(p, "panorama", 8) == 0) {
            normVm = "panorama";
        } else if (strncasecmp(p, "center_crop", 11) == 0) {
            normVm = "center_crop";
        }
        copySafeStr(nextSlot.videoMode, sizeof(nextSlot.videoMode), normVm);
    }
    copyCfgFieldCStr(cfg, "systems", "", nextSystems, sizeof(nextSystems), true);
    copyCfgFieldCStr(cfg, "genres", "", nextGenres, sizeof(nextGenres), true);
    copyCfgFieldCStr(cfg, "decades", "", nextDecades, sizeof(nextDecades), true);
    copyCfgFieldCStr(cfg, "video_type", "any", nextSlot.videoType, sizeof(nextSlot.videoType));

    nextSlot.snapshotPanoramaCount = static_cast<uint8_t>(std::max(0, std::min(5, cfg->getInt("snapshot_panorama_count", 0))));
    nextSlot.videoMaxDurationS = static_cast<uint8_t>(std::max(2, std::min(120, cfg->getInt("video_max_duration_s", 8))));
    nextSlot.videoFps = static_cast<uint8_t>(std::max(8, std::min(60, cfg->getInt("video_fps", 20))));

    nextSlot.showPoster = cfg->getBool("show_poster", true);
    nextSlot.showSynopsis = cfg->getBool("show_synopsis", true);
    nextSlot.jacketPanorama = cfg->getBool("jacket_panorama", true);
    nextSlot.marqueePanorama = cfg->getBool("marquee_panorama", false);
    nextSlot.showVideo = cfg->getBool("show_video", true);
    {
        char cbBuf[24] = "";
        copyCfgFieldCStr(cfg, "color_boost", "auto", cbBuf, sizeof(cbBuf));
        const char* p = cbBuf;
        while (*p == ' ') ++p;
        const char* normCb = "auto";
        if (strncasecmp(p, "pixel_art", 9) == 0) {
            normCb = "pixel_art";
        } else if (strncasecmp(p, "anime", 5) == 0) {
            normCb = "anime";
        } else if (strncasecmp(p, "cinema", 6) == 0 ||
                   strncasecmp(p, "false", 5) == 0 ||
                   strncasecmp(p, "off", 3) == 0 ||
                   *p == '0') {
            normCb = "cinema";
        }
        copySafeStr(nextSlot.colorBoost, sizeof(nextSlot.colorBoost), normCb);
    }

    const bool needExtras = (nextSystems[0] != '\0' ||
                             nextGenres[0] != '\0' ||
                             nextDecades[0] != '\0' ||
                             nextCustomUrl[0] != '\0' ||
                             nextGlobalHost[0] != '\0');
    if (needExtras) {
        if (!m_cfgExtras) {
            void* mem = nullptr;
#if defined(ESP32) && SHOWCASE_SUPPORT_PSRAM_PRELOAD
            if (hardwareHAL.capabilities().hasPsram) {
                mem = heap_caps_malloc(sizeof(ConfigExtras), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
#endif
            if (!mem) {
                mem = malloc(sizeof(ConfigExtras));
            }
            if (mem) {
                m_cfgExtras = new (mem) ConfigExtras();
            }
        }
        if (m_cfgExtras) {
            copySafeStr(m_cfgExtras->systems, sizeof(m_cfgExtras->systems), nextSystems);
            copySafeStr(m_cfgExtras->genres, sizeof(m_cfgExtras->genres), nextGenres);
            copySafeStr(m_cfgExtras->decades, sizeof(m_cfgExtras->decades), nextDecades);
            copySafeStr(m_cfgExtras->mqttCustomUrl, sizeof(m_cfgExtras->mqttCustomUrl), nextCustomUrl);
            copySafeStr(m_cfgExtras->globalMqttHost, sizeof(m_cfgExtras->globalMqttHost), nextGlobalHost);
        }
    } else if (m_cfgExtras) {
        free(m_cfgExtras);
        m_cfgExtras = nullptr;
    }

    m_cfg = nextSlot;
    m_cfgEnabled.store(m_cfg.enabled, std::memory_order_release);
    m_cfgHasServerUrl.store(
        strlen(m_cfg.serverUrl) >= 8U && strncmp(m_cfg.serverUrl, "http", 4) == 0,
        std::memory_order_release
    );
    m_cfgShowPoster.store(m_cfg.showPoster, std::memory_order_release);
    m_cfgShowSynopsis.store(m_cfg.showSynopsis, std::memory_order_release);
    m_cfgSynopsisSpeedPx.store(static_cast<uint16_t>(synopsisPxPerSec(m_cfg.textScrollSpeed)), std::memory_order_release);
    m_cfgStreamBaseMs.store(configuredMaxStreamMs(m_cfg), std::memory_order_release);

    uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
    m_fallbackCycleMs = std::max<uint32_t>(30000U, static_cast<uint32_t>(m_cfg.videoMaxDurationS + 25) * budget * 1000U);

    m_cfgGen.fetch_add(1, std::memory_order_acq_rel);
    m_lastPublishedCfgGen = 0;
    m_consecutiveNetFailures = 0;
    m_consecutiveWarmSkips.store(0, std::memory_order_release);

    if (wasActive) {
        m_activatedAtMs = millis();
        m_phaseStartedAtMs = m_activatedAtMs;
        m_turnEvaluated.store(false, std::memory_order_release);
        m_readyStateReceived.store(false, std::memory_order_release);
        m_hasReadyItem.store(false, std::memory_order_release);
        m_mqttConnectedAtMs.store(0U, std::memory_order_release);
        m_skipTurnImmediately.store(false, std::memory_order_release);
        m_workerBusyOrHolding.store(false, std::memory_order_release);
        m_turnDone.store(false, std::memory_order_release);
        m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
        clearLiveCardState();
        m_lastRenderedCardSeq = 0xFFFFFFFFU;
        m_lastRenderedFrameSeq = 0xFFFFFFFFU;
        m_lastRenderedPhase = 0xFFU;
        m_dirty = true;
        m_buffersReady.store(true, std::memory_order_release);
        startWorkerTask();
    }
}

void MediaShowcaseBase::activate() {
    {
        ConfigSnapshotGuard guard = ::config.acquireSnapshot();
        m_rotationSlotCount.store(static_cast<uint8_t>(guard.get().rotation.size()), std::memory_order_relaxed);
    }

#if defined(ESP32)
    m_hasPsram = hardwareHAL.capabilities().hasPsram;
#else
    m_hasPsram = true;
#endif
    probeZeroCopyCanvasEligibility();

    m_pausedByPreemption = false;
    m_activatedAtMs = millis();
    m_phaseStartedAtMs = m_activatedAtMs;
    m_lastScrollStepMs = m_activatedAtMs;
    m_consecutiveNetFailures = 0;
    m_itemsPlayedInTurn.store(0, std::memory_order_release);
    m_turnEvaluated.store(false, std::memory_order_release);
    m_readyStateReceived.store(false, std::memory_order_release);
    m_hasReadyItem.store(false, std::memory_order_release);
    m_mqttConnectedAtMs.store(0U, std::memory_order_release);
    m_skipTurnImmediately.store(false, std::memory_order_release);
    m_warmingFallback.store(false, std::memory_order_release);
    m_workerBusyOrHolding.store(false, std::memory_order_release);
    m_turnDone.store(false, std::memory_order_release);
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
    clearLiveCardState();
    m_lastRenderedCardSeq = 0xFFFFFFFFU;
    m_lastRenderedFrameSeq = 0xFFFFFFFFU;
    m_lastRenderedPhase = 0xFFU;
    m_dirty = true;
    m_renderedNewFrame = false;

    // Spawn `ShowcaseWkr` (`5,120 B` stack on `esp32dev`, `6,144 B` on PSRAM S3) FIRST before
    // allocating transient engine buffers so largest contiguous 8-bit DRAM block is never split!
    m_buffersReady.store(false, std::memory_order_release);
    const bool workerOk = startWorkerTask();

    const size_t wantPixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    if (!m_poster565 || !m_ioScratch || m_framePixels != wantPixels) {
        allocateBuffers();
    }
    m_buffersReady.store(true, std::memory_order_release);

    if (!workerOk) {
#if defined(ESP32) && !defined(UNIT_TEST)
        LOGE("Showcase", "Failed to spawn ShowcaseWkr (free8=%u largest8=%u); yielding rotation slot",
             static_cast<unsigned>(showcaseFree8BitHeap()),
             static_cast<unsigned>(showcaseLargest8BitBlock()));
        m_statusState.store(2, std::memory_order_release);
        m_readyStateReceived.store(true, std::memory_order_release);
        m_turnEvaluated.store(true, std::memory_order_release);
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
            m_turnDone.store(true, std::memory_order_release);
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
    m_buffersReady.store(false, std::memory_order_release);
    clearLiveCardState();
    if (!m_taskAlive.load(std::memory_order_acquire)) {
        freeBuffers();
    }
    m_turnEvaluated.store(false, std::memory_order_release);
    m_readyStateReceived.store(false, std::memory_order_release);
    m_workerBusyOrHolding.store(false, std::memory_order_release);
    m_mqttConnectedAtMs.store(0U, std::memory_order_release);
    m_dirty = false;
    m_renderedNewFrame = false;
}

bool MediaShowcaseBase::shutdownForDestruction() {
    m_wantActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
    stopWorkerTaskBounded(2000);
    m_buffersReady.store(false, std::memory_order_release);
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

uint32_t MediaShowcaseBase::effectiveMaxStreamMs() const {
    const uint32_t cfgMs = m_cfgStreamBaseMs.load(std::memory_order_relaxed);
    const uint32_t itemMs = m_activeItemStreamMaxMs.load(std::memory_order_relaxed);
    return std::min<uint32_t>(300000U, std::max<uint32_t>(cfgMs, itemMs));
}

uint32_t MediaShowcaseBase::computeTurnSafetyTimeoutMs() const {
    uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
    uint32_t perItemMs = effectiveMaxStreamMs() + 45000U;
    return std::max<uint32_t>(45000U, perItemMs * budget);
}

bool MediaShowcaseBase::isFinished() const {
    if (m_skipTurnImmediately.load(std::memory_order_acquire)) {
        return true;
    }
    if (m_turnDone.load(std::memory_order_acquire)) {
        return true;
    }
    const uint8_t rotSlots = m_rotationSlotCount.load(std::memory_order_relaxed);
    if (!m_cfgEnabled.load(std::memory_order_acquire) && rotSlots > 1U) {
        return true;
    }
    if (!m_turnEvaluated.load(std::memory_order_acquire)) {
        return false;
    }
    if (rotSlots > 1U) {
        return (millis() - m_activatedAtMs) >= computeTurnSafetyTimeoutMs();
    }
    return false;
}

bool MediaShowcaseBase::startWorkerTask() {
    if (!m_cfgEnabled.load(std::memory_order_acquire)) {
        m_statusState.store(3, std::memory_order_release);
        m_readyStateReceived.store(true, std::memory_order_release);
        m_turnEvaluated.store(true, std::memory_order_release);
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
            m_turnDone.store(true, std::memory_order_release);
        }
        return true;
    }
    m_wantActive.store(true, std::memory_order_release);
#if defined(ESP32) && !defined(UNIT_TEST)
    if (m_taskAlive.load(std::memory_order_acquire)) return true;
    m_taskAlive.store(true, std::memory_order_release);
    const uint32_t stackBytes = m_hasPsram ? 6144U : 5120U;
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

        extractShowcaseItemFromJson(
            doc.as<JsonObjectConst>(),
            cardObj,
            domain,
            m_sessionId,
            m_width,
            m_height,
            m_posterCapBytes,
            defaultAccent565(),
            nextCard,
            outItemId,
            itemIdCap,
            nullptr,
            0U,
            nullptr,
            0U,
            outHasStream,
            outStreamMaxMs
        );
        m_activeItemStreamMaxMs.store(outStreamMaxMs, std::memory_order_relaxed);
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
    m_hasReadyItem.store(true, std::memory_order_release);
    m_readyStateReceived.store(true, std::memory_order_release);
    m_turnEvaluated.store(true, std::memory_order_release);
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

namespace {

#if defined(ESP32) && !defined(UNIT_TEST)
/**
 * @brief Unified zero-heap `Stream` adapter over `net::RawMqttSocket` for both bounded MQTT
 *        `PUBLISH` payloads and HTTP (`Content-Length` / `Transfer-Encoding: chunked`) bodies.
 *        Inheriting `Stream` allows `ArduinoJson` to reuse its already-linked `Reader<Stream>`
 *        deserializer without instantiating extra parser templates (~4.3 KB Flash saved).
 */
class RawSocketJsonStream final : public Stream {
public:
    net::RawMqttSocket& sock;
    const std::atomic<bool>& wantActive;
    net::HttpChunkedDecoder chunkDec{};
    uint8_t decBuf[96] = {0};
    size_t decOff = 0U;
    size_t decEnd = 0U;
    size_t contentRem = 0U;
    int peeked = -1;
    bool isChunked = false;
    bool hasContentLength = false;
    bool eof = false;
    bool ioError = false;

    RawSocketJsonStream(
        net::RawMqttSocket& socketRef,
        const std::atomic<bool>& activeFlag,
        size_t boundedPayloadBytes
    )
        : sock(socketRef),
          wantActive(activeFlag),
          contentRem(boundedPayloadBytes),
          hasContentLength(true) {
        setTimeout(0);
    }

    RawSocketJsonStream(
        net::RawMqttSocket& socketRef,
        const std::atomic<bool>& activeFlag,
        size_t contentLength,
        bool hasCl,
        bool chunked
    )
        : sock(socketRef),
          wantActive(activeFlag),
          contentRem(contentLength),
          isChunked(chunked),
          hasContentLength(hasCl) {
        setTimeout(0);
    }

    size_t write(uint8_t) override { return 0U; }

    int available() override {
        return (eof || ioError) ? 0 : 1;
    }

    int peek() override {
        if (peeked < 0) {
            peeked = readNextByte();
        }
        return peeked;
    }

    int read() override {
        if (peeked >= 0) {
            const int c = peeked;
            peeked = -1;
            return c;
        }
        return readNextByte();
    }

    bool drainRemaining() {
        peeked = -1;
        if (!hasContentLength || contentRem == 0U) {
            return !ioError;
        }
        if (ioError) {
            return false;
        }
        const bool ok = sock.drain(contentRem, wantActive, 1500U);
        contentRem = 0U;
        eof = true;
        return ok;
    }

private:
    bool fillDecodedChunk() {
        decOff = 0U;
        decEnd = 0U;
        while (!eof && !ioError && !chunkDec.isDone() && !chunkDec.hasError()) {
            if (sock.rxOff >= sock.rxEnd) {
                uint8_t b = 0U;
                if (!sock.readExact(&b, 1U, wantActive, 2000U)) {
                    ioError = true;
                    return false;
                }
                size_t consumed = 0U;
                const size_t n = chunkDec.decode(&b, 1U, consumed, decBuf, sizeof(decBuf));
                if (chunkDec.hasError()) {
                    ioError = true;
                    return false;
                }
                if (n > 0U) {
                    decEnd = n;
                    return true;
                }
                if (chunkDec.isDone()) {
                    eof = true;
                    return false;
                }
                continue;
            }
            const size_t avail = sock.rxEnd - sock.rxOff;
            size_t consumed = 0U;
            const size_t n = chunkDec.decode(sock.rxBuf + sock.rxOff, avail, consumed, decBuf, sizeof(decBuf));
            sock.rxOff += consumed;
            if (chunkDec.hasError()) {
                ioError = true;
                return false;
            }
            if (n > 0U) {
                decEnd = n;
                return true;
            }
            if (chunkDec.isDone()) {
                eof = true;
                return false;
            }
        }
        return false;
    }

    int readNextByte() {
        if (eof || ioError) {
            return -1;
        }
        if (isChunked) {
            if (decOff >= decEnd && !fillDecodedChunk()) {
                return -1;
            }
            return static_cast<int>(decBuf[decOff++]);
        }
        if (hasContentLength && contentRem == 0U) {
            eof = true;
            return -1;
        }
        if (sock.rxOff < sock.rxEnd) {
            const uint8_t b = sock.rxBuf[sock.rxOff++];
            if (hasContentLength) {
                --contentRem;
            }
            return static_cast<int>(b);
        }
        uint8_t b = 0U;
        if (!sock.readExact(&b, 1U, wantActive, 2000U)) {
            if (hasContentLength) {
                ioError = true;
            } else {
                eof = true;
            }
            return -1;
        }
        if (hasContentLength) {
            --contentRem;
        }
        return static_cast<int>(b);
    }
};

bool openRawHttpSession(
    net::RawMqttSocket& sock,
    const char* host,
    uint16_t port,
    const char* reqHdr,
    size_t reqHdrLen,
    const void* reqBody,
    size_t reqBodyLen,
    const std::atomic<bool>& wantActive,
    uint32_t connTimeoutMs,
    uint32_t hdrTimeoutMs,
    int& outStatusCode,
    size_t& outContentLength,
    bool& outHasContentLength,
    bool& outIsChunked
) {
    outStatusCode = 0;
    outContentLength = 0U;
    outHasContentLength = false;
    outIsChunked = false;
    if (!sock.connectHost(host, port, wantActive, connTimeoutMs)) {
        return false;
    }
    if (!sock.writeAll(reinterpret_cast<const uint8_t*>(reqHdr), reqHdrLen, wantActive, 1000U)) {
        sock.closeAbort();
        return false;
    }
    if (reqBody && reqBodyLen > 0U &&
        !sock.writeAll(reinterpret_cast<const uint8_t*>(reqBody), reqBodyLen, wantActive, 1200U)) {
        sock.closeAbort();
        return false;
    }

    char hdrBuf[384];
    size_t len = 0U;
    hdrBuf[0] = '\0';
    const uint32_t t0 = millis();
    bool foundEnd = false;
    while ((millis() - t0) < hdrTimeoutMs && wantActive.load(std::memory_order_acquire)) {
        uint8_t b = 0U;
        if (!sock.readExact(&b, 1U, wantActive, hdrTimeoutMs)) {
            sock.closeAbort();
            return false;
        }
        if (len + 1U < sizeof(hdrBuf)) {
            hdrBuf[len++] = static_cast<char>(b);
            hdrBuf[len] = '\0';
        } else {
            memmove(hdrBuf, hdrBuf + 1, sizeof(hdrBuf) - 2U);
            hdrBuf[sizeof(hdrBuf) - 2U] = static_cast<char>(b);
            hdrBuf[sizeof(hdrBuf) - 1U] = '\0';
        }
        const size_t curLen = strlen(hdrBuf);
        if (curLen >= 4U && memcmp(hdrBuf + curLen - 4U, "\r\n\r\n", 4U) == 0) {
            foundEnd = true;
            break;
        }
    }
    if (!foundEnd) {
        sock.closeAbort();
        return false;
    }

    const char* sp = strchr(hdrBuf, ' ');
    if (sp) {
        outStatusCode = atoi(sp + 1);
    }
    outIsChunked = net::hasChunkedTransferEncoding(hdrBuf);
    const char* p = hdrBuf;
    while (*p != '\0') {
        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            p += 15;
            while (*p == ' ' || *p == '\t') {
                ++p;
            }
            outContentLength = static_cast<size_t>(strtoul(p, nullptr, 10));
            outHasContentLength = true;
            break;
        }
        const char* nl = strchr(p, '\n');
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    if (outStatusCode < 200 || outStatusCode >= 300) {
        sock.closeAbort();
        return false;
    }
    return true;
}
#endif

static_assert(
    sizeof(net::DmdzHttpStreamSession) <= 1472U,
    "DmdzHttpStreamSession must fit inside MIN_IO_SCRATCH_BYTES (1472 B) with zero heap alloc"
);

} // namespace

void MediaShowcaseBase::runWorkerLoop() {
#if defined(ESP32) && !defined(UNIT_TEST)
    while (m_wantActive.load(std::memory_order_acquire) &&
           !m_buffersReady.load(std::memory_order_acquire)) {
        vTaskDelay(1);
    }
#endif

    bool isFollowUpItemInTurn = false;

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
        const uint32_t startGen = m_cfgGen.load(std::memory_order_acquire);
        const ConfigSlot& cfg = m_cfg;
        const ConfigExtras* extras = m_cfgExtras;
        const char* domain = domainSlug();
        const uint8_t rotSlots = m_rotationSlotCount.load(std::memory_order_relaxed);
        const bool soloSlot = (rotSlots <= 1U);

        if (!cfg.enabled || strlen(cfg.serverUrl) < 8U || strncmp(cfg.serverUrl, "http", 4) != 0) {
            m_statusState.store(3, std::memory_order_release);
            m_readyStateReceived.store(true, std::memory_order_release);
            m_turnEvaluated.store(true, std::memory_order_release);
            clearLiveCardState();
            if (!soloSlot) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
                m_turnDone.store(true, std::memory_order_release);
            }
            return;
        }

        if (!m_ioScratch || m_ioScratchCap == 0U) {
            m_statusState.store(2, std::memory_order_release);
            m_readyStateReceived.store(true, std::memory_order_release);
            m_turnEvaluated.store(true, std::memory_order_release);
            if (!soloSlot) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
                m_turnDone.store(true, std::memory_order_release);
                return;
            }
#if defined(ESP32) && !defined(UNIT_TEST)
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
#else
            return;
#endif
        }

        char reqLangBuf[12] = "en";
        if (cfg.lang[0] != '\0' && strcasecmp(cfg.lang, "system") != 0) {
            copySafeStr(reqLangBuf, sizeof(reqLangBuf), cfg.lang);
        } else {
            ConfigSnapshotGuard guard = ::config.acquireSnapshot();
            const String& sysLang = guard.get().system.lang;
            if (sysLang.length() > 0) {
                copySafeStr(reqLangBuf, sizeof(reqLangBuf), sysLang.c_str());
            }
        }

        net::ShowcaseDiscovery::formatClientId(domain, m_width, m_height, false, m_sessionId, sizeof(m_sessionId));

        CardSnapshot acquiredCard;
        AcquiredItemInfo acquiredInfo;
        const bool useMqttMode = (strcasecmp(cfg.transportMode, "mqtt_udp") == 0 ||
                                  strcasecmp(cfg.transportMode, "mqtt_tcp") == 0);

        AcquireOutcome outcome = AcquireOutcome::ServerOffline;
        if (useMqttMode) {
            outcome = acquireItemViaMqtt(
                cfg,
                extras,
                domain,
                reqLangBuf,
                startGen,
                isFollowUpItemInTurn,
                acquiredCard,
                acquiredInfo
            );
            // Part A.4: Automatic same-turn MQTT -> HTTP fallback when MQTT broker (`:1883`)
            // connect fails or disconnects before acquiring an item!
            if (outcome == AcquireOutcome::BrokerUnreachable &&
                m_wantActive.load(std::memory_order_acquire) &&
                !shouldYieldShowcaseToWebUi(m_hasPsram) &&
                m_cfgGen.load(std::memory_order_acquire) == startGen) {
                LOGW("Showcase", "MQTT broker unreachable for '%s'; chaining same-turn HTTP fallback", domain);
                m_mqttConnectedAtMs.store(0U, std::memory_order_release);
                outcome = acquireItemViaHttp(
                    cfg,
                    extras,
                    domain,
                    reqLangBuf,
                    startGen,
                    acquiredCard,
                    acquiredInfo
                );
            }
        } else {
            outcome = acquireItemViaHttp(
                cfg,
                extras,
                domain,
                reqLangBuf,
                startGen,
                acquiredCard,
                acquiredInfo
            );
        }

        if (!m_wantActive.load(std::memory_order_acquire)) {
            return;
        }
        if (m_cfgGen.load(std::memory_order_acquire) != startGen) {
            isFollowUpItemInTurn = false;
            m_skipTurnImmediately.store(false, std::memory_order_release);
            m_turnDone.store(false, std::memory_order_release);
            clearLiveCardState();
            continue;
        }
        if (outcome == AcquireOutcome::Aborted) {
#if defined(ESP32) && !defined(UNIT_TEST)
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
#else
            return;
#endif
        }

        if (outcome == AcquireOutcome::ReadyItem) {
            m_consecutiveNetFailures = 0;
            m_consecutiveWarmSkips.store(0, std::memory_order_release);
            m_warmingFallback.store(false, std::memory_order_release);

            executeAcquiredItem(cfg, domain, startGen, acquiredCard, acquiredInfo);

            if (!m_wantActive.load(std::memory_order_acquire)) {
                return;
            }
            if (m_cfgGen.load(std::memory_order_acquire) != startGen) {
                isFollowUpItemInTurn = false;
                m_skipTurnImmediately.store(false, std::memory_order_release);
                m_turnDone.store(false, std::memory_order_release);
                clearLiveCardState();
                continue;
            }

            const uint32_t played = m_itemsPlayedInTurn.fetch_add(1U, std::memory_order_acq_rel) + 1U;
            const uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
            if (!soloSlot) {
                if (played >= budget) {
                    m_turnDone.store(true, std::memory_order_release);
                    return;
                }
                // Part A.2: Multi-item budget (`played < budget`): probe item N+1 without clearing
                // `m_liveCard` so `"WAITING FOR..."` never flashes if N+1 is not ready yet!
                isFollowUpItemInTurn = true;
                continue;
            }
            isFollowUpItemInTurn = false;
            continue;
        }

        // Outcome is `ServerWarming`, `ServerOffline`, or `BrokerUnreachable` (both transports failed)
        if (isFollowUpItemInTurn && !soloSlot) {
            // Item N+1 is not already ready -> yield rotation immediately (`0 ms`) without clearing
            // the previously displayed frame or flashing `"WAITING FOR..."`!
            m_skipTurnImmediately.store(true, std::memory_order_release);
            m_turnDone.store(true, std::memory_order_release);
            return;
        }

        if (outcome == AcquireOutcome::ServerWarming) {
            m_statusState.store(1, std::memory_order_release);
            m_warmingFallback.store(true, std::memory_order_release);
            const uint8_t prevSkips = m_consecutiveWarmSkips.load(std::memory_order_acquire);
            if (prevSkips < 10U) {
                m_consecutiveWarmSkips.store(static_cast<uint8_t>(prevSkips + 1U), std::memory_order_release);
            }
            if (soloSlot &&
                prevSkips >= 2U &&
                m_phase.load(std::memory_order_acquire) == static_cast<uint8_t>(Phase::StreamEndHold)) {
                clearLiveCardState();
            }
            if (!soloSlot) {
                // Hold the warming pixel-art banner briefly (`2.5 s`) on item 0 so multi-Showcase
                // rotations do not thrash across slots every `720 ms` while the server warms up.
#if defined(ESP32) && !defined(UNIT_TEST)
                m_workerBusyOrHolding.store(true, std::memory_order_release);
                for (int w = 0;
                     w < 50 &&
                     m_wantActive.load(std::memory_order_acquire) &&
                     !shouldYieldShowcaseToWebUi(m_hasPsram) &&
                     m_cfgGen.load(std::memory_order_acquire) == startGen;
                     ++w) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                m_workerBusyOrHolding.store(false, std::memory_order_release);
#endif
                m_readyStateReceived.store(true, std::memory_order_release);
                m_turnEvaluated.store(true, std::memory_order_release);
                m_skipTurnImmediately.store(true, std::memory_order_release);
                m_turnDone.store(true, std::memory_order_release);
                return;
            }
            m_readyStateReceived.store(true, std::memory_order_release);
            m_turnEvaluated.store(true, std::memory_order_release);
#if defined(ESP32) && !defined(UNIT_TEST)
            for (int w = 0;
                 w < 8 &&
                 m_wantActive.load(std::memory_order_acquire) &&
                 !shouldYieldShowcaseToWebUi(m_hasPsram) &&
                 m_cfgGen.load(std::memory_order_acquire) == startGen;
                 ++w) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            continue;
#else
            return;
#endif
        }

        // ServerOffline / unreachable
        if (m_consecutiveNetFailures < 10U) {
            ++m_consecutiveNetFailures;
        }
        m_lastPublishedCfgGen = 0;
        if (!soloSlot || shouldReportNetworkOffline(m_consecutiveNetFailures)) {
            m_statusState.store(2, std::memory_order_release);
            clearLiveCardState();
        } else if (m_statusState.load(std::memory_order_acquire) != 0U) {
            m_statusState.store(1, std::memory_order_release);
        }
        if (!soloSlot) {
#if defined(ESP32) && !defined(UNIT_TEST)
            m_workerBusyOrHolding.store(true, std::memory_order_release);
            for (int w = 0;
                 w < 30 &&
                 m_wantActive.load(std::memory_order_acquire) &&
                 !shouldYieldShowcaseToWebUi(m_hasPsram) &&
                 m_cfgGen.load(std::memory_order_acquire) == startGen;
                 ++w) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            m_workerBusyOrHolding.store(false, std::memory_order_release);
#endif
            m_readyStateReceived.store(true, std::memory_order_release);
            m_turnEvaluated.store(true, std::memory_order_release);
            m_skipTurnImmediately.store(true, std::memory_order_release);
            m_turnDone.store(true, std::memory_order_release);
            return;
        }
        m_readyStateReceived.store(true, std::memory_order_release);
        m_turnEvaluated.store(true, std::memory_order_release);
#if defined(ESP32) && !defined(UNIT_TEST)
        for (int w = 0;
             w < 10 &&
             m_wantActive.load(std::memory_order_acquire) &&
             !shouldYieldShowcaseToWebUi(m_hasPsram) &&
             m_cfgGen.load(std::memory_order_acquire) == startGen;
             ++w) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
#else
        return;
#endif
    }
}

MediaShowcaseBase::AcquireOutcome MediaShowcaseBase::acquireItemViaMqtt(
    const ConfigSlot& cfg,
    const ConfigExtras* extras,
    const char* domain,
    const char* reqLang,
    uint32_t startGen,
    bool isFollowUpItemInTurn,
    CardSnapshot& outCard,
    AcquiredItemInfo& outInfo
) {
    outCard = CardSnapshot{};
    outInfo = AcquiredItemInfo{};
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!m_ioScratch || m_ioScratchCap < 512U) {
        return AcquireOutcome::BrokerUnreachable;
    }

    char mqttHost[64] = "";
    uint16_t mqttPort = 1883;
    if (!resolveMqttBroker(cfg, extras, mqttHost, sizeof(mqttHost), mqttPort)) {
        return AcquireOutcome::BrokerUnreachable;
    }

    uint16_t showcaseHttpPort = 8890;
    bool serverIsHttps = false;
    {
        char serverHost[64] = "";
        if (net::parseEndpointHostAndPort(cfg.serverUrl, 8890, serverHost, sizeof(serverHost), showcaseHttpPort, &serverIsHttps)) {
            if (net::ShowcaseDiscovery::resolveHostInPlace(serverHost, sizeof(serverHost), showcaseHttpPort, false)) {
                snprintf(
                    outInfo.baseServerUrl,
                    sizeof(outInfo.baseServerUrl),
                    "%s://%s:%u",
                    serverIsHttps ? "https" : "http",
                    serverHost,
                    static_cast<unsigned>(showcaseHttpPort)
                );
            }
        }
        if (outInfo.baseServerUrl[0] == '\0') {
            if (cfg.useShowcaseBroker) {
                snprintf(
                    outInfo.baseServerUrl,
                    sizeof(outInfo.baseServerUrl),
                    "%s://%s:%u",
                    serverIsHttps ? "https" : "http",
                    mqttHost,
                    static_cast<unsigned>(showcaseHttpPort)
                );
            } else {
                copySafeStr(outInfo.baseServerUrl, sizeof(outInfo.baseServerUrl), cfg.serverUrl);
            }
        }
    }

    const uint8_t rotSlots = m_rotationSlotCount.load(std::memory_order_relaxed);
    const bool soloSlot = (rotSlots <= 1U);
    const uint32_t tcpTimeoutMs = soloSlot ? 1200U : 450U;
    const uint32_t connAckTimeoutMs = soloSlot ? 800U : 250U;

    const uint16_t localUdpPort = static_cast<uint16_t>(5568U + static_cast<uint16_t>(m_domain));
    const char* statusTopic = "dmd_showcase/server/status";
    char readyTopic[96];
    char eventsTopic[96];
    snprintf(readyTopic, sizeof(readyTopic), "dmd_showcase/clients/%s/%s/ready", m_sessionId, domain);
    snprintf(eventsTopic, sizeof(eventsTopic), "dmd_showcase/clients/%s/%s/events", m_sessionId, domain);

    IPAddress ipAddr = WiFi.localIP();
    char localIpBuf[20];
    snprintf(localIpBuf, sizeof(localIpBuf), "%u.%u.%u.%u", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);

    NetworkBudget::ScopedDmdNetActivity mqttAct(NetworkBudget::DmdNetActivity::Mqtt);
    net::RawMqttSocket mqttCli;
    if (!net::connectMqttBrokerSession(
            mqttCli,
            mqttHost,
            mqttPort,
            m_sessionId,
            m_mqttConnSeq,
            m_wantActive,
            tcpTimeoutMs,
            connAckTimeoutMs)) {
        if (!m_wantActive.load(std::memory_order_acquire) || shouldYieldShowcaseToWebUi(m_hasPsram)) {
            return AcquireOutcome::Aborted;
        }
        return AcquireOutcome::BrokerUnreachable;
    }

    const uint32_t connectedAtMs = millis();
    m_mqttConnectedAtMs.store(connectedAtMs != 0U ? connectedAtMs : 1U, std::memory_order_release);
    if (cfg.useShowcaseBroker) {
        net::ShowcaseDiscovery::touchVerifiedHost(mqttHost, showcaseHttpPort);
    }

    if (m_lastPublishedCfgGen != startGen) {
        uint16_t posterW = 32;
        uint16_t posterH = 32;
        computeCardPosterDims(m_width, m_height, posterW, posterH);
        const uint8_t effFps = resolveEffectiveVideoFps(cfg.videoFps, m_hasPsram);
        const char* sysCsv = extras ? extras->systems : "";
        const char* genCsv = extras ? extras->genres : "";
        const char* decCsv = extras ? extras->decades : "";

        char configTopic[80];
        snprintf(configTopic, sizeof(configTopic), "dmd_showcase/clients/%s/config", m_sessionId);

        size_t pos = 0U;
        const size_t scratchCap = m_ioScratchCap;
        m_ioScratch[0] = '\0';
        appendRawToBuf(m_ioScratch, scratchCap, pos, "{\"dmd_id\":\"");
        appendJsonEscapedToBuf(m_ioScratch, scratchCap, pos, m_sessionId);
        appendRawToBuf(m_ioScratch, scratchCap, pos, "\",\"client_ip\":\"");
        appendRawToBuf(m_ioScratch, scratchCap, pos, localIpBuf);
        appendRawToBuf(m_ioScratch, scratchCap, pos, "\",\"udp_ip\":\"");
        appendRawToBuf(m_ioScratch, scratchCap, pos, localIpBuf);
        appendRawToBuf(m_ioScratch, scratchCap, pos, "\",\"transport\":\"mqtt_tcp\",\"width\":");
        appendUintToBuf(m_ioScratch, scratchCap, pos, m_width);
        appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"height\":");
        appendUintToBuf(m_ioScratch, scratchCap, pos, m_height);
        appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"poster_width\":");
        appendUintToBuf(m_ioScratch, scratchCap, pos, posterW);
        appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"poster_height\":");
        appendUintToBuf(m_ioScratch, scratchCap, pos, posterH);
        appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"udp_port\":");
        appendUintToBuf(m_ioScratch, scratchCap, pos, localUdpPort);
        appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"lang\":\"");
        appendJsonEscapedToBuf(m_ioScratch, scratchCap, pos, reqLang);
        appendRawToBuf(m_ioScratch, scratchCap, pos, "\",\"domains\":{\"");
        appendRawToBuf(m_ioScratch, scratchCap, pos, domain);
        appendRawToBuf(m_ioScratch, scratchCap, pos, "\":{");
        appendShowcaseCommonConfigJson(
            m_ioScratch,
            scratchCap,
            pos,
            sysCsv,
            genCsv,
            decCsv,
            cfg.videoType,
            cfg.showPoster,
            cfg.showSynopsis,
            false,
            cfg.textScrollSpeed,
            cfg.jacketPanorama,
            cfg.marqueePanorama,
            cfg.snapshotPanoramaCount,
            cfg.showVideo,
            cfg.videoMaxDurationS,
            effFps,
            cfg.videoMode,
            cfg.colorBoost
        );
        appendRawToBuf(m_ioScratch, scratchCap, pos, "}}}");

        if (net::sendMqttPublishPacket(mqttCli, configTopic, m_ioScratch, true, m_wantActive, m_ioScratch, scratchCap)) {
            m_lastPublishedCfgGen = startGen;
        }
    }

    if (!net::sendMqttSubscribePacket(mqttCli, 1, statusTopic, m_wantActive) ||
        !net::sendMqttSubscribePacket(mqttCli, 2, readyTopic, m_wantActive)) {
        net::sendMqttDisconnectAndStop(mqttCli, m_wantActive);
        return AcquireOutcome::BrokerUnreachable;
    }

    const uint8_t prevSkips = m_consecutiveWarmSkips.load(std::memory_order_acquire);
    const uint32_t playedSoFar = m_itemsPlayedInTurn.load(std::memory_order_acquire);
    const uint32_t warmSettleMs = isFollowUpItemInTurn
        ? 0U
        : (soloSlot ? 2000U : ((prevSkips >= 1U && playedSoFar == 0U) ? 350U : 0U));
    const uint32_t retainedWaitMs = isFollowUpItemInTurn
        ? 200U
        : (soloSlot ? 2400U : std::max<uint32_t>(250U, warmSettleMs));

    bool gotReadyItem = false;
    bool sawReadyFalse = false;
    bool sawServerOnline = false;
    uint32_t readyFalseAtMs = 0U;
    const uint32_t waitReadyStart = millis();

    while (m_wantActive.load(std::memory_order_acquire) &&
           !shouldYieldShowcaseToWebUi(m_hasPsram) &&
           m_cfgGen.load(std::memory_order_acquire) == startGen &&
           (millis() - waitReadyStart) < retainedWaitMs &&
           !gotReadyItem) {
        if (sawReadyFalse && (millis() - readyFalseAtMs) >= warmSettleMs) {
            break;
        }
        if (mqttCli.available() >= 2U) {
            int rawHdr = mqttCli.readByte();
            if (rawHdr < 0) {
                break;
            }
            const uint8_t fixedHdr = static_cast<uint8_t>(rawHdr);
            uint32_t remLen = 0U;
            uint32_t mult = 1U;
            bool remOk = true;
            for (int i = 0; i < 4; ++i) {
                uint8_t b = 0U;
                if (!mqttCli.readExact(&b, 1U, m_wantActive, 600U)) {
                    remOk = false;
                    break;
                }
                remLen += static_cast<uint32_t>(b & 0x7FU) * mult;
                mult *= 128U;
                if ((b & 0x80U) == 0U) {
                    break;
                }
            }
            if (!remOk) {
                break;
            }
            if ((fixedHdr & 0xF0U) != 0x30U || remLen < 2U || remLen > 262144U) {
                if (remLen > 0U && !mqttCli.drain(remLen, m_wantActive, 1500U)) {
                    break;
                }
                continue;
            }

            uint8_t tLenBe[2] = {0, 0};
            if (!mqttCli.readExact(tLenBe, 2U, m_wantActive, 800U)) {
                break;
            }
            const uint16_t tLen = static_cast<uint16_t>((tLenBe[0] << 8) | tLenBe[1]);
            uint32_t hdrConsumed = 2U + static_cast<uint32_t>(tLen);
            const uint8_t qos = static_cast<uint8_t>((fixedHdr >> 1) & 0x03U);
            if (qos > 0U) {
                hdrConsumed += 2U;
            }
            if (hdrConsumed > remLen) {
                mqttCli.drain(remLen - 2U, m_wantActive, 1000U);
                continue;
            }

            char topicBuf[112] = "";
            const size_t toReadT = (tLen < sizeof(topicBuf) - 1U) ? tLen : (sizeof(topicBuf) - 1U);
            if (toReadT > 0U && !mqttCli.readExact(reinterpret_cast<uint8_t*>(topicBuf), toReadT, m_wantActive, 800U)) {
                break;
            }
            topicBuf[toReadT] = '\0';
            if (tLen > toReadT && !mqttCli.drain(tLen - toReadT, m_wantActive, 800U)) {
                break;
            }
            if (qos > 0U) {
                uint8_t pktIdSkip[2];
                if (!mqttCli.readExact(pktIdSkip, 2U, m_wantActive, 800U)) {
                    break;
                }
            }

            const size_t payloadLen = static_cast<size_t>(remLen - hdrConsumed);
            if (strcmp(topicBuf, statusTopic) == 0) {
                RawSocketJsonStream reader(mqttCli, m_wantActive, payloadLen);
                StaticJsonDocument<64> sFilter;
                sFilter["online"] = true;
                StaticJsonDocument<128> sDoc;
                const DeserializationError sErr = deserializeJson(
                    sDoc,
                    static_cast<Stream&>(reader),
                    DeserializationOption::Filter(sFilter)
                );
                if (!reader.drainRemaining()) {
                    break;
                }
                if (sErr == DeserializationError::Ok) {
                    const bool online = sDoc["online"] | false;
                    if (!online) {
                        net::sendMqttDisconnectAndStop(mqttCli, m_wantActive);
                        return AcquireOutcome::ServerOffline;
                    }
                    sawServerOnline = true;
                }
            } else if (strcmp(topicBuf, readyTopic) == 0) {
                // Part A.3: Streaming filtered JSON parse directly off `RawMqttSocket` using
                // `m_ioScratch` (`0` heap alloc, `0` drop on `> 8 KB` / `256x64` packets)!
                RawSocketJsonStream reader(mqttCli, m_wantActive, payloadLen);
                StaticJsonDocument<JSON_OBJECT_SIZE(28)> rFilter;
                buildShowcaseMqttReadyFilter(rFilter);
                ScratchSingleBlockAllocator scratchAlloc{m_ioScratch, m_ioScratchCap, false};
                BasicJsonDocument<ScratchSingleBlockAllocator> doc(m_ioScratchCap, scratchAlloc);
                const DeserializationError err = deserializeJson(
                    doc,
                    static_cast<Stream&>(reader),
                    DeserializationOption::Filter(rFilter)
                );
                if (!reader.drainRemaining()) {
                    break;
                }
                if (err == DeserializationError::Ok) {
                    const bool rdy = doc["ready"] | false;
                    JsonObjectConst cardObj = doc["card"].as<JsonObjectConst>();
                    if (!rdy || cardObj.isNull()) {
                        if (!sawReadyFalse) {
                            sawReadyFalse = true;
                            readyFalseAtMs = millis();
                        }
                    } else {
                        extractShowcaseItemFromJson(
                            doc.as<JsonObjectConst>(),
                            cardObj,
                            domain,
                            m_sessionId,
                            m_width,
                            m_height,
                            m_posterCapBytes,
                            defaultAccent565(),
                            outCard,
                            outInfo.itemId,
                            sizeof(outInfo.itemId),
                            outInfo.posterPath,
                            sizeof(outInfo.posterPath),
                            outInfo.streamPath,
                            sizeof(outInfo.streamPath),
                            outInfo.hasStream,
                            outInfo.streamMaxMs
                        );
                        m_activeItemStreamMaxMs.store(outInfo.streamMaxMs, std::memory_order_relaxed);
                        m_hasReadyItem.store(true, std::memory_order_release);
                        gotReadyItem = true;
                    }
                }
            } else {
                if (payloadLen > 0U && !mqttCli.drain(payloadLen, m_wantActive, 1000U)) {
                    break;
                }
            }
        } else {
            if (!mqttCli.connected()) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(8));
        }
    }

    if (!m_wantActive.load(std::memory_order_acquire) || shouldYieldShowcaseToWebUi(m_hasPsram)) {
        net::sendMqttDisconnectAndStop(mqttCli, m_wantActive);
        return AcquireOutcome::Aborted;
    }

    if (!gotReadyItem) {
        const bool sockDropped = !mqttCli.connected();
        net::sendMqttDisconnectAndStop(mqttCli, m_wantActive);
        if (!sawReadyFalse) {
            m_lastPublishedCfgGen = 0;
        }
        if (sawReadyFalse || sawServerOnline) {
            return AcquireOutcome::ServerWarming;
        }
        if (sockDropped) {
            return AcquireOutcome::BrokerUnreachable;
        }
        // Broker connected cleanly, but Showcase service published neither `status` nor `ready`
        // within `250 ms` -> chain into HTTP fallback in the same turn to verify `:8890` directly!
        return AcquireOutcome::BrokerUnreachable;
    }

    net::sendMqttUnsubscribePacket(mqttCli, 3, readyTopic, m_wantActive);
    char startCardCmd[256];
    snprintf(
        startCardCmd,
        sizeof(startCardCmd),
        "{\"action\":\"start_card\",\"item_id\":\"%s\",\"transport\":\"mqtt_tcp\",\"client_ip\":\"%s\",\"udp_ip\":\"%s\"}",
        outInfo.itemId,
        localIpBuf,
        localIpBuf
    );
    net::sendMqttPublishPacket(mqttCli, eventsTopic, startCardCmd, false, m_wantActive, m_ioScratch, m_ioScratchCap);
    net::sendMqttDisconnectAndStop(mqttCli, m_wantActive);
    return AcquireOutcome::ReadyItem;
#else
    (void)cfg;
    (void)extras;
    (void)domain;
    (void)reqLang;
    (void)startGen;
    (void)isFollowUpItemInTurn;
    return AcquireOutcome::BrokerUnreachable;
#endif
}

MediaShowcaseBase::AcquireOutcome MediaShowcaseBase::acquireItemViaHttp(
    const ConfigSlot& cfg,
    const ConfigExtras* extras,
    const char* domain,
    const char* reqLang,
    uint32_t startGen,
    CardSnapshot& outCard,
    AcquiredItemInfo& outInfo
) {
    outCard = CardSnapshot{};
    outInfo = AcquiredItemInfo{};
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!m_ioScratch || m_ioScratchCap < 512U) {
        return AcquireOutcome::ServerOffline;
    }

    char resolvedHost[64] = "";
    uint16_t resolvedPort = 8890;
    bool resolvedOnline = false;
    if (!net::ShowcaseDiscovery::resolveUrl(
            cfg.serverUrl,
            outInfo.baseServerUrl,
            sizeof(outInfo.baseServerUrl),
            resolvedHost,
            sizeof(resolvedHost),
            resolvedPort,
            resolvedOnline,
            nullptr,
            0,
            true,
            false) ||
        net::ShowcaseDiscovery::isAutoDiscoverableHost(resolvedHost)) {
        return AcquireOutcome::ServerOffline;
    }

    bool isHttps = false;
    (void)net::parseEndpointHostAndPort(
        outInfo.baseServerUrl,
        resolvedPort,
        resolvedHost,
        sizeof(resolvedHost),
        resolvedPort,
        &isHttps
    );

    uint16_t pw = 32;
    uint16_t ph = 32;
    computeCardPosterDims(m_width, m_height, pw, ph);
    const uint8_t effFps = resolveEffectiveVideoFps(cfg.videoFps, m_hasPsram);
    const char* sysCsv = extras ? extras->systems : "";
    const char* genCsv = extras ? extras->genres : "";
    const char* decCsv = extras ? extras->decades : "";

    size_t pos = 0U;
    const size_t scratchCap = m_ioScratchCap;
    m_ioScratch[0] = '\0';
    appendRawToBuf(m_ioScratch, scratchCap, pos, "{\"client_id\":\"");
    appendJsonEscapedToBuf(m_ioScratch, scratchCap, pos, m_sessionId);
    appendRawToBuf(m_ioScratch, scratchCap, pos, "\",\"width\":");
    appendUintToBuf(m_ioScratch, scratchCap, pos, m_width);
    appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"height\":");
    appendUintToBuf(m_ioScratch, scratchCap, pos, m_height);
    appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"poster_width\":");
    appendUintToBuf(m_ioScratch, scratchCap, pos, pw);
    appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"poster_height\":");
    appendUintToBuf(m_ioScratch, scratchCap, pos, ph);
    appendCharToBuf(m_ioScratch, scratchCap, pos, ',');
    appendShowcaseCommonConfigJson(
        m_ioScratch,
        scratchCap,
        pos,
        sysCsv,
        genCsv,
        decCsv,
        cfg.videoType,
        cfg.showPoster,
        cfg.showSynopsis,
        true,
        cfg.textScrollSpeed,
        cfg.jacketPanorama,
        cfg.marqueePanorama,
        cfg.snapshotPanoramaCount,
        cfg.showVideo,
        cfg.videoMaxDurationS,
        effFps,
        cfg.videoMode,
        cfg.colorBoost
    );
    appendRawToBuf(m_ioScratch, scratchCap, pos, ",\"lang\":\"");
    appendJsonEscapedToBuf(m_ioScratch, scratchCap, pos, reqLang);
    appendRawToBuf(m_ioScratch, scratchCap, pos, "\"}");

    struct ScopedHttpBusyGuard {
        std::atomic<bool>& flag;
        explicit ScopedHttpBusyGuard(std::atomic<bool>& f) : flag(f) {
            flag.store(true, std::memory_order_release);
        }
        ~ScopedHttpBusyGuard() {
            flag.store(false, std::memory_order_release);
        }
    } busyGuard{m_workerBusyOrHolding};

    const uint8_t rotSlots = m_rotationSlotCount.load(std::memory_order_relaxed);
    const uint32_t httpConnTimeoutMs = (rotSlots <= 1U) ? 1200U : 550U;

    auto parseFilteredNextDoc = [&](JsonObjectConst rootObj) -> AcquireOutcome {
        const char* status = rootObj["status"] | "warming";
        JsonObjectConst item = rootObj["item"].as<JsonObjectConst>();
        if (strcmp(status, "ready") != 0 || item.isNull()) {
            return AcquireOutcome::ServerWarming;
        }
        extractShowcaseItemFromJson(
            rootObj,
            item,
            domain,
            m_sessionId,
            m_width,
            m_height,
            m_posterCapBytes,
            defaultAccent565(),
            outCard,
            outInfo.itemId,
            sizeof(outInfo.itemId),
            outInfo.posterPath,
            sizeof(outInfo.posterPath),
            outInfo.streamPath,
            sizeof(outInfo.streamPath),
            outInfo.hasStream,
            outInfo.streamMaxMs
        );
        m_activeItemStreamMaxMs.store(outInfo.streamMaxMs, std::memory_order_relaxed);
        m_hasReadyItem.store(true, std::memory_order_release);
        return AcquireOutcome::ReadyItem;
    };

    if (!isHttps) {
        // Zero-heap, zero-TIME_WAIT plain HTTP path via `RawMqttSocket` (`SO_LINGER {1, 0}`)
        {
            char reqHdr[256];
            const int hdrLen = snprintf(
                reqHdr,
                sizeof(reqHdr),
                "POST /api/v1/%s/config HTTP/1.1\r\n"
                "Host: %s:%u\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                domain,
                resolvedHost,
                static_cast<unsigned>(resolvedPort),
                static_cast<unsigned>(pos)
            );
            if (hdrLen <= 0 || static_cast<size_t>(hdrLen) >= sizeof(reqHdr)) {
                return AcquireOutcome::ServerOffline;
            }
            net::RawMqttSocket cfgSock;
            int statusCode = 0;
            size_t contentLen = 0U;
            bool hasCl = false;
            bool chunked = false;
            const bool cfgOk = openRawHttpSession(
                cfgSock,
                resolvedHost,
                resolvedPort,
                reqHdr,
                static_cast<size_t>(hdrLen),
                m_ioScratch,
                pos,
                m_wantActive,
                httpConnTimeoutMs,
                1800U,
                statusCode,
                contentLen,
                hasCl,
                chunked
            );
            cfgSock.closeAbort();
            if (!cfgOk) {
                return m_wantActive.load(std::memory_order_acquire)
                    ? AcquireOutcome::ServerOffline
                    : AcquireOutcome::Aborted;
            }
        }

        if (!m_wantActive.load(std::memory_order_acquire) ||
            shouldYieldShowcaseToWebUi(m_hasPsram) ||
            m_cfgGen.load(std::memory_order_acquire) != startGen) {
            return AcquireOutcome::Aborted;
        }

        char getReq[256];
        const int getLen = snprintf(
            getReq,
            sizeof(getReq),
            "GET /api/v1/%s/next?client_id=%s HTTP/1.1\r\n"
            "Host: %s:%u\r\n"
            "Accept: application/json\r\n"
            "Connection: close\r\n\r\n",
            domain,
            m_sessionId,
            resolvedHost,
            static_cast<unsigned>(resolvedPort)
        );
        if (getLen <= 0 || static_cast<size_t>(getLen) >= sizeof(getReq)) {
            return AcquireOutcome::ServerOffline;
        }
        net::RawMqttSocket nextSock;
        int statusCode = 0;
        size_t contentLen = 0U;
        bool hasCl = false;
        bool chunked = false;
        if (!openRawHttpSession(
                nextSock,
                resolvedHost,
                resolvedPort,
                getReq,
                static_cast<size_t>(getLen),
                nullptr,
                0U,
                m_wantActive,
                httpConnTimeoutMs,
                2200U,
                statusCode,
                contentLen,
                hasCl,
                chunked)) {
            return AcquireOutcome::ServerOffline;
        }
        net::ShowcaseDiscovery::touchVerifiedHost(resolvedHost, resolvedPort);

        RawSocketJsonStream reader(nextSock, m_wantActive, contentLen, hasCl, chunked);
        StaticJsonDocument<JSON_OBJECT_SIZE(28)> nFilter;
        buildShowcaseHttpNextFilter(nFilter);
        ScratchSingleBlockAllocator scratchAlloc{m_ioScratch, m_ioScratchCap, false};
        BasicJsonDocument<ScratchSingleBlockAllocator> doc(m_ioScratchCap, scratchAlloc);
        const DeserializationError err = deserializeJson(
            doc,
            static_cast<Stream&>(reader),
            DeserializationOption::Filter(nFilter)
        );
        nextSock.closeAbort();
        if (err != DeserializationError::Ok) {
            return AcquireOutcome::ServerOffline;
        }
        return parseFilteredNextDoc(doc.as<JsonObjectConst>());
    }

#if SHOWCASE_SUPPORT_PSRAM_PRELOAD
    // TLS (`https://`) path via `SecureHttpClient` + filtered streaming JSON parse (PSRAM targets)
    net::SecureHttpOptions httpOpts;
    httpOpts.ownerId = net::OWNER_SHOWCASE;
    httpOpts.requestTimeoutMs = 3000;
    httpOpts.handshakeTimeoutSec = 2;
    httpOpts.keepAlive = false;

    {
        char cfgUrl[160];
        snprintf(cfgUrl, sizeof(cfgUrl), "%s/api/v1/%s/config", outInfo.baseServerUrl, domain);
        auto cfgResp = net::SecureHttpClient::post(cfgUrl, "application/json", m_ioScratch, httpOpts);
        if (!cfgResp.ok()) {
            return AcquireOutcome::ServerOffline;
        }
    }
    if (!m_wantActive.load(std::memory_order_acquire) ||
        shouldYieldShowcaseToWebUi(m_hasPsram) ||
        m_cfgGen.load(std::memory_order_acquire) != startGen) {
        return AcquireOutcome::Aborted;
    }

    char nextUrl[192];
    snprintf(nextUrl, sizeof(nextUrl), "%s/api/v1/%s/next?client_id=%s", outInfo.baseServerUrl, domain, m_sessionId);
    auto nextResp = net::SecureHttpClient::get(nextUrl, httpOpts);
    if (!nextResp.ok()) {
        return AcquireOutcome::ServerOffline;
    }
    net::ShowcaseDiscovery::touchVerifiedHost(resolvedHost, resolvedPort);

    StaticJsonDocument<JSON_OBJECT_SIZE(28)> nFilter;
    buildShowcaseHttpNextFilter(nFilter);
    ScratchSingleBlockAllocator scratchAlloc{m_ioScratch, m_ioScratchCap, false};
    BasicJsonDocument<ScratchSingleBlockAllocator> doc(m_ioScratchCap, scratchAlloc);
    if (deserializeJson(doc, nextResp.stream(), DeserializationOption::Filter(nFilter)) != DeserializationError::Ok) {
        return AcquireOutcome::ServerOffline;
    }
    return parseFilteredNextDoc(doc.as<JsonObjectConst>());
#else
    return AcquireOutcome::ServerOffline;
#endif
#else
    (void)cfg;
    (void)extras;
    (void)domain;
    (void)reqLang;
    (void)startGen;
    return AcquireOutcome::ServerOffline;
#endif
}

bool MediaShowcaseBase::fetchBinaryPosterRgb565(
    const char* baseServerUrl,
    const char* posterPath,
    uint16_t* dst565,
    size_t expectedBytes
) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!dst565 || expectedBytes == 0U || !posterPath || posterPath[0] == '\0') {
        return false;
    }

    char host[64] = "";
    uint16_t port = 8890;
    bool isHttps = false;
    const char* reqPath = posterPath;

    if (strncmp(posterPath, "http://", 7) == 0 || strncmp(posterPath, "https://", 8) == 0) {
        if (!net::parseEndpointHostAndPort(posterPath, 8890, host, sizeof(host), port, &isHttps)) {
            return false;
        }
        const char* afterScheme = strstr(posterPath, "://");
        const char* slash = afterScheme ? strchr(afterScheme + 3, '/') : nullptr;
        reqPath = slash ? slash : "/";
    } else {
        if (!baseServerUrl ||
            !net::parseEndpointHostAndPort(baseServerUrl, 8890, host, sizeof(host), port, &isHttps)) {
            return false;
        }
    }
    (void)net::ShowcaseDiscovery::resolveHostInPlace(host, sizeof(host), port, false);

    if (!isHttps) {
        char req[320];
        const int reqLen = snprintf(
            req,
            sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%u\r\n"
            "Connection: close\r\n\r\n",
            reqPath,
            host,
            static_cast<unsigned>(port)
        );
        if (reqLen <= 0 || static_cast<size_t>(reqLen) >= sizeof(req)) {
            return false;
        }
        net::RawMqttSocket sock;
        int statusCode = 0;
        size_t contentLen = 0U;
        bool hasCl = false;
        bool chunked = false;
        if (!openRawHttpSession(
                sock,
                host,
                port,
                req,
                static_cast<size_t>(reqLen),
                nullptr,
                0U,
                m_wantActive,
                650U,
                1500U,
                statusCode,
                contentLen,
                hasCl,
                chunked)) {
            return false;
        }
        if (hasCl && contentLen < expectedBytes) {
            sock.closeAbort();
            return false;
        }

        uint8_t* dstBytes = reinterpret_cast<uint8_t*>(dst565);
        bool ok = false;
        if (!chunked) {
            ok = sock.readExact(dstBytes, expectedBytes, m_wantActive, 1800U);
        } else {
            RawSocketJsonStream bodyStream(sock, m_wantActive, contentLen, hasCl, true);
            ok = readExactFromStream(bodyStream, dstBytes, expectedBytes, m_wantActive, 1800U);
        }
        sock.closeAbort();
        return ok;
    }

#if SHOWCASE_SUPPORT_PSRAM_PRELOAD
    char posterUrl[320];
    if (strncmp(posterPath, "http", 4) == 0) {
        copySafeStr(posterUrl, sizeof(posterUrl), posterPath);
    } else {
        snprintf(posterUrl, sizeof(posterUrl), "%s%s", baseServerUrl ? baseServerUrl : "", posterPath);
    }
    net::SecureHttpOptions httpOpts;
    httpOpts.ownerId = net::OWNER_SHOWCASE;
    httpOpts.requestTimeoutMs = 2200;
    httpOpts.handshakeTimeoutSec = 2;
    httpOpts.keepAlive = false;
    auto resp = net::SecureHttpClient::get(posterUrl, httpOpts);
    return resp.ok() && readExactFromStream(resp.stream(), reinterpret_cast<uint8_t*>(dst565), expectedBytes, m_wantActive, 2000U);
#else
    return false;
#endif
#else
    (void)baseServerUrl;
    (void)posterPath;
    (void)dst565;
    (void)expectedBytes;
    return false;
#endif
}


void MediaShowcaseBase::executeAcquiredItem(
    const ConfigSlot& cfg,
    const char* /*domain*/,
    uint32_t startGen,
    CardSnapshot& card,
    const AcquiredItemInfo& info
) {
    // Disarm any previous item's stream before touching poster/card buffers while holding the
    // last presented frame on screen (`Phase::StreamEndHold`) until the next Card + poster are ready.
    if (m_streamPlayer.isCore1Streaming() || m_liveCard.hasVideoFrame) {
        m_phase.store(static_cast<uint8_t>(Phase::StreamEndHold), std::memory_order_release);
        m_streamPlayer.resetCore1StreamState();
        m_nextStreamFrameDueMs.store(0U, std::memory_order_relaxed);
        m_publishedFramePtr.store(nullptr, std::memory_order_release);
        m_seq.fetch_add(1U, std::memory_order_acq_rel);
        m_liveCard.hasVideoFrame = false;
        m_seq.fetch_add(1U, std::memory_order_release);
        waitRenderBlitQuiescent();
    }
    m_streamPlayer.freeSpscRing();

    // Ensure `m_poster565` is available (e.g., on item 2..N of a multi-item rotation turn)
    const size_t expectedPosterBytes = static_cast<size_t>(card.posterW) * static_cast<size_t>(card.posterH) * sizeof(uint16_t);
    if (cfg.showPoster && !m_poster565 && m_posterCapBytes >= expectedPosterBytes && m_posterCapBytes > 0U) {
#if defined(ESP32) && SHOWCASE_SUPPORT_PSRAM_PRELOAD
        if (m_hasPsram) {
            m_poster565 = static_cast<uint16_t*>(heap_caps_malloc(m_posterCapBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
#endif
        if (!m_poster565) {
            m_poster565 = static_cast<uint16_t*>(malloc(m_posterCapBytes));
        }
        if (m_poster565) {
            memset(m_poster565, 0, m_posterCapBytes);
        }
    }

    if (cfg.showPoster &&
        !card.hasPoster &&
        m_poster565 &&
        expectedPosterBytes > 0U &&
        expectedPosterBytes <= m_posterCapBytes &&
        info.posterPath[0] != '\0') {
        if (fetchBinaryPosterRgb565(info.baseServerUrl, info.posterPath, m_poster565, expectedPosterBytes)) {
            card.hasPoster = true;
        }
    }

    // Publish Info Card to Core 1
    m_seq.fetch_add(1U, std::memory_order_acq_rel);
    m_liveCard = card;
    m_seq.fetch_add(1U, std::memory_order_release);
    m_statusState.store(0, std::memory_order_release);
    m_hasReadyItem.store(true, std::memory_order_release);
    m_readyStateReceived.store(true, std::memory_order_release);
    m_turnEvaluated.store(true, std::memory_order_release);
    m_phaseStartedAtMs = millis();
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

    // Compute full-synopsis Card dwell (`900 ms` start hold + full scroll + `700 ms` end hold)
    uint32_t cardWaitMs = 4500U;
    const uint32_t synGlyphs = static_cast<uint32_t>(countUtf8Glyphs(card.synopsis));
    const uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
    if (cfg.showSynopsis && synGlyphs > 0U && speedPx > 0U) {
        const uint32_t textW = synGlyphs * 6U;
        const uint32_t availW = static_cast<uint32_t>(
            engines::showcase::ShowcaseCardRenderer::computeCardTextViewportWidth(m_width, m_height, cfg.showPoster)
        );
        if (textW > availW) {
            const uint32_t scrollDistPx = (textW - availW) + (availW / 2U);
            const uint32_t synScrollMs = 900U + (scrollDistPx * 1000U) / speedPx + 700U;
            if (synScrollMs > cardWaitMs) {
                cardWaitMs = synScrollMs;
            }
        }
    }

#if defined(ESP32) && !defined(UNIT_TEST)
    struct ShowcaseStreamCtx {
        MediaShowcaseBase* self;
        uint32_t startGen;
    } ctx{this, startGen};

    auto checkStop = [](void* raw) -> net::StreamStopReason {
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

    const uint32_t cardT0 = millis();
    if (!info.hasStream || info.streamPath[0] == '\0' || info.baseServerUrl[0] == '\0') {
        while (checkStop(&ctx) == net::StreamStopReason::Continue &&
               (millis() - cardT0) < cardWaitMs) {
            vTaskDelay(pdMS_TO_TICKS(30));
        }
        return;
    }

    engines::showcase::ShowcaseStreamRunOptions runOpts{};
    runOpts.baseServerUrl = info.baseServerUrl;
    runOpts.streamPath = info.streamPath;
    runOpts.clientId = m_sessionId;
    runOpts.logTag = "Showcase";
    runOpts.surfaceForReadOnlyCheck = m_surface;
    runOpts.ioScratch = m_ioScratch;
    runOpts.ioScratchCap = m_ioScratchCap;
    runOpts.panelWidth = m_width;
    runOpts.panelHeight = m_height;
    runOpts.preStreamCardWaitMs = cardWaitMs;
    runOpts.cardStartedAtMs = cardT0;
    runOpts.maxStreamMs = std::max<uint32_t>(effectiveMaxStreamMs(), info.streamMaxMs);
    runOpts.hasPsram = m_hasPsram;
    runOpts.purgeOnComplete = true;
    runOpts.waitFirstFrameBeforeLive = false;
    runOpts.userCtx = &ctx;
    runOpts.checkStop = checkStop;
    runOpts.onBeforeLiveStream = [](void* raw) {
        auto* c = static_cast<ShowcaseStreamCtx*>(raw);
        c->self->freePosterAfterBlank();
    };
    runOpts.onEnterLiveStream = [](void* raw) {
        auto* c = static_cast<ShowcaseStreamCtx*>(raw);
        c->self->m_seq.fetch_add(1U, std::memory_order_acq_rel);
        c->self->m_liveCard.hasVideoFrame = true;
        c->self->m_seq.fetch_add(1U, std::memory_order_release);
        c->self->m_phase.store(static_cast<uint8_t>(Phase::LiveStream), std::memory_order_release);
    };
    runOpts.onExitLiveStream = [](void* raw) {
        auto* c = static_cast<ShowcaseStreamCtx*>(raw);
        c->self->m_phase.store(static_cast<uint8_t>(Phase::StreamEndHold), std::memory_order_release);
        c->self->m_seq.fetch_add(1U, std::memory_order_acq_rel);
        c->self->m_liveCard.hasVideoFrame = false;
        c->self->m_seq.fetch_add(1U, std::memory_order_release);
        c->self->waitRenderBlitQuiescent();
    };
    (void)m_streamPlayer.runStreamSession(runOpts);
#else
    (void)cfg;
    (void)startGen;
    (void)info;
#endif
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
    engines::showcase::ShowcaseCardRenderer::drawCardPhase(
        s,
        m_domain,
        m_statusState.load(std::memory_order_relaxed),
        card,
        m_poster565,
        m_cfgShowPoster.load(std::memory_order_relaxed),
        m_cfgShowSynopsis.load(std::memory_order_relaxed),
        m_titleStepPx,
        m_synScrollPx,
        elapsedMs
    );
}

void MediaShowcaseBase::update(EngineContext* context) {
    (void)context;
    ConfigSnapshotGuard guard = ::config.acquireSnapshot();
    const auto& snap = guard.get();
    const uint8_t rotSlots = static_cast<uint8_t>(snap.rotation.size());
    m_rotationSlotCount.store(rotSlots, std::memory_order_relaxed);

    const uint32_t now = millis();
    const uint32_t nowUs = micros();

    // Part A.2: 1:1 RPi `turn_evaluated` readiness gate on Core 1
    if (!m_turnEvaluated.load(std::memory_order_acquire)) {
        const bool hasReady = m_hasReadyItem.load(std::memory_order_acquire);
        if (hasReady) {
            m_turnEvaluated.store(true, std::memory_order_release);
        } else if (m_readyStateReceived.load(std::memory_order_acquire)) {
            m_turnEvaluated.store(true, std::memory_order_release);
            if (rotSlots > 1U) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
        } else if (rotSlots > 1U) {
            if (m_workerBusyOrHolding.load(std::memory_order_acquire)) {
                if ((now - m_activatedAtMs) >= 4500U || !m_taskAlive.load(std::memory_order_acquire)) {
                    m_turnEvaluated.store(true, std::memory_order_release);
                    m_skipTurnImmediately.store(true, std::memory_order_release);
                }
            } else {
                const uint32_t mqttConnMs = m_mqttConnectedAtMs.load(std::memory_order_acquire);
                const uint32_t graceMs = (m_consecutiveWarmSkips.load(std::memory_order_acquire) > 0U &&
                                          m_itemsPlayedInTurn.load(std::memory_order_acquire) == 0U)
                    ? 350U
                    : 0U;
                const bool retainedExpired = (mqttConnMs != 0U) && ((now - mqttConnMs) >= (250U + graceMs));
                const bool connectExpired = (mqttConnMs == 0U) && ((now - m_activatedAtMs) >= 850U);
                if (retainedExpired || connectExpired || !m_taskAlive.load(std::memory_order_acquire)) {
                    m_turnEvaluated.store(true, std::memory_order_release);
                    m_skipTurnImmediately.store(true, std::memory_order_release);
                }
            }
        }
    }

    const uint32_t s1 = m_seq.load(std::memory_order_acquire);
    if ((s1 & 1U) == 0U && s1 != m_lastRenderedCardSeq) {
        CardSnapshot temp = m_liveCard;
        const uint32_t s2 = m_seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            m_renderCard = temp;
            m_lastRenderedCardSeq = s1;
            m_cardAnimStartMs = now;
            m_cardLastTickUs = nowUs;
            m_synSubpixelRem = 0U;
            m_synScrollPx = 0U;
            m_titleSubpixelRem = 0U;
            m_titleStepPx = 0U;
            m_dirty = true;
        }
    }

    const uint8_t curPhase = m_phase.load(std::memory_order_acquire);
    if (curPhase != m_lastRenderedPhase) {
        m_lastRenderedPhase = curPhase;
        m_cardLastTickUs = nowUs;
        m_dirty = true;
    }

    if (curPhase == static_cast<uint8_t>(Phase::LiveStream)) {
        if (!m_streamPlayer.isCore1Streaming()) {
            if (m_publishedFramePtr.load(std::memory_order_acquire) != nullptr) {
                const uint32_t fSeq = m_frameSeq.load(std::memory_order_acquire);
                if ((fSeq & 1U) == 0U && fSeq != m_lastRenderedFrameSeq && m_renderCard.hasVideoFrame) {
                    m_dirty = true;
                }
            } else {
                m_dirty = false;
            }
        } else if (m_streamPlayer.shouldMarkCore1Dirty(now)) {
            m_dirty = true;
        }
    } else if (curPhase == static_cast<uint8_t>(Phase::StreamEndHold)) {
        // Hold final presented video frame cleanly with zero redraws during stream teardown / rotation handoff.
        m_dirty = false;
    } else if (curPhase == static_cast<uint8_t>(Phase::StreamBlank)) {
        // Hold clean black cut with zero redraw spin while Core 0 hands off to the stream.
    } else if (!m_renderCard.hasData) {
        if ((now - m_lastScrollStepMs) >= 30U) {
            m_lastScrollStepMs = now;
            m_dirty = true;
        }
    } else {
        const uint32_t dtUs = nowUs - m_cardLastTickUs;
        if (dtUs >= 8000U) {
            m_cardLastTickUs = nowUs;
            const uint32_t prevTitleStep = m_titleStepPx;
            const uint32_t prevSynPx = m_synScrollPx;

            (void)engines::showcase::ShowcaseCardRenderer::advanceCardScrollSubpixels(
                m_titleStepPx,
                m_titleSubpixelRem,
                dtUs,
                30U
            );

            if (m_cfgShowSynopsis.load(std::memory_order_relaxed) &&
                m_renderCard.synopsis[0] != '\0' &&
                (now - m_cardAnimStartMs) >= 900U) {
                const uint32_t speedPx = m_cfgSynopsisSpeedPx.load(std::memory_order_relaxed);
                (void)engines::showcase::ShowcaseCardRenderer::advanceCardScrollSubpixels(
                    m_synScrollPx,
                    m_synSubpixelRem,
                    dtUs,
                    speedPx
                );
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
    };

    const uint32_t elapsedMs = millis() - m_activatedAtMs;
    const Phase curPhase = static_cast<Phase>(m_phase.load(std::memory_order_acquire));

    if (curPhase == Phase::StreamEndHold) {
        m_dirty = false;
        m_renderedNewFrame = false;
        return;
    }

    if (curPhase == Phase::StreamBlank) {
        BlitSeqGuard blitGuard{m_renderBlitSeq};
        s->clear(0);
        m_dirty = false;
        m_renderedNewFrame = true;
        m_blankPresentedSeq.fetch_add(1U, std::memory_order_release);
        return;
    }

    if (curPhase == Phase::LiveStream) {
        if (m_streamPlayer.isCore1Streaming()) {
            BlitSeqGuard blitGuard{m_renderBlitSeq};
            const bool drew = m_streamPlayer.renderFrameCore1(s, m_width, m_height);
            m_dirty = false;
            m_renderedNewFrame = drew;
            return;
        }

        const uint16_t* framePtr = m_publishedFramePtr.load(std::memory_order_acquire);
        if (m_renderCard.hasVideoFrame && framePtr != nullptr) {
            const uint32_t fSeq1 = m_frameSeq.load(std::memory_order_acquire);
            if ((fSeq1 & 1U) != 0U) {
                m_dirty = false;
                m_renderedNewFrame = false;
                return;
            }
            BlitSeqGuard blitGuard{m_renderBlitSeq};
            const uint32_t fSeq2 = m_frameSeq.load(std::memory_order_acquire);
            const uint16_t* framePtr2 = m_publishedFramePtr.load(std::memory_order_acquire);
            if ((fSeq2 & 1U) != 0U || framePtr2 == nullptr) {
                m_dirty = false;
                m_renderedNewFrame = false;
                return;
            }
            if (s->width() != static_cast<int>(m_width) || s->height() != static_cast<int>(m_height)) {
                s->clear(0);
            }
            s->blit565(framePtr2, 0, 0, m_width, m_height);
            m_lastRenderedFrameSeq = fSeq2;
            m_consumedFrameSeq.store(fSeq2, std::memory_order_release);
            m_presentedStreamFrames.fetch_add(1, std::memory_order_relaxed);
            m_dirty = false;
            m_renderedNewFrame = true;
            return;
        }
        m_dirty = false;
        m_renderedNewFrame = false;
        return;
    }

    if (m_posterAliasesFrame && m_renderCard.hasPoster &&
        (m_frameSeq.load(std::memory_order_acquire) & 1U) != 0U) {
        m_dirty = false;
        m_renderedNewFrame = false;
        return;
    }

    BlitSeqGuard blitGuard{m_renderBlitSeq};
    if (m_posterAliasesFrame && m_renderCard.hasPoster &&
        (m_frameSeq.load(std::memory_order_acquire) & 1U) != 0U) {
        m_dirty = false;
        m_renderedNewFrame = false;
        return;
    }
    s->clear(0);
    drawCardPhase(s, m_renderCard, elapsedMs);

    m_dirty = false;
    m_renderedNewFrame = true;
}

EngineDescriptor RetroGamesEngineDescriptorHandler::getDescriptor() const {
    static constexpr FieldTableEntry kDomainFields[] = {
        ConfigField("systems", ConfigType::LIST, "Arcade & Console Systems", "Select one or more systems dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept),
        ConfigField("genres", ConfigType::LIST, "Game Genres", "Select one or more genres dynamically filtered by your chosen systems (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically filtered by your chosen systems (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept)
    };
    EngineDescriptor desc;
    initShowcaseCommonDescriptor(desc, "retro_games", "Retro Games Showcase", "arcade", kDomainFields, 3U);
    desc.factory = []() { return std::unique_ptr<IEngine>(new RetroGamesEngine()); };
    return desc;
}

EngineDescriptor MovieShowcaseEngineDescriptorHandler::getDescriptor() const {
    static constexpr FieldTableEntry kDomainFields[] = {
        ConfigField("genres", ConfigType::LIST, "Movie Genres", "Select one or more movie genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/movies/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/movies/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (trailer,scene,any)", "any", false, "", "", "", "any,trailer,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    EngineDescriptor desc;
    initShowcaseCommonDescriptor(desc, "movie_showcase", "Movie Showcase", "media", kDomainFields, 3U);
    desc.factory = []() { return std::unique_ptr<IEngine>(new MovieShowcaseEngine()); };
    return desc;
}

EngineDescriptor SeriesShowcaseEngineDescriptorHandler::getDescriptor() const {
    static constexpr FieldTableEntry kDomainFields[] = {
        ConfigField("genres", ConfigType::LIST, "Series Genres", "Select one or more series genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/series/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/series/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,trailer,any)", "any", false, "", "", "", "any,opening,trailer", "", false, "", ValidationPolicy::FallbackDefault)
    };
    EngineDescriptor desc;
    initShowcaseCommonDescriptor(desc, "series_showcase", "Series Showcase", "media", kDomainFields, 3U);
    desc.factory = []() { return std::unique_ptr<IEngine>(new SeriesShowcaseEngine()); };
    return desc;
}

EngineDescriptor AnimeShowcaseEngineDescriptorHandler::getDescriptor() const {
    static constexpr FieldTableEntry kDomainFields[] = {
        ConfigField("genres", ConfigType::LIST, "Anime Genres", "Select one or more anime genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/anime/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/anime/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,ending,scene,any)", "opening", false, "", "", "", "any,opening,ending,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    EngineDescriptor desc;
    initShowcaseCommonDescriptor(desc, "anime_showcase", "Anime Showcase", "media", kDomainFields, 3U);
    desc.factory = []() { return std::unique_ptr<IEngine>(new AnimeShowcaseEngine()); };
    return desc;
}
