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
    loadOfflineDemoCard(m_liveCard, static_cast<uint8_t>(Lang::EN));
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

void MediaShowcaseBase::loadOfflineDemoCard(CardSnapshot& out, uint8_t langEnumVal) const {
    Lang lang = static_cast<Lang>(langEnumVal);
    out.badgeColor565 = defaultAccent565();
    out.hasPoster = false;
    out.hasVideoFrame = false;
    switch (m_domain) {
        case ShowcaseDomainId::Games:
            copySafeStr(out.title, sizeof(out.title), "CHRONO TRIGGER");
            copySafeStr(out.year, sizeof(out.year), "1995");
            copySafeStr(out.badge, sizeof(out.badge), "SNES");
            copySafeStr(out.rating, sizeof(out.rating), "9.8");
            break;
        case ShowcaseDomainId::Movies:
            copySafeStr(out.title, sizeof(out.title), "BLADE RUNNER 2049");
            copySafeStr(out.year, sizeof(out.year), "2017");
            copySafeStr(out.badge, sizeof(out.badge), "SCI-FI");
            copySafeStr(out.rating, sizeof(out.rating), "8.6");
            break;
        case ShowcaseDomainId::Series:
            copySafeStr(out.title, sizeof(out.title), "SEVERANCE");
            copySafeStr(out.year, sizeof(out.year), "2022");
            copySafeStr(out.badge, sizeof(out.badge), "TV");
            copySafeStr(out.rating, sizeof(out.rating), "8.9");
            break;
        case ShowcaseDomainId::Anime:
            copySafeStr(out.title, sizeof(out.title), "COWBOY BEBOP");
            copySafeStr(out.year, sizeof(out.year), "1998");
            copySafeStr(out.badge, sizeof(out.badge), "ANIME");
            copySafeStr(out.rating, sizeof(out.rating), "9.4");
            break;
    }
    copySafeStr(out.synopsis, sizeof(out.synopsis), I18n::getShowcaseDemoSynopsis(static_cast<uint8_t>(m_domain), lang));
}

