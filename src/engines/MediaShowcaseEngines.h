#pragma once
#include <Arduino.h>
#include <atomic>
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "../../include/core/EngineContract.h"

class IDrawingSurface;

enum class ShowcaseDomainId : uint8_t {
    Games = 0,
    Movies = 1,
    Series = 2,
    Anime = 3
};

/**
 * @struct HttpChunkedDecoder
 * @brief Zero-heap streaming RFC 9112 HTTP/1.1 `Transfer-Encoding: chunked` decoder.
 *        Strips `<hex>[;ext]\r\n` headers and `\r\n` trailers in bulk `memcpy` slices
 *        when a server or reverse proxy omits `Content-Length`.
 */
struct HttpChunkedDecoder {
    enum class State : uint8_t {
        SizeHex = 0,
        SizeExt = 1,
        SizeLf = 2,
        Payload = 3,
        PayloadCr = 4,
        PayloadLf = 5,
        Done = 6,
        Error = 7
    };

    State state = State::SizeHex;
    size_t chunkRem = 0;
    uint8_t hexDigits = 0;

    void reset() {
        state = State::SizeHex;
        chunkRem = 0;
        hexDigits = 0;
    }

    bool isDone() const { return state == State::Done; }
    bool hasError() const { return state == State::Error; }

    size_t decode(
        const uint8_t* src,
        size_t srcLen,
        size_t& outSrcConsumed,
        uint8_t* dst,
        size_t dstCap
    );
};

/**
 * @class MediaShowcaseBase
 * @brief Shared V3.0 base implementation for the 4 DMD Media Showcase engines on ESP32 / ESP32-S3:
 *        RetroGamesEngine, MovieShowcaseEngine, SeriesShowcaseEngine, and AnimeShowcaseEngine.
 *
 * Strictly enforces all 20 real-time architectural invariants + V3.0 requirements:
 * - Zero dynamic allocation and zero mutex on the Core 1 hot path (update / render).
 * - Strict Consume-Once & Zero Media Cache on the DMD: never caches or replays items when
 *   the server goes offline (MQTT LWT `online:false`, UDP watchdog timeout, or HTTP error).
 * - Dedicated MQTT Broker Config (`use_showcase_broker` auto-deriving `<server_ip>:1883`,
 *   or `mqtt_broker_source` `global` / `custom`).
 * - Dual-transport Core 0 worker (`mqtt_tcp` real-time `DMDS` TCP streaming + `http_poll` fallback).
 * - 4 Creative Procedural Pixel-Art "Waiting for Content" Animations on Core 1.
 * - Quiescent, allocation-free deactivation (`deactivate()` / `shutdownForDestruction()`)
 *   reclaiming all transient buffers before the next engine initializes.
 */
class MediaShowcaseBase : public IEngine {
public:
    explicit MediaShowcaseBase(ShowcaseDomainId domain);
    ~MediaShowcaseBase() override;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void resume() override {
        m_lastRenderedCardSeq = 0xFFFFFFFFU;
        m_lastRenderedFrameSeq = 0xFFFFFFFFU;
        m_lastRenderedPhase = 0xFFU;
        m_dirty = true;
        m_renderedNewFrame = false;
    }
    bool shutdownForDestruction() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override {
        uint16_t newW = geometry.width > 0 ? geometry.width : 128;
        uint16_t newH = geometry.height > 0 ? geometry.height : 32;
        if (newW != m_width || newH != m_height) {
            m_width = newW;
            m_height = newH;
            m_lastPublishedCfgGen = 0;
            m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
            clearLiveCardState();
            m_cfgGen.fetch_add(1, std::memory_order_acq_rel);
        }
        m_dirty = true;
    }

    uint16_t currentWidth() const { return m_width; }
    uint16_t currentHeight() const { return m_height; }
    uint32_t configGeneration() const { return m_cfgGen.load(std::memory_order_acquire); }
    const char* sessionId() const { return m_sessionId; }

