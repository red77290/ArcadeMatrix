#pragma once
#include <Arduino.h>
#include <atomic>
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "../../include/core/EngineContract.h"
#include "../core/net/DmdzStreamClient.h"
#include "showcase/ShowcaseCardRenderer.h"
#include "showcase/ShowcaseStreamPlayer.h"

class IDrawingSurface;

using ShowcaseDomainId = engines::showcase::ShowcaseDomainId;
using HttpChunkedDecoder = net::HttpChunkedDecoder;

/**
 * @class MediaShowcaseBase
 * @brief Shared V3.1 orchestrator for the 4 DMD Media Showcase engines on ESP32 / ESP32-S3:
 *        RetroGamesEngine, MovieShowcaseEngine, SeriesShowcaseEngine, and AnimeShowcaseEngine.
 *
 * Architecture highlights:
 * - Compact `<= 1,600 B` C++ instance footprint (`~1.2 KB` on ESP32) with task-first activation.
 * - 1:1 RPi turn-readiness gate (`m_turnEvaluated`, `m_readyStateReceived`, `m_mqttConnectedAtMs`),
 *   streaming filtered MQTT/HTTP JSON parsing (`0` drop `> 8 KB`), binary `.rgb565` poster fetch,
 *   and automatic same-turn `MQTT -> HTTP` fallback.
 * - Core 1 Zero-Copy `ScopedCanvasGuard` in-place `DMDZ` v2 (`16-bit` True-RGB565 lossless
 *   `RLE` / `LZ` / `Scroll2D`) decoding with dynamic `allowsOverlay() == false` during
 *   `StreamBlank` and `LiveStream`.
 * - Shared `ShowcaseStreamPlayer` delivery (`esp32dev` `25 MHz` SD cache `/cache/showcase_live.dmdz`
 *   with `0` Wi-Fi traffic during playback; `ESP32-S3` PSRAM preload `<= 4 MB` + `1 MB` PSRAM SPSC).
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
    void pause() override;
    void resume() override;
    bool shutdownForDestruction() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

    uint16_t currentWidth() const { return m_width; }
    uint16_t currentHeight() const { return m_height; }
    uint32_t configGeneration() const { return m_cfgGen.load(std::memory_order_acquire); }
    const char* sessionId() const { return m_sessionId; }

    bool isRealtime() const override { return true; }
    bool needsClear() const override { return false; }
    bool hasNewFrame() const override { return m_renderedNewFrame; }
    bool selfPaced() const override { return true; }

    bool allowsOverlay() const override {
        const uint8_t curPhase = m_phase.load(std::memory_order_acquire);
        return curPhase != static_cast<uint8_t>(Phase::StreamBlank) &&
               curPhase != static_cast<uint8_t>(Phase::LiveStream) &&
               curPhase != static_cast<uint8_t>(Phase::StreamEndHold);
    }

    uint32_t nextFrameDueInMs() const override {
        const uint8_t curPhase = m_phase.load(std::memory_order_acquire);
        if (curPhase == static_cast<uint8_t>(Phase::StreamBlank)) {
            return (m_lastRenderedPhase != static_cast<uint8_t>(Phase::StreamBlank)) ? 1U : 0xFFFFFFFFU;
        }
        if (curPhase == static_cast<uint8_t>(Phase::StreamEndHold)) {
            return 0xFFFFFFFFU;
        }
        if (curPhase == static_cast<uint8_t>(Phase::LiveStream) &&
            m_renderCard.hasVideoFrame) {
            if (m_streamPlayer.isCore1Streaming()) {
                return m_streamPlayer.computeCore1NextFrameDueInMs(millis());
            }
            if (m_publishedFramePtr.load(std::memory_order_acquire) == nullptr) {
                return 0xFFFFFFFFU;
            }
            const uint32_t fSeq = m_frameSeq.load(std::memory_order_acquire);
            if ((fSeq & 1U) == 0U && fSeq != m_lastRenderedFrameSeq) {
                return 1U;
            }
            return engines::showcase::ShowcaseStreamPlayer::computeSelfPacedWakeupMs(
                m_nextStreamFrameDueMs.load(std::memory_order_relaxed),
                millis()
            );
        }
        return 0xFFFFFFFFU;
    }

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
    bool evictOnInactiveLowMemory() const override { return true; }

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

    static size_t countUtf8Glyphs(const char* utf8) {
        return engines::showcase::ShowcaseCardRenderer::countUtf8Glyphs(utf8);
    }

    static void copySafeCsvTokens(char* dst, size_t cap, const char* src);

    static uint8_t resolveEffectiveVideoFps(uint8_t requestedFps, bool hasPsram) {
        (void)hasPsram;
        return requestedFps < 8U ? 8U : (requestedFps > 60U ? 60U : requestedFps);
    }

    static bool shouldReportNetworkOffline(uint8_t consecutiveFailures) {
        return consecutiveFailures >= 3U;
    }

    static bool hasChunkedTransferEncoding(const char* rawHeaders) {
        return net::hasChunkedTransferEncoding(rawHeaders);
    }

    static constexpr size_t MAX_SYSTEMS_FILTER_LEN = 560;
    static constexpr size_t MAX_FILTER_LEN = 192;

protected:
    static constexpr size_t MAX_URL_LEN = 96;
    static constexpr size_t MAX_TITLE_LEN = engines::showcase::ShowcaseCardData::MAX_TITLE_LEN;
    static constexpr size_t MAX_BADGE_LEN = engines::showcase::ShowcaseCardData::MAX_BADGE_LEN;
    static constexpr size_t MAX_META_LEN = 16;
    static constexpr size_t MAX_SYNOPSIS_LEN = engines::showcase::ShowcaseCardData::MAX_SYNOPSIS_LEN;
    static constexpr size_t MAX_SESSION_ID_LEN = 48;
    static constexpr size_t MAX_ITEM_ID_LEN = 128;
    static constexpr size_t IO_SCRATCH_BYTES = 3072;
    static constexpr size_t FALLBACK_IO_SCRATCH_BYTES = 2048;
    static constexpr size_t MIN_IO_SCRATCH_BYTES = engines::showcase::ShowcaseStreamPlayer::MIN_SESSION_SCRATCH_BYTES;
    static constexpr size_t PSRAM_IO_SCRATCH_BYTES = 6144;

    enum class Phase : uint8_t {
        Card = 0,
        StreamBlank = 1,
        LiveStream = 2,
        StreamEndHold = 3
    };

    struct ConfigSlot {
        char serverUrl[MAX_URL_LEN] = "http://dmdmediashowcase.local:8890";
        char transportMode[16] = "mqtt_tcp";
        char mqttBrokerSource[12] = "global";
        char lang[8] = "system";
        char textScrollSpeed[12] = "4";
        char videoMode[16] = "auto_action";
        char videoType[16] = "any";
        char colorBoost[16] = "auto";
        uint16_t globalMqttPort = 1883;
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
    };

    struct ConfigExtras {
        char systems[MAX_SYSTEMS_FILTER_LEN] = "";
        char genres[MAX_FILTER_LEN] = "";
        char decades[MAX_FILTER_LEN] = "";
        char mqttCustomUrl[MAX_URL_LEN] = "";
        char globalMqttHost[64] = "";
    };

    struct AcquiredItemInfo {
        char itemId[MAX_ITEM_ID_LEN] = "";
        char streamPath[224] = "";
        char posterPath[224] = "";
        char baseServerUrl[112] = "";
        uint32_t streamMaxMs = 12000U;
        bool hasStream = false;
    };

    using CardSnapshot = engines::showcase::ShowcaseCardData;

    ShowcaseDomainId m_domain;
    IDrawingSurface* m_surface = nullptr;
    uint16_t m_width = 128;
    uint16_t m_height = 32;
    bool m_hasPsram = false;

    ConfigSlot m_cfg;
    ConfigExtras* m_cfgExtras = nullptr;
    std::atomic<uint32_t> m_cfgGen{1};
    std::atomic<uint32_t> m_cfgStreamBaseMs{12000};
    std::atomic<uint16_t> m_cfgSynopsisSpeedPx{20};
    std::atomic<bool> m_cfgEnabled{true};
    std::atomic<bool> m_cfgHasServerUrl{true};
    std::atomic<bool> m_cfgShowPoster{true};
    std::atomic<bool> m_cfgShowSynopsis{true};

    char m_sessionId[MAX_SESSION_ID_LEN] = "";
    std::atomic<uint32_t> m_seq{0};
    std::atomic<uint32_t> m_frameSeq{0};
    std::atomic<uint32_t> m_consumedFrameSeq{0};
    std::atomic<uint32_t> m_renderBlitSeq{0};
    std::atomic<uint32_t> m_presentedStreamFrames{0};
    std::atomic<uint32_t> m_nextStreamFrameDueMs{0};
    std::atomic<uint32_t> m_blankPresentedSeq{0};
    std::atomic<uint32_t> m_activeItemStreamMaxMs{0};
    std::atomic<const uint16_t*> m_publishedFramePtr{nullptr};

    CardSnapshot m_liveCard;
    engines::showcase::ShowcaseStreamPlayer m_streamPlayer;
    uint16_t* m_poster565 = nullptr;
    uint16_t* m_frame565 = nullptr;
    char* m_ioScratch = nullptr;

    size_t m_ioScratchCap = 0;
    size_t m_posterCapBytes = 0;
    size_t m_framePixels = 0;
    bool m_posterAliasesFrame = false;
    bool m_pausedByPreemption = false;
    uint32_t m_mqttConnSeq = 0;
    uint32_t m_lastPublishedCfgGen = 0;
    uint8_t m_consecutiveNetFailures = 0;

    std::atomic<bool> m_wantActive{false};
    std::atomic<bool> m_taskAlive{false};
    std::atomic<bool> m_buffersReady{false};
    std::atomic<bool> m_turnEvaluated{false};
    std::atomic<bool> m_readyStateReceived{false};
    std::atomic<bool> m_hasReadyItem{false};
    std::atomic<uint32_t> m_mqttConnectedAtMs{0};
    std::atomic<bool> m_skipTurnImmediately{false};
    std::atomic<bool> m_warmingFallback{false};
    std::atomic<bool> m_workerBusyOrHolding{false};
    std::atomic<bool> m_turnDone{false};
    std::atomic<uint8_t> m_phase{static_cast<uint8_t>(Phase::Card)};
    std::atomic<uint8_t> m_statusState{3};
    std::atomic<uint8_t> m_rotationSlotCount{1};
    std::atomic<uint8_t> m_consecutiveWarmSkips{0};
    std::atomic<uint32_t> m_rotationBudget{1};
    std::atomic<uint32_t> m_itemsPlayedInTurn{0};

#if defined(ESP32)
    TaskHandle_t m_task = nullptr;
#endif

    CardSnapshot m_renderCard;

    uint32_t m_activatedAtMs = 0;
    uint32_t m_phaseStartedAtMs = 0;
    uint32_t m_lastScrollStepMs = 0;
    uint32_t m_cardAnimStartMs = 0;
    uint32_t m_cardLastTickMs = 0;
    uint32_t m_synSubpixelRem = 0;
    uint32_t m_synScrollPx = 0;
    uint32_t m_titleSubpixelRem = 0;
    uint32_t m_titleStepPx = 0;
    uint32_t m_fallbackCycleMs = 24000;
    uint32_t m_lastRenderedCardSeq = 0xFFFFFFFFU;
    uint32_t m_lastRenderedFrameSeq = 0xFFFFFFFFU;
    uint8_t m_lastRenderedPhase = 0xFFU;
    bool m_dirty = true;
    bool m_renderedNewFrame = false;

    const char* domainSlug() const {
        return engines::showcase::ShowcaseCardRenderer::domainSlug(m_domain);
    }
    const char* domainBadgeLabel() const {
        return engines::showcase::ShowcaseCardRenderer::domainBadgeLabel(m_domain);
    }
    uint16_t defaultAccent565() const {
        return engines::showcase::ShowcaseCardRenderer::defaultAccent565(m_domain);
    }
    static uint32_t synopsisPxPerSec(const char* speedStr) {
        return engines::showcase::ShowcaseCardRenderer::synopsisPxPerSec(speedStr);
    }
    static uint32_t configuredMaxStreamMs(const ConfigSlot& cfg);
    uint32_t effectiveMaxStreamMs() const;
    static void computeCardPosterDims(uint16_t panelW, uint16_t panelH, uint16_t& outW, uint16_t& outH) {
        engines::showcase::ShowcaseCardRenderer::computeCardPosterDims(panelW, panelH, outW, outH);
    }
    static bool resolveMqttBroker(
        const ConfigSlot& cfg,
        const ConfigExtras* extras,
        char* outHost,
        size_t hostCap,
        uint16_t& outPort
    );
    void probeZeroCopyCanvasEligibility();
    bool allocateBuffers();
    void freeBuffers();
    void freePosterAfterBlank();
    void blankCardBeforeStream();
    bool acquireStepCFrameBuffer();
    void releaseStepCFrameBuffer();
    void waitRenderBlitQuiescent() const;
    void clearLiveCardState();
    bool startWorkerTask();
    void stopWorkerTaskBounded(uint32_t timeoutMs);
    static void workerTaskEntry(void* arg);
    void runWorkerLoop();

    enum class AcquireOutcome : uint8_t {
        ReadyItem = 0,
        ServerWarming = 1,
        ServerOffline = 2,
        BrokerUnreachable = 3,
        Aborted = 4
    };

    AcquireOutcome acquireItemViaMqtt(
        const ConfigSlot& cfg,
        const ConfigExtras* extras,
        const char* domain,
        const char* reqLang,
        uint32_t startGen,
        bool isFollowUpItemInTurn,
        CardSnapshot& outCard,
        AcquiredItemInfo& outInfo
    );
    AcquireOutcome acquireItemViaHttp(
        const ConfigSlot& cfg,
        const ConfigExtras* extras,
        const char* domain,
        const char* reqLang,
        uint32_t startGen,
        CardSnapshot& outCard,
        AcquiredItemInfo& outInfo
    );
    bool fetchBinaryPosterRgb565(
        const char* baseServerUrl,
        const char* posterPath,
        uint16_t* dst565,
        size_t expectedBytes
    );
    void executeAcquiredItem(
        const ConfigSlot& cfg,
        const char* domain,
        uint32_t startGen,
        CardSnapshot& card,
        const AcquiredItemInfo& info
    );

    void drawWaitingPixelArt(IDrawingSurface* s, int mW, int mH, uint32_t elapsedMs);
    void drawCardPhase(IDrawingSurface* s, const CardSnapshot& card, uint32_t elapsedMs);
    void drawScrollingLine(IDrawingSurface* s, const char* text, int minX, int maxX, int y, uint16_t color, uint32_t stepPx) {
        engines::showcase::ShowcaseCardRenderer::drawScrollingLine(s, text, minX, maxX, y, color, stepPx);
    }
    void drawSinglePassSynopsis(IDrawingSurface* s, const char* text, int minX, int maxX, int y, uint16_t color, uint32_t scrollPx) {
        engines::showcase::ShowcaseCardRenderer::drawSinglePassSynopsis(s, text, minX, maxX, y, color, scrollPx);
    }
};

static_assert(
    sizeof(MediaShowcaseBase) <= 1600,
    "MediaShowcaseBase instance footprint must remain <= 1600 bytes"
);

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