bool MediaShowcaseBase::allocateBuffers() {
    freeBuffers();
    size_t posterBytes = 32 * 32 * sizeof(uint16_t);
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

    copySafeStr(slot.lang, sizeof(slot.lang), config->getString("lang", "system").c_str());
    copySafeStr(slot.videoMode, sizeof(slot.videoMode), config->getString("video_mode", "auto_action").c_str());
    copySafeStr(slot.systems, sizeof(slot.systems), config->getString("systems", "mame,neogeo,snes,megadrive,psx").c_str());
    copySafeStr(slot.genres, sizeof(slot.genres), config->getString("genres", "").c_str());
    copySafeStr(slot.decades, sizeof(slot.decades), config->getString("decades", "").c_str());
    copySafeStr(slot.videoType, sizeof(slot.videoType), config->getString("video_type", "any").c_str());

    slot.itemsPerRotation = static_cast<uint8_t>(std::max(1, std::min(10, config->getInt("items_per_rotation", 3))));
    slot.cardDurationS = static_cast<uint8_t>(std::max(3, std::min(30, config->getInt("card_duration_s", 7))));
    slot.snapshotDurationS = static_cast<uint8_t>(std::max(2, std::min(20, config->getInt("snapshot_duration_s", 4))));
    slot.videoMaxDurationS = static_cast<uint8_t>(std::max(2, std::min(20, config->getInt("video_max_duration_s", 6))));
    slot.videoFps = static_cast<uint8_t>(std::max(8, std::min(30, config->getInt("video_fps", 15))));

    slot.showPoster = config->getBool("show_poster", true);
    slot.showSynopsis = config->getBool("show_synopsis", true);
    slot.showSnapshot = config->getBool("show_snapshot", true);
    slot.showVideo = config->getBool("show_video", true);
    slot.colorBoost = config->getBool("color_boost", true);

    uint32_t perItemMs = static_cast<uint32_t>(slot.cardDurationS + slot.snapshotDurationS + slot.videoMaxDurationS) * 1000U;
    m_fallbackCycleMs = std::max<uint32_t>(12000U, perItemMs * slot.itemsPerRotation);

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
    postBody.reserve(320);
    postBody += "{\"session_id\":\"";
    postBody += m_sessionId;
    postBody += "\",\"count\":";
    postBody += String(cfg.itemsPerRotation);
    postBody += ",\"width\":";
    postBody += String(m_width);
    postBody += ",\"height\":";
    postBody += String(m_height);
    postBody += ",\"fps\":";
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
            // Showcase server offline -> display localized demo card for 1 cycle
            return;
        }
        batchDoc.clear();
        if (deserializeJson(batchDoc, batchResp.stream()) != DeserializationError::Ok) {
            return;
        }
        const char* status = batchDoc["status"] | "ready";
        if (strcmp(status, "warming") == 0) {
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                // Multiple engines in rotation: skip turn immediately without freezing
                m_skipTurnImmediately.store(true, std::memory_order_release);
                return;
            }
            // Single engine in rotation: show warming fallback card while polling
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
        int yr = item["year"] | 0;
        if (yr > 0) snprintf(nextCard.year, sizeof(nextCard.year), "%d", yr);
        const char* badge = item["system_badge"] | "";
        if (badge[0] == '\0' && item["genres"].is<JsonArray>() && item["genres"].size() > 0) {
            badge = item["genres"][0] | "";
        }
        if (badge[0] == '\0') badge = domain;
        copySafeStr(nextCard.badge, sizeof(nextCard.badge), badge);

        float ratingVal = item["rating"] | 0.0f;
        if (ratingVal > 0.0f) {
            snprintf(nextCard.rating, sizeof(nextCard.rating), "%.1f", ratingVal);
        }
        copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), item["synopsis"] | "");
        nextCard.badgeColor565 = parseHexColor565(item["accent_color"] | nullptr, defaultAccent565());
        nextCard.hasPoster = false;
        nextCard.hasVideoFrame = false;

        // Optional Poster RGB565 download (`32x32x2 = 2048` bytes)
        const char* posterPath = item["poster_rgb565_url"] | "";
        if (cfg.showPoster && m_poster565 && posterPath[0] != '\0') {
            String posterUrl = (strncmp(posterPath, "http", 4) == 0)
                ? String(posterPath)
                : (String(cfg.serverUrl) + posterPath);
            auto pResp = net::SecureHttpClient::get(posterUrl, httpOpts);
            if (pResp.ok()) {
                if (readExactFromStream(pResp.stream(), reinterpret_cast<uint8_t*>(m_poster565), 32 * 32 * 2, m_wantActive)) {
                    nextCard.hasPoster = true;
                }
            }
        }

        // Publish CardSnapshot via Seqlock
        m_seq.fetch_add(1, std::memory_order_acq_rel);
        m_liveCard = nextCard;
        m_seq.fetch_add(1, std::memory_order_release);
        m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

        // Phase 1: Rich Card dwell
        uint32_t cardWaitMs = static_cast<uint32_t>(cfg.cardDurationS) * 1000U;
        uint32_t t0 = millis();
        while (m_wantActive.load(std::memory_order_acquire) && (millis() - t0) < cardWaitMs) {
#if defined(ESP32)
            vTaskDelay(pdMS_TO_TICKS(40));
#else
            break;
#endif
        }

        // Phase 2: Snapshot Pan-Scan
        const char* snapPath = item["snapshot_rgb565_url"] | "";
        if (m_wantActive.load(std::memory_order_acquire) && cfg.showSnapshot && m_frame565 && snapPath[0] != '\0') {
            String snapUrl = (strncmp(snapPath, "http", 4) == 0)
                ? String(snapPath)
                : (String(cfg.serverUrl) + snapPath);
            auto sResp = net::SecureHttpClient::get(snapUrl, httpOpts);
            if (sResp.ok()) {
                size_t frameBytes = m_framePixels * sizeof(uint16_t);
                if (readExactFromStream(sResp.stream(), reinterpret_cast<uint8_t*>(m_frame565), frameBytes, m_wantActive)) {
                    m_seq.fetch_add(1, std::memory_order_acq_rel);
                    m_liveCard.hasVideoFrame = true;
                    m_seq.fetch_add(1, std::memory_order_release);
                    m_phase.store(static_cast<uint8_t>(Phase::SnapshotPan), std::memory_order_release);

                    uint32_t snapWaitMs = static_cast<uint32_t>(cfg.snapshotDurationS) * 1000U;
                    uint32_t ts = millis();
                    while (m_wantActive.load(std::memory_order_acquire) && (millis() - ts) < snapWaitMs) {
#if defined(ESP32)
                        vTaskDelay(pdMS_TO_TICKS(40));
#else
                        break;
#endif
                    }
                }
            }
        }

        // Phase 3: Video Stream (`.rgb565.stream` `DMDS`)
        const char* vidPath = item["video_stream_url"] | "";
        if (m_wantActive.load(std::memory_order_acquire) && cfg.showVideo && m_hasPsram && m_frame565 && vidPath[0] != '\0') {
            String vidUrl = (strncmp(vidPath, "http", 4) == 0)
                ? String(vidPath)
                : (String(cfg.serverUrl) + vidPath);
            if (vidUrl.indexOf("max_duration_s=") < 0) {
                vidUrl += (vidUrl.indexOf('?') >= 0 ? "&" : "?");
                vidUrl += "max_duration_s=" + String(cfg.videoMaxDurationS);
            }
            auto vResp = net::SecureHttpClient::get(vidUrl, httpOpts);
            if (vResp.ok()) {
                Stream& st = vResp.stream();
                uint8_t hdr[16];
                if (readExactFromStream(st, hdr, sizeof(hdr), m_wantActive)) {
                    if (memcmp(hdr, "DMDS", 4) == 0) {
                        uint16_t fpsX100 = static_cast<uint16_t>(hdr[10] | (hdr[11] << 8));
                        uint16_t frameCount = static_cast<uint16_t>(hdr[12] | (hdr[13] << 8));
                        uint16_t frameBytes = static_cast<uint16_t>(hdr[14] | (hdr[15] << 8));
                        uint32_t frameDelayMs = (fpsX100 > 0) ? std::max<uint32_t>(15U, 100000U / fpsX100) : 66U;
                        size_t maxBufBytes = m_framePixels * sizeof(uint16_t);

                        if (frameBytes > 0 && frameBytes <= maxBufBytes) {
                            m_phase.store(static_cast<uint8_t>(Phase::VideoStream), std::memory_order_release);
                            uint32_t maxVideoMs = static_cast<uint32_t>(cfg.videoMaxDurationS) * 1000U;
                            uint32_t tv = millis();
                            for (uint16_t f = 0; f < frameCount; ++f) {
                                if (!m_wantActive.load(std::memory_order_acquire)) break;
                                if ((millis() - tv) >= maxVideoMs) break;
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
                        }
                    }
                }
            }
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
    Lang sysLang = I18n::parseLang(snap.system.lang.c_str());

    // Lock-free seqlock check for updated CardSnapshot from Core 0 worker
    uint32_t s1 = m_seq.load(std::memory_order_acquire);
    if ((s1 & 1U) == 0U) {
        CardSnapshot temp = m_liveCard;
        uint32_t s2 = m_seq.load(std::memory_order_acquire);
        if (s1 == s2 && temp.title[0] != '\0') {
            m_renderCard = temp;
            m_dirty = true;
        }
    }

    if (m_renderCard.title[0] == '\0') {
        loadOfflineDemoCard(m_renderCard, static_cast<uint8_t>(sysLang));
        m_dirty = true;
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

void MediaShowcaseBase::drawCardPhase(IDrawingSurface* s, const CardSnapshot& card, uint32_t elapsedMs) {
    int mW = s->width();
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];

    // 1. Content text area rendered first so left poster & borders cleanly clip scrolling text
    int textX = cfg.showPoster ? 34 : 3;

    // Middle Line: Title
    drawScrollingLine(s, card.title, textX, mW - 2, 11, 0xFFFF, elapsedMs);

    // Bottom Line: Synopsis or Warming status
    if (m_warmingFallback.load(std::memory_order_relaxed)) {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        Lang sysLang = I18n::parseLang(guard.get().system.lang.c_str());
        drawScrollingLine(s, I18n::getShowcaseWarmingLabel(sysLang), textX, mW - 2, 22, 0xFFE0, elapsedMs);
    } else if (cfg.showSynopsis && card.synopsis[0] != '\0') {
        drawScrollingLine(s, card.synopsis, textX, mW - 2, 22, 0xAD75, elapsedMs);
    }

    // 2. Occlude left poster region so scrolling text never bleeds into the 32x32 artwork
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

    // 3. Top Header Bar (Badge + Year + Rating)
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
        ConfigField("items_per_rotation", ConfigType::INTEGER, "Items per Rotation", "Number of items displayed before rotating to the next screen", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("card_duration_s", ConfigType::INTEGER, "Card Duration (s)", "Seconds to display the rich poster and metadata card", "7", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("snapshot_duration_s", ConfigType::INTEGER, "Snapshot Duration (s)", "Seconds to display the pan-scan snapshot scene", "4", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "6", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "auto_action", false, "", "", "", "auto_action,center_crop,letterbox,pan_scan", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the 32x32 cover or cabinet artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_snapshot", ConfigType::BOOLEAN, "Show Snapshot", "Display full-panel scene snapshot after the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video trailer or gameplay clip (PSRAM)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("systems", ConfigType::STRING, "Arcade & Console Systems", "Comma-separated systems (mame,neogeo,snes,megadrive,psx,nes,gba)", "mame,neogeo,snes,megadrive,psx", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("genres", ConfigType::STRING, "Game Genres", "Optional genre filter (action,rpg,fighting,shmup,platformer)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::STRING, "Decades", "Optional decade filter (1980s,1990s,2000s)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept)
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
        ConfigField("items_per_rotation", ConfigType::INTEGER, "Items per Rotation", "Number of items displayed before rotating to the next screen", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("card_duration_s", ConfigType::INTEGER, "Card Duration (s)", "Seconds to display the rich poster and metadata card", "7", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("snapshot_duration_s", ConfigType::INTEGER, "Snapshot Duration (s)", "Seconds to display the pan-scan snapshot scene", "4", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "6", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "auto_action", false, "", "", "", "auto_action,center_crop,letterbox,pan_scan", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the 32x32 cover or cabinet artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_snapshot", ConfigType::BOOLEAN, "Show Snapshot", "Display full-panel scene snapshot after the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video trailer or gameplay clip (PSRAM)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::STRING, "Movie Genres", "Optional genre filter (sci-fi,action,thriller,animation)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::STRING, "Decades", "Optional decade filter (1980s,1990s,2000s,2010s,2020s)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
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
        ConfigField("items_per_rotation", ConfigType::INTEGER, "Items per Rotation", "Number of items displayed before rotating to the next screen", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("card_duration_s", ConfigType::INTEGER, "Card Duration (s)", "Seconds to display the rich poster and metadata card", "7", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("snapshot_duration_s", ConfigType::INTEGER, "Snapshot Duration (s)", "Seconds to display the pan-scan snapshot scene", "4", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "6", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "auto_action", false, "", "", "", "auto_action,center_crop,letterbox,pan_scan", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the 32x32 cover or cabinet artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_snapshot", ConfigType::BOOLEAN, "Show Snapshot", "Display full-panel scene snapshot after the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video trailer or gameplay clip (PSRAM)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::STRING, "Series Genres", "Optional genre filter (drama,sci-fi,crime,fantasy)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::STRING, "Decades", "Optional decade filter (1990s,2000s,2010s,2020s)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
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
        ConfigField("items_per_rotation", ConfigType::INTEGER, "Items per Rotation", "Number of items displayed before rotating to the next screen", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("card_duration_s", ConfigType::INTEGER, "Card Duration (s)", "Seconds to display the rich poster and metadata card", "7", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("snapshot_duration_s", ConfigType::INTEGER, "Snapshot Duration (s)", "Seconds to display the pan-scan snapshot scene", "4", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "6", false, "2", "20", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "15", false, "8", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Crop Mode", "Smart framing mode powered by dmd_gif_converter", "auto_action", false, "", "", "", "auto_action,center_crop,letterbox,pan_scan", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the 32x32 cover or cabinet artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_snapshot", ConfigType::BOOLEAN, "Show Snapshot", "Display full-panel scene snapshot after the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video trailer or gameplay clip (PSRAM)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::BOOLEAN, "LED Color Boost", "Boost contrast and saturation for HUB75 LED panels", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::STRING, "Anime Genres", "Optional genre filter (mecha,shonen,cyberpunk,seinen)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::STRING, "Decades", "Optional decade filter (1980s,1990s,2000s,2010s,2020s)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,ending,scene,any)", "opening", false, "", "", "", "any,opening,ending,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new AnimeShowcaseEngine()); };
    return desc;
}
