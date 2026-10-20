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
 * @brief Shared base implementation for the 4 DMD Media Showcase engines on ESP32 / ESP32-S3:
 *        RetroGamesEngine, MovieShowcaseEngine, SeriesShowcaseEngine, and AnimeShowcaseEngine.
 *
 * Strictly enforces all 20 real-time architectural invariants:
 * - Zero dynamic allocation and zero mutex on the Core 1 hot path (update / render).
 * - Core 0 worker task performs the 2-step LAN HTTP protocol (`POST /session` + `GET /batch`)
 *   and streams binary `.rgb565` / `.rgb565.stream` (`DMDS`) frames into pre-allocated buffers.
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
    bool isFinished() const override;

protected:
    static constexpr size_t MAX_URL_LEN = 96;
    static constexpr size_t MAX_FILTER_LEN = 64;
    static constexpr size_t MAX_TITLE_LEN = 64;
    static constexpr size_t MAX_BADGE_LEN = 12;
    static constexpr size_t MAX_META_LEN = 16;
    static constexpr size_t MAX_SYNOPSIS_LEN = 192;
    static constexpr size_t MAX_SESSION_ID_LEN = 40;

    enum class Phase : uint8_t {
        Card = 0,
        SnapshotPan = 1,
        VideoStream = 2
    };

    struct ConfigSlot {
        char serverUrl[MAX_URL_LEN] = "http://127.0.0.1:8080";
        char lang[8] = "system";
        char videoMode[16] = "auto_action";
        char systems[MAX_FILTER_LEN] = "mame,neogeo,snes,megadrive,psx";
        char genres[MAX_FILTER_LEN] = "";
        char decades[MAX_FILTER_LEN] = "";
        char videoType[16] = "any";
        uint8_t itemsPerRotation = 3;
        uint8_t cardDurationS = 7;
        uint8_t snapshotDurationS = 4;
        uint8_t videoMaxDurationS = 6;
        uint8_t videoFps = 15;
        bool showPoster = true;
        bool showSynopsis = true;
        bool showSnapshot = true;
        bool showVideo = true;
        bool colorBoost = true;
    };

    struct CardSnapshot {
        char title[MAX_TITLE_LEN] = "";
        char year[8] = "";
        char badge[MAX_BADGE_LEN] = "";
        char rating[8] = "";
        char synopsis[MAX_SYNOPSIS_LEN] = "";
        uint16_t badgeColor565 = 0xFD20;
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
    uint16_t* m_poster565 = nullptr; // 32x32 RGB565 (2048 bytes)
    uint16_t* m_frame565 = nullptr;  // WxH RGB565 (8192 bytes for 128x32)
    size_t m_framePixels = 0;

    std::atomic<bool> m_wantActive{false};
    std::atomic<bool> m_taskAlive{false};
    std::atomic<bool> m_skipTurnImmediately{false};
    std::atomic<bool> m_warmingFallback{false};
    std::atomic<bool> m_turnDone{false};
    std::atomic<uint8_t> m_phase{static_cast<uint8_t>(Phase::Card)};
    std::atomic<uint8_t> m_rotationSlotCount{1};

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
    uint16_t defaultAccent565() const;
    void loadOfflineDemoCard(CardSnapshot& out, uint8_t langEnumVal) const;
    bool allocateBuffers();
    void freeBuffers();
    void startWorkerTask();
    void stopWorkerTaskBounded(uint32_t timeoutMs);
    static void workerTaskEntry(void* arg);
    void runWorkerLoop();
    void drawCardPhase(IDrawingSurface* s, const CardSnapshot& card, uint32_t elapsedMs);
    void drawScrollingLine(IDrawingSurface* s, const char* text, int minX, int maxX, int y, uint16_t color, uint32_t elapsedMs);
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

