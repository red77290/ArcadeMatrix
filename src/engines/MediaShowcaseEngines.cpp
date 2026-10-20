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

} // namespace

MediaShowcaseBase::MediaShowcaseBase(ShowcaseDomainId domain) : m_domain(domain) {
    const char* slug = domainSlug();
    snprintf(m_sessionId, sizeof(m_sessionId), "esp32-%s-default", slug);
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

uint16_t MediaShowcaseBase::defaultAccent565() const {
    switch (m_domain) {
        case ShowcaseDomainId::Games:  return 0xFD20; // Arcade Gold/Orange
        case ShowcaseDomainId::Movies: return 0xF945; // Cinema Crimson
        case ShowcaseDomainId::Series: return 0x06BF; // Series Cyan
        case ShowcaseDomainId::Anime:  return 0xF81F; // Anime Magenta
        default:                       return 0xFD20;
    }
}

void MediaShowcaseBase::computeCardPosterDims(uint16_t panelW, uint16_t panelH, uint16_t& outW, uint16_t& outH) {
    if (panelH > panelW) {
        // Vertical TATE mode (e.g. 64x128, 64x256, 32x128)
        uint16_t side = (panelW >= 64) ? 40 : 24;
        outW = side;
        outH = side;
    } else if (panelW == panelH) {
        // Square panel (64x64)
        outW = 32;
        outH = 32;
    } else if (panelH >= 64) {
        // Tall / XL landscape (128x64, 256x64)
        outW = 48;
        outH = 64;
    } else {
        // Standard landscape (128x32)
        outW = 32;
        outH = 32;
    }
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

    copySafeStr(slot.primarySource, sizeof(slot.primarySource), config->getString("primary_source", "auto").c_str());
    copySafeStr(slot.scraperUser, sizeof(slot.scraperUser), config->getString("scraper_user", "").c_str());
    copySafeStr(slot.scraperPass, sizeof(slot.scraperPass), config->getString("scraper_pass", "").c_str());
    copySafeStr(slot.scraperApiKey, sizeof(slot.scraperApiKey), config->getString("scraper_api_key", "").c_str());
    copySafeStr(slot.lang, sizeof(slot.lang), config->getString("lang", "system").c_str());
    copySafeStr(slot.videoMode, sizeof(slot.videoMode), config->getString("video_mode", "auto_action").c_str());
    copySafeStr(slot.systems, sizeof(slot.systems), config->getString("systems", "").c_str());
    copySafeStr(slot.genres, sizeof(slot.genres), config->getString("genres", "").c_str());
    copySafeStr(slot.decades, sizeof(slot.decades), config->getString("decades", "").c_str());
    copySafeStr(slot.videoType, sizeof(slot.videoType), config->getString("video_type", "any").c_str());

    slot.itemsPerRotation = static_cast<uint8_t>(std::max(1, std::min(10, config->getInt("items_per_rotation", 3))));
    slot.imageCount = static_cast<uint8_t>(std::max(0, std::min(5, config->getInt("image_count", 2))));
    slot.snapshotPanoramaCount = static_cast<uint8_t>(std::max(0, std::min(5, config->getInt("snapshot_panorama_count", 0))));
    slot.cardDurationS = static_cast<uint8_t>(std::max(3, std::min(30, config->getInt("card_duration_s", 7))));
    slot.snapshotDurationS = static_cast<uint8_t>(std::max(2, std::min(20, config->getInt("snapshot_duration_s", 4))));
    slot.videoMaxDurationS = static_cast<uint8_t>(std::max(2, std::min(20, config->getInt("video_max_duration_s", 6))));
    slot.videoFps = static_cast<uint8_t>(std::max(8, std::min(30, config->getInt("video_fps", 15))));

    slot.showPoster = config->getBool("show_poster", true);
    slot.showSynopsis = config->getBool("show_synopsis", true);
    slot.showSnapshot = config->getBool("show_snapshot", true);
    slot.jacketPanorama = config->getBool("jacket_panorama", true);
    slot.marqueePanorama = config->getBool("marquee_panorama", false);
    slot.showVideo = config->getBool("show_video", true);
    slot.colorBoost = config->getBool("color_boost", true);

    uint32_t perItemMs = static_cast<uint32_t>(slot.cardDurationS + slot.snapshotDurationS + slot.videoMaxDurationS + 18) * 1000U;
    m_fallbackCycleMs = std::max<uint32_t>(20000U, perItemMs * slot.itemsPerRotation);

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
    freeBuffers();
}

bool MediaShowcaseBase::shutdownForDestruction() {
    m_wantActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
    stopWorkerTaskBounded(2000);
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
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
        }
        return;
    }

    net::SecureHttpOptions httpOpts;
    httpOpts.ownerId = net::OWNER_SHOWCASE;
    httpOpts.requestTimeoutMs = 3500;
    httpOpts.keepAlive = false;

    // Step 1: Proactive Session Registration (`POST /api/v1/{domain}/session`)
    String sessionUrl = String(cfg.serverUrl) + "/api/v1/" + domain + "/session";
    String reqLang = String(cfg.lang);
    if (reqLang == "system" || reqLang.length() == 0) {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        reqLang = guard.get().system.lang;
        if (reqLang.length() == 0) reqLang = "en";
    }

    String postBody;
    postBody.reserve(560);
    postBody += "{\"client_id\":\"";
    postBody += m_sessionId;
    postBody += "\",\"count\":";
    postBody += String(cfg.itemsPerRotation);
    postBody += ",\"width\":";
    postBody += String(m_width);
    postBody += ",\"height\":";
    postBody += String(m_height);
    postBody += ",\"image_count\":";
    postBody += String(cfg.showSnapshot ? cfg.imageCount : 0);
    postBody += ",\"jacket_panorama\":";
    postBody += (cfg.jacketPanorama ? "true" : "false");
    postBody += ",\"marquee_panorama\":";
    postBody += (cfg.marqueePanorama ? "true" : "false");
    postBody += ",\"snapshot_panorama_count\":";
    postBody += String(cfg.snapshotPanoramaCount);
    postBody += ",\"primary_source\":\"";
    postBody += cfg.primarySource;
    postBody += "\",\"scraper_user\":\"";
    postBody += cfg.scraperUser;
    postBody += "\",\"scraper_pass\":\"";
    postBody += cfg.scraperPass;
    postBody += "\",\"scraper_api_key\":\"";
    postBody += cfg.scraperApiKey;
    postBody += "\",\"video_fps\":";
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
        auto sessResp = net::SecureHttpClient::post(sessionUrl, "application/json", postBody, httpOpts);
        if (sessResp.ok()) {
            DynamicJsonDocument sDoc(512);
            if (deserializeJson(sDoc, sessResp.stream()) == DeserializationError::Ok) {
                const char* sid = sDoc["session_id"] | "";
                if (sid[0] != '\0') {
                    copySafeStr(m_sessionId, sizeof(m_sessionId), sid);
                }
            }
        }
    }

    if (!m_wantActive.load(std::memory_order_acquire)) return;

    // Step 2: Fast LAN Batch Check (`GET /api/v1/{domain}/batch?session_id=...`)
    String batchUrl = String(cfg.serverUrl) + "/api/v1/" + domain + "/batch?session_id=" + m_sessionId;
    DynamicJsonDocument batchDoc(6144);
    bool batchReady = false;

    while (m_wantActive.load(std::memory_order_acquire) && !batchReady) {
        auto batchResp = net::SecureHttpClient::get(batchUrl, httpOpts);
        if (!batchResp.ok()) {
            m_statusState.store(2, std::memory_order_release); // NetworkOffline
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
            return;
        }
        batchDoc.clear();
        if (deserializeJson(batchDoc, batchResp.stream()) != DeserializationError::Ok) {
            m_statusState.store(2, std::memory_order_release);
            return;
        }
        const char* status = batchDoc["status"] | "ready";
        if (strcmp(status, "warming") == 0) {
            m_statusState.store(1, std::memory_order_release); // ServerWarming
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                // Multiple engines in rotation: skip turn immediately without freezing
                m_skipTurnImmediately.store(true, std::memory_order_release);
                return;
            }
            // Single engine in rotation: show warming status while polling
            m_warmingFallback.store(true, std::memory_order_release);
#if defined(ESP32)
            for (int w = 0; w < 15 && m_wantActive.load(std::memory_order_acquire); ++w) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
#else
            break;
#endif
        } else {
            m_warmingFallback.store(false, std::memory_order_release);
            m_statusState.store(0, std::memory_order_release); // LiveReady
            batchReady = true;
        }
    }

    JsonArray items = batchDoc["items"].as<JsonArray>();
    if (items.isNull() || items.size() == 0) return;

    uint8_t shown = 0;
    for (JsonObject item : items) {
        if (!m_wantActive.load(std::memory_order_acquire)) return;
        if (shown >= cfg.itemsPerRotation) break;

        CardSnapshot nextCard;
        copySafeStr(nextCard.title, sizeof(nextCard.title), item["title"] | "UNKNOWN");
        const char* yrStr = item["year"] | "";
        copySafeStr(nextCard.year, sizeof(nextCard.year), yrStr);
        const char* badge = item["badge"] | (item["system_badge"] | "");
        if (badge[0] == '\0') badge = domain;
        copySafeStr(nextCard.badge, sizeof(nextCard.badge), badge);

        const char* ratingStr = item["rating"] | "";
        copySafeStr(nextCard.rating, sizeof(nextCard.rating), ratingStr);
        copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), item["synopsis"] | "");
        nextCard.badgeColor565 = parseHexColor565(item["badge_color"] | (item["accent_color"] | nullptr), defaultAccent565());
        computeCardPosterDims(m_width, m_height, nextCard.posterW, nextCard.posterH);
        nextCard.hasData = true;
        nextCard.hasPoster = false;
        nextCard.hasVideoFrame = false;

        JsonObject assetsObj = item["assets"].as<JsonObject>();

        // Optional Poster RGB565 download (adaptive mini-jacket up to 48x64x2 = 6144 bytes)
        const char* posterPath = assetsObj["poster_rgb565"] | (item["poster_rgb565_url"] | "");
        size_t expectedPosterBytes = static_cast<size_t>(nextCard.posterW) * static_cast<size_t>(nextCard.posterH) * 2U;
        if (cfg.showPoster && m_poster565 && posterPath[0] != '\0' && expectedPosterBytes <= (48U * 64U * 2U)) {
            String posterUrl = (strncmp(posterPath, "http", 4) == 0)
                ? String(posterPath)
                : (String(cfg.serverUrl) + posterPath);
            auto pResp = net::SecureHttpClient::get(posterUrl, httpOpts);
            if (pResp.ok()) {
                if (readExactFromStream(pResp.stream(), reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes, m_wantActive)) {
                    nextCard.hasPoster = true;
                }
            }
        }

        // Publish CardSnapshot via Seqlock
        m_seq.fetch_add(1, std::memory_order_acq_rel);
        m_liveCard = nextCard;
        m_seq.fetch_add(1, std::memory_order_release);
        m_phaseStartedAtMs = millis();
        m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

        // Phase 1: Organic Rich Card dwell (waits until 100% of the synopsis has scrolled across the DMD)
        uint32_t baseCardMs = static_cast<uint32_t>(cfg.cardDurationS) * 1000U;
        uint32_t synChars = static_cast<uint32_t>(strlen(nextCard.synopsis));
        uint32_t cardWaitMs = baseCardMs;
        if (cfg.showSynopsis && synChars > 0) {
            uint32_t scrollDistPx = synChars * 6U + 80U;
            uint32_t synScrollMs = 900U + (scrollDistPx * 1000U) / 22U + 500U;
            if (synScrollMs > cardWaitMs) {
                cardWaitMs = synScrollMs;
            }
        }
        uint32_t t0 = millis();
        while (m_wantActive.load(std::memory_order_acquire) && (millis() - t0) < cardWaitMs) {
#if defined(ESP32)
            vTaskDelay(pdMS_TO_TICKS(40));
#else
            break;
#endif
        }

        // Helper lambda to stream a `.rgb565.stream` (`DMDS`) file into `m_frame565`
        auto streamDmdsAsset = [&](const char* relOrAbsUrl, Phase targetPhase, uint32_t maxPlayMs) {
            if (!relOrAbsUrl || relOrAbsUrl[0] == '\0' || !m_frame565) return;
            String fullUrl = (strncmp(relOrAbsUrl, "http", 4) == 0)
                ? String(relOrAbsUrl)
                : (String(cfg.serverUrl) + relOrAbsUrl);
            auto sResp = net::SecureHttpClient::get(fullUrl, httpOpts);
            if (!sResp.ok()) return;
            Stream& st = sResp.stream();
            uint8_t hdr[16];
            if (!readExactFromStream(st, hdr, sizeof(hdr), m_wantActive)) return;
            if (memcmp(hdr, "DMDS", 4) != 0) return;

            uint8_t fps = hdr[5] > 0 ? hdr[5] : 15;
            uint16_t w = static_cast<uint16_t>(hdr[6] | (hdr[7] << 8));
            uint16_t h = static_cast<uint16_t>(hdr[8] | (hdr[9] << 8));
            uint16_t totalFrames = static_cast<uint16_t>(hdr[10] | (hdr[11] << 8));
            uint16_t delayMs = static_cast<uint16_t>(hdr[12] | (hdr[13] << 8));
            uint32_t frameDelayMs = (delayMs > 0) ? delayMs : (1000U / fps);
            size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2U;
            size_t maxBufBytes = m_framePixels * sizeof(uint16_t);
            if (frameBytes == 0 || frameBytes > maxBufBytes || totalFrames == 0) return;

            m_phase.store(static_cast<uint8_t>(targetPhase), std::memory_order_release);
            uint32_t tStart = millis();
            for (uint16_t f = 0; f < totalFrames; ++f) {
                if (!m_wantActive.load(std::memory_order_acquire)) break;
                if ((millis() - tStart) >= maxPlayMs) break;
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
        };

        // Phase 2: Top-to-Bottom Scrolling Panorama Sequence (`jacket` -> `marquee` -> `snapshots`)
        uint8_t maxPanStreams = static_cast<uint8_t>(
            (cfg.jacketPanorama ? 1 : 0) +
            (cfg.marqueePanorama ? 1 : 0) +
            std::max(cfg.snapshotPanoramaCount, cfg.showSnapshot ? cfg.imageCount : uint8_t(0))
        );
        if (m_wantActive.load(std::memory_order_acquire) && maxPanStreams > 0 && m_frame565) {
            JsonArray panStreams = assetsObj["panorama_streams_rgb565"].as<JsonArray>();
            uint8_t imgIdx = 0;
            for (JsonVariant v : panStreams) {
                if (!m_wantActive.load(std::memory_order_acquire)) break;
                if (imgIdx >= maxPanStreams) break;
                const char* panPath = v.as<const char*>() ? v.as<const char*>() : "";
                streamDmdsAsset(panPath, Phase::SnapshotPan, static_cast<uint32_t>(cfg.snapshotDurationS) * 1000U);
                imgIdx++;
            }
        }

        // Phase 3: Video Stream (`.rgb565.stream` `DMDS`) — skipped immediately if `has_video == false`
        bool hasVideo = item["has_video"] | false;
        const char* vidPath = assetsObj["video_stream_rgb565"] | (item["video_stream_url"] | "");
        if (m_wantActive.load(std::memory_order_acquire) && cfg.showVideo && hasVideo && m_hasPsram && m_frame565 && vidPath[0] != '\0') {
            streamDmdsAsset(vidPath, Phase::VideoStream, static_cast<uint32_t>(cfg.videoMaxDurationS) * 1000U);
        }

        shown++;
    }

    m_turnDone.store(true, std::memory_order_release);
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
        if (s1 == s2 && temp.hasData) {
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

    uint32_t activeScrollMs = (elapsedMs > 900U) ? (elapsedMs - 900U) : 0U;
    int scrollPx = static_cast<int>((activeScrollMs * 22U) / 1000U);
    s->setCursor((minX + 6) - scrollPx, y);
    s->print(text);
}