    bool isRealtime() const override { return true; }
    bool needsClear() const override { return false; }
    bool hasNewFrame() const override { return m_renderedNewFrame; }
    bool selfPaced() const override { return true; }
    void setRotationBudget(uint32_t budget) override {
        uint32_t clamped = budget > 0 ? budget : 1;
        m_rotationBudget.store(clamped, std::memory_order_relaxed);
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1 &&
            m_itemsPlayedInTurn.load(std::memory_order_acquire) >= clamped) {
            m_turnDone.store(true, std::memory_order_release);
        }
    }
    uint32_t computeTurnSafetyTimeoutMs() const;
    bool isFinished() const override;

    /**
     * @brief Zero-copy in-place parser for an MQTT `ready` JSON payload (`dmd_showcase/clients/{id}/{domain}/ready`).
     *        Exposed for deterministic native/embedded unit testing of large base64 poster payloads
     *        and duplicate/aliased stream metadata keys (`stream_fps`/`fps`, `stream_duration_s`/`total_stream_duration_s`).
     * @param jsonBuf Mutable null-terminated JSON buffer (modified in-place by ArduinoJson zero-copy mode).
     * @param showPoster Whether inline `poster_rgb565_b64` should be decoded into `m_poster565`.
     * @param outItemId Receives the parsed `item_id` (or `card.id`).
     * @param outHasStream Receives true if `has_panoramas` or `has_video` is set.
     * @param outStreamMaxMs Receives the computed stream watchdog timeout in milliseconds.
     * @param outExplicitWarming Receives true if the payload is a valid `{"ready": false}` message.
     * @return true if a valid `{"ready": true, "card": {...}}` item was parsed into `m_liveCard`.
     */
    bool parseReadyPayloadInPlaceBuf(
        char* jsonBuf,
        bool showPoster,
        char* outItemId,
        size_t itemIdCap,
        bool& outHasStream,
        uint32_t& outStreamMaxMs,
        bool& outExplicitWarming
    );

    bool parseReadyPayloadInPlace(
        char* jsonBuf,
        bool showPoster,
        String& outItemId,
        bool& outHasStream,
        uint32_t& outStreamMaxMs,
        bool& outExplicitWarming
    );

    /**
     * @brief Counts rendered UTF-8 glyphs (folding NFD combining accents and expanding `œ`/`Œ` ligatures to 2 columns).
     */
    static size_t countUtf8Glyphs(const char* utf8);

    /**
     * @brief Copies a comma-separated filter token list into a bounded buffer without ever
     *        truncating mid-token. If `src` exceeds `cap - 1`, truncates cleanly at the last
     *        complete comma boundary so partial system/genre IDs are never sent to Showcase.
     */
    static void copySafeCsvTokens(char* dst, size_t cap, const char* src);

    /**
     * @brief Caps advertised `video_fps` to 25 FPS on non-PSRAM boards (`ESP32_STD`) to keep
     *        Wi-Fi/LwIP UDP chunk rate <= 150 pkt/s (6x 1408-byte chunks/frame), while preserving
     *        full 25..60 FPS on PSRAM (`ESP32-S3`).
     */
    static uint8_t resolveEffectiveVideoFps(uint8_t requestedFps, bool hasPsram) {
        uint8_t clamped = requestedFps < 8U ? 8U : (requestedFps > 60U ? 60U : requestedFps);
        if (!hasPsram && clamped > 25U) {
            return 25U;
        }
        return clamped;
    }

    /**
     * @brief Requires 3 consecutive transport failures before transitioning to `NetworkOffline` (`2`),
     *        preventing transient LAN/Wi-Fi retries from flashing a false offline banner on the DMD.
     */
    static bool shouldReportNetworkOffline(uint8_t consecutiveFailures) {
        return consecutiveFailures >= 3U;
    }

    /**
     * @brief Case-insensitive check for `Transfer-Encoding: ...chunked...` in raw HTTP headers.
     */
    static bool hasChunkedTransferEncoding(const char* rawHeaders);

    static constexpr size_t MAX_SYSTEMS_FILTER_LEN = 560;
    static constexpr size_t MAX_FILTER_LEN = 192;

