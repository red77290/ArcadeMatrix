#pragma once

#include "../../core/drawing/DmdzStreamDecoder.h"
#include "../../core/drawing/IDrawingSurface.h"
#include "../../core/net/DmdzStreamClient.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

#if !defined(ESP32) || defined(BOARD_HAS_PSRAM)
#define SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD 1
#else
#define SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD 0
#endif

namespace engines::showcase {

enum class ShowcaseStreamExecMode : uint8_t {
    LegacyFramePtr = 0,
    SpscRing = 1,
    PsramPreload = 2
};

/**
 * @struct ShowcaseStreamRunOptions
 * @brief Zero-allocation POD parameters driving a 3-Tier adaptive Showcase stream session on Core 0.
 *        Shared 1:1 between `MediaShowcaseBase` (autonomous rotation engines) and
 *        `FrontendSyncEngine` + `InGameShowcaseEngine` (late-preempting Batocera/Recalbox sync).
 */
struct ShowcaseStreamRunOptions {
    const char* baseServerUrl = nullptr;
    const char* streamPath = nullptr;
    const char* clientId = nullptr;
    const char* logTag = "Showcase";
    const IDrawingSurface* surfaceForReadOnlyCheck = nullptr;
    char* ioScratch = nullptr;
    size_t ioScratchCap = 0U;
    uint16_t panelWidth = 128U;
    uint16_t panelHeight = 32U;
    uint32_t preStreamCardWaitMs = 0U;
    uint32_t cardStartedAtMs = 0U;
    uint32_t maxStreamMs = 15000U;
    bool hasPsram = false;
    bool purgeOnComplete = true;
    bool waitFirstFrameBeforeLive = false;

    void* userCtx = nullptr;
    net::StreamStopReason (*checkStop)(void* ctx) = nullptr;
    void (*onBeforeLiveStream)(void* ctx) = nullptr;
    void (*onEnterLiveStream)(void* ctx) = nullptr;
    void (*onExitLiveStream)(void* ctx) = nullptr;
};

/**
 * @class ShowcaseStreamPlayer
 * @brief Reusable 3-Tier (`sram` / `psram` / `psram_hd`) `DMDZ` v2 (`FRAME_PAL8` + RLE/LZ/Scroll2D)
 *        stream orchestrator (Core 0) and Zero-Copy `ScopedCanvasGuard` frame presenter (Core 1).
 *
 * Real-time invariants on Core 1 (`computeCore1NextFrameDueInMs`, `shouldMarkCore1Dirty`, `renderFrameCore1`):
 * - Zero heap allocations (`malloc`, `free`, `new`, `String`), zero `std::mutex`, zero blocking waits.
 * - Decodes directly in-place into `ScopedCanvasGuard::data()` when `hasCanvas() && getRotation() == 0`
 *   (`0 B` private engine RGB565 buffer on `esp32dev`), falling back automatically to a Core 0-allocated
 *   `m_fallbackFrame565` + `IDrawingSurface::blit565` when rotated (`90/180/270 deg`) or canvasless.
 */
class ShowcaseStreamPlayer {
public:
    static constexpr size_t MIN_SESSION_SCRATCH_BYTES = 1472U;
    static constexpr size_t SRAM_SPSC_HI_RING_BYTES = 10240U;
    static constexpr size_t SRAM_SPSC_DEFAULT_RING_BYTES = 8192U;
    static constexpr size_t SRAM_SPSC_LOW_RING_BYTES = 6144U;
    static constexpr size_t SRAM_SPSC_MIN_RING_BYTES = 4096U;
    static constexpr size_t PSRAM_TIER2_MAX_PRELOAD_BYTES = 1572864U;     // 1.5 MB
    static constexpr size_t PSRAM_TIER2_SPSC_RING_BYTES = 262144U;        // 256 KB
    static constexpr size_t PSRAM_TIER3_MAX_PRELOAD_BYTES = 3670016U;     // 3.5 MB
    static constexpr size_t PSRAM_TIER3_MIN_FREE_RESERVE_BYTES = 2621440U; // 2.5 MB
    static constexpr size_t PSRAM_TIER3_SPSC_RING_BYTES = 786432U;        // 768 KB
    static constexpr size_t PSRAM_TIER3_START_CUSHION_BYTES = 131072U;    // 128 KB

    ShowcaseStreamPlayer() = default;
    ~ShowcaseStreamPlayer();

    ShowcaseStreamPlayer(const ShowcaseStreamPlayer&) = delete;
    ShowcaseStreamPlayer& operator=(const ShowcaseStreamPlayer&) = delete;

    // --- Core 1 Zero-Copy Surface Probing ---
    bool probeZeroCopyOnCore1(IDrawingSurface* surface, uint16_t expectedW, uint16_t expectedH);
    bool refreshZeroCopyFromSurfaceReadOnly(
        const IDrawingSurface* surface,
        uint16_t expectedW,
        uint16_t expectedH
    );
    void onGeometryChanged(uint8_t rotation);
    bool isZeroCopyEligible() const {
        return m_zeroCopyEligible.load(std::memory_order_acquire);
    }
    void setZeroCopyEligible(bool eligible) {
        m_zeroCopyEligible.store(eligible, std::memory_order_release);
    }

