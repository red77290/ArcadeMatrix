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
 * - Dual-transport Core 0 worker (`mqtt_udp` real-time `DMDF` UDP streaming + `http_poll` fallback).
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
    void resume() override { m_dirty = true; }
    bool shutdownForDestruction() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override {
        m_width = geometry.width > 0 ? geometry.width : 128;
        m_height = geometry.height > 0 ? geometry.height : 32;
        m_dirty = true;
    }

    bool isRealtime() const override { return true; }
    bool needsClear() const override { return false; }
    bool hasNewFrame() const override { return m_dirty; }
    bool selfPaced() const override { return true; }
    void setRotationBudget(uint32_t budget) override { m_rotationBudget.store(budget > 0 ? budget : 1, std::memory_order_relaxed); }
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
    bool parseReadyPayloadInPlace(
        char* jsonBuf,
        bool showPoster,
        String& outItemId,
        bool& outHasStream,
        uint32_t& outStreamMaxMs,
        bool& outExplicitWarming
    );

protected:
    static constexpr size_t MAX_URL_LEN = 96;
    static constexpr size_t MAX_FILTER_LEN = 128;
    static constexpr size_t MAX_TITLE_LEN = 64;
    static constexpr size_t MAX_BADGE_LEN = 12;
    static constexpr size_t MAX_META_LEN = 16;
    static constexpr size_t MAX_SYNOPSIS_LEN = 192;
    static constexpr size_t MAX_SESSION_ID_LEN = 40;

    enum class Phase : uint8_t {
        Card = 0,
        LiveStream = 1
    };

    struct ConfigSlot {
        char serverUrl[MAX_URL_LEN] = "http://dmdmediashowcase.local:8890";
        char transportMode[16] = "mqtt_udp";
        char mqttBrokerSource[12] = "global";
        char mqttCustomUrl[MAX_URL_LEN] = "";
        char lang[8] = "system";
        char textScrollSpeed[12] = "4";
        char videoMode[16] = "auto_action";
        char systems[MAX_FILTER_LEN] = "";
        char genres[MAX_FILTER_LEN] = "";
        char decades[MAX_FILTER_LEN] = "";
        char videoType[16] = "any";
        uint8_t snapshotPanoramaCount = 0;
        uint8_t videoMaxDurationS = 8;
        uint8_t videoFps = 15;
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
    std::atomic<uint32_t> m_seq{0}; // Seqlock for CardSnapshot + pixel buffers
    CardSnapshot m_liveCard;
    uint16_t* m_poster565 = nullptr; // Up to 48x64 RGB565 (6144 bytes)
    uint16_t* m_frame565 = nullptr;  // WxH RGB565 (8192 bytes for 128x32)
    size_t m_framePixels = 0;

    std::atomic<bool> m_wantActive{false};
    std::atomic<bool> m_taskAlive{false};
    std::atomic<bool> m_skipTurnImmediately{false};
    std::atomic<bool> m_warmingFallback{false};
    std::atomic<bool> m_turnDone{false};
    std::atomic<uint8_t> m_phase{static_cast<uint8_t>(Phase::Card)};
    std::atomic<uint8_t> m_statusState{3}; // 0=LiveReady, 1=ServerWarming, 2=NetworkOffline, 3=Unconfigured
    std::atomic<uint8_t> m_rotationSlotCount{1};
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
    bool m_dirty = true;

    const char* domainSlug() const;
    const char* domainBadgeLabel() const;
    uint16_t defaultAccent565() const;
    static uint32_t synopsisPxPerSec(const char* speedStr);
    static void computeCardPosterDims(uint16_t panelW, uint16_t panelH, uint16_t& outW, uint16_t& outH);
    static bool resolveMqttBroker(const ConfigSlot& cfg, char* outHost, size_t hostCap, uint16_t& outPort);
    bool allocateBuffers();
    void freeBuffers();
    void clearLiveCardState();
    void startWorkerTask();
    void stopWorkerTaskBounded(uint32_t timeoutMs);
    static void workerTaskEntry(void* arg);
    void runWorkerLoop();
    bool runMqttUdpTurn(const ConfigSlot& cfg, const char* domain, const char* reqLang);
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