protected:
    static constexpr size_t MAX_URL_LEN = 96;
    static constexpr size_t MAX_TITLE_LEN = 80;
    static constexpr size_t MAX_BADGE_LEN = 12;
    static constexpr size_t MAX_META_LEN = 16;
    static constexpr size_t MAX_SYNOPSIS_LEN = 256;
    static constexpr size_t MAX_SESSION_ID_LEN = 48;
    static constexpr size_t MAX_ITEM_ID_LEN = 128;
    static constexpr size_t IO_SCRATCH_BYTES = 1472;
    static constexpr size_t PSRAM_MQTT_RX_BYTES = 32768;

    enum class Phase : uint8_t {
        Card = 0,
        LiveStream = 1
    };

    struct ConfigSlot {
        char serverUrl[MAX_URL_LEN] = "http://dmdmediashowcase.local:8890";
        char transportMode[16] = "mqtt_tcp";
        char mqttBrokerSource[12] = "global";
        char mqttCustomUrl[MAX_URL_LEN] = "";
        char lang[8] = "system";
        char textScrollSpeed[12] = "4";
        char videoMode[16] = "auto_action";
        char systems[MAX_SYSTEMS_FILTER_LEN] = "";
        char genres[MAX_FILTER_LEN] = "";
        char decades[MAX_FILTER_LEN] = "";
        char videoType[16] = "any";
        uint8_t snapshotPanoramaCount = 0;
        uint8_t videoMaxDurationS = 8;
        uint8_t videoFps = 20;
        bool enabled = true;
        bool useShowcaseBroker = true;
        bool showPoster = true;
        bool showSynopsis = true;
        bool jacketPanorama = true;
        bool marqueePanorama = false;
        bool showVideo = true;
        char colorBoost[16] = "auto";
    };

    struct CardSnapshot {
        char title[MAX_TITLE_LEN] = "";
        char year[8] = "";
        char badge[MAX_BADGE_LEN] = "";
        char rating[8] = "";
        char synopsis[MAX_SYNOPSIS_LEN] = "";
        uint16_t badgeColor565 = 0xFD20;
        uint16_t posterW = 32;
        uint16_t posterH = 32;
        bool hasData = false;
        bool hasPoster = false;
        bool hasVideoFrame = false;
    };

    ShowcaseDomainId m_domain;
    IDrawingSurface* m_surface = nullptr;
    uint16_t m_width = 128;
    uint16_t m_height = 32;
    bool m_hasPsram = false;

    // Double-buffered configuration (written on cold path, read by Core 0 & Core 1)
    ConfigSlot m_cfg[2];
    std::atomic<uint8_t> m_cfgIdx{0};
    std::atomic<uint32_t> m_cfgGen{1};

    // Session & Handoff state between Core 0 worker and Core 1 render loop
    char m_sessionId[MAX_SESSION_ID_LEN] = "";
    std::atomic<uint32_t> m_seq{0};              // Seqlock for CardSnapshot metadata
    std::atomic<uint32_t> m_frameSeq{0};         // Tear-free frame seq: ODD while Core 0 writes m_frame565, EVEN when complete
    std::atomic<uint32_t> m_consumedFrameSeq{0}; // Last EVEN frame seq blitted by Core 1 (prevents frame overwrite before blit)
    std::atomic<uint32_t> m_presentedStreamFrames{0};
    CardSnapshot m_liveCard;
    uint16_t* m_poster565 = nullptr; // 32x32 (2048 B) on 128x32 or up to 48x64 (6144 B) on larger panels
    uint16_t* m_frame565 = nullptr;  // WxH RGB565 (8192 bytes for 128x32)
    char* m_ioScratch = nullptr;     // Preallocated 1472 B SRAM scratch for MQTT TX/RX & HTTP stream headers
    char* m_psramMqttRx = nullptr;   // Preallocated 32 KB SPIRAM buffer on ESP32-S3 for XL Base64 posters
    size_t m_posterCapBytes = 0;
    size_t m_framePixels = 0;
    uint32_t m_mqttConnSeq = 0;
    uint32_t m_lastPublishedCfgGen = 0;
    uint8_t m_consecutiveNetFailures = 0;

    std::atomic<bool> m_wantActive{false};
    std::atomic<bool> m_taskAlive{false};
    std::atomic<bool> m_skipTurnImmediately{false};
    std::atomic<bool> m_warmingFallback{false};
    std::atomic<bool> m_turnDone{false};
    std::atomic<uint8_t> m_phase{static_cast<uint8_t>(Phase::Card)};
    std::atomic<uint8_t> m_statusState{3}; // 0=LiveReady, 1=ServerWarming, 2=NetworkOffline, 3=Unconfigured
    std::atomic<uint8_t> m_rotationSlotCount{1};
    std::atomic<uint8_t> m_consecutiveWarmSkips{0};
    std::atomic<uint32_t> m_rotationBudget{1};
    std::atomic<uint32_t> m_itemsPlayedInTurn{0};