    // --- Core 0 Lifecycle & Memory Management ---
    bool ensureDecoderAllocated(bool hasPsram);
    void freeDecoder();
    bool hasDecoder() const { return m_decoder != nullptr; }

    bool acquireFallbackFrameBuffer(uint16_t w, uint16_t h, bool hasPsram);
    void freeFallbackFrameBuffer();
    uint16_t* fallbackFrameBuffer() const { return m_fallbackFrame565; }
    size_t fallbackFramePixels() const { return m_fallbackFramePixels; }

    bool allocateSpscRing(size_t preferredBytes, bool hasPsram);
    void freeSpscRing();
    net::DmdzSpscByteRing& spscRing() { return m_spscRing; }
    const net::DmdzSpscByteRing& spscRing() const { return m_spscRing; }

    void resetCore1StreamState();
    void releaseAll();

    // --- Core 0 3-Tier Adaptive Stream Runner ---
    bool runStreamSession(const ShowcaseStreamRunOptions& opts);

    // --- Core 1 Lock-Free Real-Time Pacing & Presentation ---
    ShowcaseStreamExecMode execMode() const {
        return static_cast<ShowcaseStreamExecMode>(m_execMode.load(std::memory_order_acquire));
    }
    bool isCore1Streaming() const {
        return execMode() != ShowcaseStreamExecMode::LegacyFramePtr;
    }

    uint32_t presentedFrames() const {
        return m_presentedFrames.load(std::memory_order_relaxed);
    }
    void resetPresentedFrames() {
        m_presentedFrames.store(0U, std::memory_order_release);
    }

    uint32_t nextFrameDueMsRaw() const {
        return m_nextFrameDueMs.load(std::memory_order_relaxed);
    }
    void setNextFrameDueMsRaw(uint32_t dueMs) {
        m_nextFrameDueMs.store(dueMs, std::memory_order_relaxed);
    }

    static uint32_t computeSelfPacedWakeupMs(uint32_t dueMs, uint32_t nowMs);
    uint32_t computeCore1NextFrameDueInMs(uint32_t nowMs) const;
    bool shouldMarkCore1Dirty(uint32_t nowMs) const;
    bool renderFrameCore1(IDrawingSurface* surface, uint16_t defaultW, uint16_t defaultH);

    // --- Deterministic Unit-Test Helpers (Zero Socket Dependencies) ---
    void armSpscStreamForTest(
        uint16_t w,
        uint16_t h,
        uint8_t version,
        bool isDmdz,
        uint16_t totalFrames,
        uint32_t fps = 20U
    );

    void armPreloadStreamForTest(
        const uint8_t* preloadBuf,
        size_t bytesDownloaded,
        bool complete,
        uint16_t w,
        uint16_t h,
        uint8_t version,
        bool isDmdz,
        uint16_t totalFrames,
        uint32_t fps = 20U
    );
    void updatePreloadProgressForTest(size_t bytesDownloaded, bool complete);
    bool isCore1StreamFinished() const {
        return m_core1StreamFinished.load(std::memory_order_acquire);
    }

private:
    bool hasBufferedFrameInSpscRing() const;

    core::drawing::DmdzStreamDecoder* m_decoder = nullptr;
    uint16_t* m_fallbackFrame565 = nullptr;
    uint8_t* m_spscRingStorage = nullptr;
    net::DmdzSpscByteRing m_spscRing{};
    size_t m_fallbackFramePixels = 0U;

    const uint8_t* m_preloadReadPtr = nullptr;
    std::atomic<size_t> m_preloadBytesDownloaded{0U};
    std::atomic<bool> m_preloadComplete{false};
    std::atomic<bool> m_core1StreamFinished{false};
    std::atomic<bool> m_zeroCopyEligible{false};
    std::atomic<uint8_t> m_execMode{static_cast<uint8_t>(ShowcaseStreamExecMode::LegacyFramePtr)};
    std::atomic<uint32_t> m_presentedFrames{0U};
    std::atomic<uint32_t> m_nextFrameDueMs{0U};

    uint32_t m_streamFrameIntervalUs = 50000U;
    uint16_t m_streamHeaderW = 128U;
    uint16_t m_streamHeaderH = 32U;
    uint16_t m_streamTotalFrames = 0U;
    uint8_t m_streamHeaderVersion = 2U;
    bool m_streamHeaderIsDmdz = true;

    size_t m_core1PreloadOffset = 16U;
    uint32_t m_core1PreloadFrameIdx = 0U;
    uint32_t m_core1NextFrameDueUs = 0U;
    uint16_t m_core1DecPartialRem = 0U;
    bool m_core1DecoderInitialized = false;
    bool m_core1StreamArmed = false;
};

} // namespace engines::showcase