void MediaShowcaseBase::drawCardPhase(IDrawingSurface* s, const CardSnapshot& card, uint32_t elapsedMs) {
    int mW = s->width();
    int mH = s->height();
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];

    // If no live item has been received yet, render a clean connection status screen (zero fake mock titles)
    if (!card.hasData) {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        Lang sysLang = I18n::parseLang(guard.get().system.lang.c_str());
        uint8_t st = m_statusState.load(std::memory_order_relaxed);
        const char* statusText = (st == 3)
            ? I18n::getShowcaseUnconfiguredLabel(sysLang)
            : (st == 1)
                ? I18n::getShowcaseWarmingLabel(sysLang)
                : I18n::getShowcaseOfflineLabel(sysLang);
        uint16_t accent = defaultAccent565();
        const char* slug = domainSlug();
        s->fillRect(0, 0, mW, 10, s->color565(18, 22, 34));
        s->drawFastHLine(0, 10, mW, accent);
        s->setTextSize(1);
        s->setTextColor(accent);
        s->setCursor(4, 1);
        s->print(slug);
        int textY = std::max(13, (mH / 2) - 3);
        drawScrollingLine(s, statusText, 4, mW - 4, textY, 0xFFE0, elapsedMs);
        return;
    }

    uint32_t cardElapsedMs = (millis() >= m_phaseStartedAtMs) ? (millis() - m_phaseStartedAtMs) : elapsedMs;

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
                drawSinglePassSynopsis(s, card.synopsis, 2, mW - 2, sepY + 3, 0xAD75, cardElapsedMs);
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
            drawSinglePassSynopsis(s, card.synopsis, 2, mW - 2, 49, 0xAD75, cardElapsedMs);
        }
        return;
    }

    // 3. Tall / XL Landscape (e.g. 128x64, 256x64) -> 48x64 mini-jacket + spacious metadata
    if (mH >= 64) {
        int textX = cfg.showPoster ? (static_cast<int>(card.posterW) + 3) : 3;
        drawScrollingLine(s, card.title, textX, mW - 2, 13, 0xFFFF, cardElapsedMs);
        s->drawFastHLine(textX, 34, mW - textX - 2, s->color565(35, 45, 65));
        if (cfg.showSynopsis && card.synopsis[0] != '\0') {
            drawSinglePassSynopsis(s, card.synopsis, textX, mW - 2, 42, 0xAD75, cardElapsedMs);
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

    // Bottom Line: Complete single-pass synopsis or warming status
    if (m_warmingFallback.load(std::memory_order_relaxed)) {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        Lang sysLang = I18n::parseLang(guard.get().system.lang.c_str());
        drawScrollingLine(s, I18n::getShowcaseWarmingLabel(sysLang), textX, mW - 2, 22, 0xFFE0, cardElapsedMs);
    } else if (cfg.showSynopsis && card.synopsis[0] != '\0') {
        drawSinglePassSynopsis(s, card.synopsis, textX, mW - 2, 22, 0xAD75, cardElapsedMs);
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

    if ((curPhase == Phase::SnapshotPan || curPhase == Phase::VideoStream) &&
        m_renderCard.hasVideoFrame && m_frame565 != nullptr) {
        s->blit565(m_frame565, 0, 0, m_width, m_height);
        if (curPhase == Phase::SnapshotPan && m_renderCard.badge[0] != '\0') {
            int bw = static_cast<int>(strlen(m_renderCard.badge)) * 6 + 4;
            s->fillRect(0, 0, bw, 9, 0x0000);
            s->setTextSize(1);
            s->setTextColor(m_renderCard.badgeColor565);
            s->setCursor(2, 1);
            s->print(m_renderCard.badge);
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
        ConfigField("server_url", ConfigType::STRING, "Showcase Server URL", "HTTP base URL of the DMD Media Showcase container", "http://127.0.0.1:8080", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("primary_source", ConfigType::ENUM, "Primary Scraper Source", "Preferred primary source (other sources used automatically as fallback)", "auto", false, "", "", "", "auto,screenscraper,arcadeitalia,libretro", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("scraper_user", ConfigType::STRING, "Scraper Username", "Optional account login (e.g. ScreenScraper.fr user)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("scraper_pass", ConfigType::STRING, "Scraper Password", "Optional account password (e.g. ScreenScraper.fr password)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("scraper_api_key", ConfigType::STRING, "Scraper API Key", "Optional API key or devid:devpassword", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("items_per_rotation", ConfigType::INTEGER, "Items per Rotation", "Number of items displayed before rotating to the next screen", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width cover/jacket from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Marquee / Banner Panorama", "Scroll the arcade marquee or title banner from top to bottom after the jacket", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional gameplay snapshots to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("image_count", ConfigType::INTEGER, "Scrolling Images per Item", "Number of top-to-bottom panorama images displayed per item (0..5)", "2", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("card_duration_s", ConfigType::INTEGER, "Card Duration (s)", "Minimum seconds to display the card (auto-extended until 100% of synopsis scrolls)", "7", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("snapshot_duration_s", ConfigType::INTEGER, "Image Scroll Duration (s)", "Seconds per top-to-bottom panorama image", "4", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "6", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "pixel_art", false, "", "", "", "auto_action,pixel_art,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_snapshot", ConfigType::BOOLEAN, "Show Panorama Images", "Display top-to-bottom scrolling panorama images after the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("systems", ConfigType::STRING, "Arcade & Console Systems", "Optional system filter across 28 systems (mame,neogeo,cps1,cps2,cps3,fbneo,naomi,atomiswave,model2,model3,snes,nes,megadrive,mastersystem,dreamcast,saturn,pcengine,psx,psp,n64,nds,gb,gbc,gba,atari2600,atari7800,lynx,amiga)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("genres", ConfigType::STRING, "Game Genres", "Optional genre filter across 16 categories (action,platformer,fighting,beat-em-up,shoot-em-up,run-and-gun,rpg,jrpg,adventure,racing,sports,puzzle,strategy,simulation,horror,pinball)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::STRING, "Decades", "Optional decade filter (1970s,1980s,1990s,2000s)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept)
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
        ConfigField("server_url", ConfigType::STRING, "Showcase Server URL", "HTTP base URL of the DMD Media Showcase container", "http://127.0.0.1:8080", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("primary_source", ConfigType::ENUM, "Primary Scraper Source", "Preferred primary source (other sources used automatically as fallback)", "auto", false, "", "", "", "auto,tmdb,wikipedia", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("scraper_api_key", ConfigType::STRING, "TMDB API Key", "Optional TMDB v3 API key", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("items_per_rotation", ConfigType::INTEGER, "Items per Rotation", "Number of items displayed before rotating to the next screen", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width movie poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the movie backdrop banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional movie stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("image_count", ConfigType::INTEGER, "Scrolling Images per Item", "Number of top-to-bottom panorama images displayed per item (0..5)", "2", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("card_duration_s", ConfigType::INTEGER, "Card Duration (s)", "Minimum seconds to display the card (auto-extended until 100% of synopsis scrolls)", "7", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("snapshot_duration_s", ConfigType::INTEGER, "Image Scroll Duration (s)", "Seconds per top-to-bottom panorama image", "4", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "6", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "auto_action", false, "", "", "", "auto_action,cinema,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_snapshot", ConfigType::BOOLEAN, "Show Panorama Images", "Display top-to-bottom scrolling panorama images after the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::STRING, "Movie Genres", "Optional genre filter across 23 categories (action,adventure,animation,biography,comedy,crime,cyberpunk,documentary,drama,family,fantasy,film-noir,history,horror,martial-arts,musical,mystery,post-apocalyptic,romance,sci-fi,sport,thriller,war,western)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::STRING, "Decades", "Optional decade filter (1970s,1980s,1990s,2000s,2010s,2020s)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
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
        ConfigField("server_url", ConfigType::STRING, "Showcase Server URL", "HTTP base URL of the DMD Media Showcase container", "http://127.0.0.1:8080", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("primary_source", ConfigType::ENUM, "Primary Scraper Source", "Preferred primary source (other sources used automatically as fallback)", "auto", false, "", "", "", "auto,tvmaze,tmdb,tvdb", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("scraper_api_key", ConfigType::STRING, "TMDB / TVDB API Key", "Optional TMDB or TheTVDB API key", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("items_per_rotation", ConfigType::INTEGER, "Items per Rotation", "Number of items displayed before rotating to the next screen", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width series poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the series banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional episode stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("image_count", ConfigType::INTEGER, "Scrolling Images per Item", "Number of top-to-bottom panorama images displayed per item (0..5)", "2", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("card_duration_s", ConfigType::INTEGER, "Card Duration (s)", "Minimum seconds to display the card (auto-extended until 100% of synopsis scrolls)", "7", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("snapshot_duration_s", ConfigType::INTEGER, "Image Scroll Duration (s)", "Seconds per top-to-bottom panorama image", "4", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "6", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "auto_action", false, "", "", "", "auto_action,cinema,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_snapshot", ConfigType::BOOLEAN, "Show Panorama Images", "Display top-to-bottom scrolling panorama images after the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::STRING, "Series Genres", "Optional genre filter across 24 categories (action,adventure,animation,anime,comedy,crime,cyberpunk,documentary,drama,espionage,family,fantasy,history,horror,legal,medical,mystery,political,romance,sci-fi,sitcom,supernatural,thriller,war,western)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::STRING, "Decades", "Optional decade filter (1980s,1990s,2000s,2010s,2020s)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
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
        ConfigField("server_url", ConfigType::STRING, "Showcase Server URL", "HTTP base URL of the DMD Media Showcase container", "http://127.0.0.1:8080", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("primary_source", ConfigType::ENUM, "Primary Scraper Source", "Preferred primary source (other sources used automatically as fallback)", "auto", false, "", "", "", "auto,animethemes,jikan,tmdb", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("scraper_api_key", ConfigType::STRING, "TMDB / TVDB API Key", "Optional TMDB or TheTVDB API key", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("items_per_rotation", ConfigType::INTEGER, "Items per Rotation", "Number of items displayed before rotating to the next screen", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width anime key visual from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the anime banner from top to bottom after the key visual", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional anime stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("image_count", ConfigType::INTEGER, "Scrolling Images per Item", "Number of top-to-bottom panorama images displayed per item (0..5)", "2", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("card_duration_s", ConfigType::INTEGER, "Card Duration (s)", "Minimum seconds to display the card (auto-extended until 100% of synopsis scrolls)", "7", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("snapshot_duration_s", ConfigType::INTEGER, "Image Scroll Duration (s)", "Seconds per top-to-bottom panorama image", "4", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "6", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "anime", false, "", "", "", "auto_action,anime,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_snapshot", ConfigType::BOOLEAN, "Show Panorama Images", "Display top-to-bottom scrolling panorama images after the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::STRING, "Anime Genres", "Optional genre filter across 31 categories (action,adventure,comedy,drama,fantasy,sci-fi,mystery,romance,supernatural,suspense,horror,Sports,award-winning,gourmet,slice-of-life,mecha,cyberpunk,isekai,mahou-shoujo,martial-arts,military,music,mythology,parody,psychological,racing,samurai,school,space,super-power,vampire,shonen,shojo,seinen,josei)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::STRING, "Decades", "Optional decade filter (1970s,1980s,1990s,2000s,2010s,2020s)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,ending,scene,any)", "opening", false, "", "", "", "any,opening,ending,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new AnimeShowcaseEngine()); };
    return desc;
}