#if defined(ESP32)
    TaskHandle_t m_task = nullptr;
#endif

    // Core 1 local render state (zero heap allocation)
    CardSnapshot m_renderCard;
    uint32_t m_activatedAtMs = 0;
    uint32_t m_phaseStartedAtMs = 0;
    uint32_t m_lastScrollStepMs = 0;
    uint32_t m_fallbackCycleMs = 24000;
    uint32_t m_lastRenderedCardSeq = 0xFFFFFFFFU;
    uint32_t m_lastRenderedFrameSeq = 0xFFFFFFFFU;
    uint8_t m_lastRenderedPhase = 0xFFU;
    bool m_dirty = true;
    bool m_renderedNewFrame = false;

    const char* domainSlug() const;
    const char* domainBadgeLabel() const;
    uint16_t defaultAccent565() const;
    static uint32_t synopsisPxPerSec(const char* speedStr);
    static uint32_t configuredMaxStreamMs(const ConfigSlot& cfg);
    static void computeCardPosterDims(uint16_t panelW, uint16_t panelH, uint16_t& outW, uint16_t& outH);
    static bool resolveMqttBroker(const ConfigSlot& cfg, char* outHost, size_t hostCap, uint16_t& outPort);
    bool allocateBuffers();
    void freeBuffers();
    void clearLiveCardState();
    void finishTurnItem();
    void startWorkerTask();
    void stopWorkerTaskBounded(uint32_t timeoutMs);
    static void workerTaskEntry(void* arg);
    void runWorkerLoop();
    bool runMqttUdpTurn(
        const ConfigSlot& cfg,
        const char* domain,
        const char* reqLang,
        char* outHttpFallbackItemId,
        size_t fallbackItemCap
    );
    void runHttpPollTurn(const ConfigSlot& cfg, const char* domain, const char* reqLang);
    bool streamUnifiedRgb565Http(const char* serverUrl, const char* streamPath);
    void drawWaitingPixelArt(IDrawingSurface* s, int mW, int mH, uint32_t elapsedMs);
    void drawCardPhase(IDrawingSurface* s, const CardSnapshot& card, uint32_t elapsedMs);
    void drawScrollingLine(IDrawingSurface* s, const char* text, int minX, int maxX, int y, uint16_t color, uint32_t elapsedMs);
    void drawSinglePassSynopsis(IDrawingSurface* s, const char* text, int minX, int maxX, int y, uint16_t color, uint32_t elapsedMs, uint32_t speedPxPerSec);
};

class RetroGamesEngine : public MediaShowcaseBase {
public:
    RetroGamesEngine() : MediaShowcaseBase(ShowcaseDomainId::Games) {}
};

class MovieShowcaseEngine : public MediaShowcaseBase {
public:
    MovieShowcaseEngine() : MediaShowcaseBase(ShowcaseDomainId::Movies) {}
};

class SeriesShowcaseEngine : public MediaShowcaseBase {
public:
    SeriesShowcaseEngine() : MediaShowcaseBase(ShowcaseDomainId::Series) {}
};

class AnimeShowcaseEngine : public MediaShowcaseBase {
public:
    AnimeShowcaseEngine() : MediaShowcaseBase(ShowcaseDomainId::Anime) {}
};

class RetroGamesEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};

class MovieShowcaseEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};

class SeriesShowcaseEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};

class AnimeShowcaseEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
