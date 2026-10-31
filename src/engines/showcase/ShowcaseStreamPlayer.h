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
 * @brief Zero-allocation POD parameters driving a Showcase `DMDZ` v2 stream session on Core 0.
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
 * @brief Shared lossless `16-bit` True-RGB565 `DMDZ` v2 (`RLE` / `LZ` / `Scroll2D` / `Delta` / `Raw`)
 *        stream orchestrator (Core 0) and Zero-Copy `ScopedCanvasGuard` frame presenter (Core 1).
 *
 * Target strategies (100% shared between `MediaShowcaseBase` and `FrontendSyncEngine`):
 * - `esp32dev` (`!hasPsram`): downloads `.rgb565.stream?purge=false&codec=dmdz2` over plain HTTP TCP
 *   onto the `25 MHz` SD card (`/cache/showcase_live.dmdz` or `/cache/showcase_sync.dmdz`) during Info Card
 *   dwell (`0` heap alloc during download), closes the TCP socket (`0` Wi-Fi traffic during playback),
 *   and streams from SD into an `8,448–9,216 B` SPSC ring (`> 8,195 B` max `128x32` `FRAME_RAW`) for
 *   atomic single-tick Zero-Copy `SRAM1` Canvas presentation on Core 1.
 * - `ESP32-S3` (`hasPsram`): 100% PSRAM-only (`0` SD card), preloading clips `<= 4 MB` into PSRAM during
 *   Info Card dwell (closing TCP before Frame 0) and falling back on the same open HTTP socket to a
 *   `1 MB` PSRAM SPSC ring for giant `256x64` HD clips (`> 4 MB`).
 *
 * Real-time invariants on Core 1 (`computeCore1NextFrameDueInMs`, `shouldMarkCore1Dirty`, `renderFrameCore1`):
 * - Zero heap allocations (`malloc`, `free`, `new`, `String`), zero `std::mutex`, zero SD/socket I/O.
 */
class ShowcaseStreamPlayer {
public:
    static constexpr size_t MIN_SESSION_SCRATCH_BYTES = 1472U;
    static constexpr size_t SRAM_SPSC_HI_RING_BYTES = 9216U;
    static constexpr size_t SRAM_SPSC_DEFAULT_RING_BYTES = 8448U;
    static constexpr size_t SRAM_SPSC_LOW_RING_BYTES = 6144U;
    static constexpr size_t SRAM_SPSC_MIN_RING_BYTES = 4096U;
    static constexpr size_t PSRAM_MAX_PRELOAD_BYTES = 4194304U;       // 4.0 MB
    static constexpr size_t PSRAM_MIN_FREE_RESERVE_BYTES = 1572864U;  // 1.5 MB
    static constexpr size_t PSRAM_STREAM_SPSC_RING_BYTES = 1048576U;  // 1.0 MB

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

    // --- Core 0 Stream Session Runner ---
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
    bool shouldRefillSpscRing(size_t batchMinFreeBytes) const;
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
    mutable std::atomic<bool> m_spscConsumerNeedsTopUp{false};
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
