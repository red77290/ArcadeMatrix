#include "ShowcaseStreamPlayer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(ESP32)
#include <esp_heap_caps.h>
#if !defined(UNIT_TEST)
#include "../../core/Logger.h"
#include "../../core/NetworkBudget.h"
#include "../../core/SDUtils.h"
#include "../../core/SdLockGuard.h"
#include "../../hal/BoardProfile.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#endif

namespace engines::showcase {

namespace {

static_assert(
    sizeof(net::DmdzHttpStreamSession) <= ShowcaseStreamPlayer::MIN_SESSION_SCRATCH_BYTES,
    "DmdzHttpStreamSession must fit inside MIN_SESSION_SCRATCH_BYTES (1472 B) with zero heap alloc"
);

#if defined(ESP32)
inline size_t playerFree8BitHeap() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

inline size_t playerLargest8BitBlock() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#endif

#if defined(ESP32) && !defined(UNIT_TEST)
template <typename T>
struct ScratchSessionGuard {
    T* ptr = nullptr;
    ScratchSessionGuard(char* scratch, size_t cap) {
        if (scratch && cap >= sizeof(T) && (reinterpret_cast<uintptr_t>(scratch) % alignof(T)) == 0U) {
            ptr = new (scratch) T();
        }
    }
    ~ScratchSessionGuard() {
        if (ptr) {
            ptr->~T();
            ptr = nullptr;
        }
    }
    ScratchSessionGuard(const ScratchSessionGuard&) = delete;
    ScratchSessionGuard& operator=(const ScratchSessionGuard&) = delete;
};

/**
 * @struct ScopedSdCacheFile
 * @brief Core 0 RAII SD cache file wrapper acquiring `SdLockGuard` strictly per bounded chunk
 *        so `sdMutex` is never held across network waits or FreeRTOS sleeps.
 */
struct ScopedSdCacheFile {
    FsFile file{};
    size_t curPos = 0U;
    bool isOpen = false;

    ScopedSdCacheFile() = default;
    ~ScopedSdCacheFile() {
        closeFile();
    }

    ScopedSdCacheFile(const ScopedSdCacheFile&) = delete;
    ScopedSdCacheFile& operator=(const ScopedSdCacheFile&) = delete;

    bool openReadWrite(const char* cachePath) {
        closeFile();
        if (!cachePath || cachePath[0] == '\0') {
            return false;
        }
        SdLockGuard guard(pdMS_TO_TICKS(600));
        if (!guard) {
            return false;
        }
        if (!sd.exists("/cache")) {
            sd.mkdir("/cache");
        }
#if USE_SD_MMC
        if (!sd.exists(cachePath)) {
            FsFile createFile = sd.open(cachePath, FILE_OPEN_WRITE);
            if (createFile) {
                createFile.close();
            }
        }
        file = sd.open(cachePath, "r+");
#else
        file = sd.open(cachePath, O_RDWR | O_CREAT);
#endif
        if (!file) {
            return false;
        }
        file.seek(0);
        curPos = 0U;
        isOpen = true;
        return true;
    }

    bool writeAt(size_t offset, const uint8_t* src, size_t len) {
        if (!src || len == 0U) {
            return true;
        }
        SdLockGuard guard(pdMS_TO_TICKS(500));
        if (!guard || !isOpen) {
            return false;
        }
        if (curPos != offset) {
            if (!file.seek(offset)) {
                return false;
            }
            curPos = offset;
        }
        const size_t w = file.write(src, len);
        curPos += w;
        return w == len;
    }

    void flushFile() {
        SdLockGuard guard(pdMS_TO_TICKS(400));
        if (guard && isOpen) {
            file.flush();
        }
    }

    bool readAt(size_t offset, uint8_t* dst, size_t len, size_t& outRead) {
        outRead = 0U;
        if (!dst || len == 0U) {
            return true;
        }
        SdLockGuard guard(pdMS_TO_TICKS(250));
        if (!guard || !isOpen) {
            return false;
        }
        if (curPos != offset) {
            if (!file.seek(offset)) {
                return false;
            }
            curPos = offset;
        }
        const int r = file.read(dst, len);
        if (r <= 0) {
            return false;
        }
        outRead = static_cast<size_t>(r);
        curPos += outRead;
        return true;
    }

    void closeFile() {
        if (isOpen) {
            SdLockGuard guard(pdMS_TO_TICKS(600));
            if (guard) {
                file.close();
            }
            isOpen = false;
        }
    }
};
#endif

} // namespace

ShowcaseStreamPlayer::~ShowcaseStreamPlayer() {
    releaseAll();
}

bool ShowcaseStreamPlayer::probeZeroCopyOnCore1(
    IDrawingSurface* surface,
    uint16_t expectedW,
    uint16_t expectedH
) {
    bool eligible = false;
    if (surface &&
        surface->hasCanvas() &&
        surface->getRotation() == 0U &&
        surface->physicalWidth() == static_cast<int16_t>(expectedW) &&
        surface->physicalHeight() == static_cast<int16_t>(expectedH)) {
        ScopedCanvasGuard probe(surface);
        if (probe.isValid() &&
            probe.width() == expectedW &&
            probe.height() == expectedH &&
            (probe.stridePixels() == 0U || probe.stridePixels() == expectedW)) {
            eligible = true;
        }
    }
    m_zeroCopyEligible.store(eligible, std::memory_order_release);
    return eligible;
}

bool ShowcaseStreamPlayer::refreshZeroCopyFromSurfaceReadOnly(
    const IDrawingSurface* surface,
    uint16_t expectedW,
    uint16_t expectedH
) {
    if (!surface) {
        return m_zeroCopyEligible.load(std::memory_order_acquire);
    }
    const bool eligible =
        surface->hasCanvas() &&
        surface->getRotation() == 0U &&
        surface->width() == static_cast<int16_t>(expectedW) &&
        surface->height() == static_cast<int16_t>(expectedH) &&
        surface->physicalWidth() == static_cast<int16_t>(expectedW) &&
        surface->physicalHeight() == static_cast<int16_t>(expectedH);
    m_zeroCopyEligible.store(eligible, std::memory_order_release);
    return eligible;
}

void ShowcaseStreamPlayer::onGeometryChanged(uint8_t rotation) {
    if (rotation != 0U) {
        m_zeroCopyEligible.store(false, std::memory_order_release);
    }
}

bool ShowcaseStreamPlayer::ensureDecoderAllocated(bool hasPsram) {
    if (m_decoder != nullptr) {
        return true;
    }
    void* decMem = nullptr;
#if defined(ESP32) && SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    if (hasPsram) {
        decMem = heap_caps_malloc(sizeof(core::drawing::DmdzStreamDecoder), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
#else
    (void)hasPsram;
#endif
    if (!decMem) {
        decMem = malloc(sizeof(core::drawing::DmdzStreamDecoder));
    }
    if (!decMem) {
        return false;
    }
    m_decoder = new (decMem) core::drawing::DmdzStreamDecoder();
    return true;
}

void ShowcaseStreamPlayer::freeDecoder() {
    if (m_decoder) {
        m_decoder->~DmdzStreamDecoder();
        free(m_decoder);
        m_decoder = nullptr;
    }
}

bool ShowcaseStreamPlayer::acquireFallbackFrameBuffer(uint16_t w, uint16_t h, bool hasPsram) {
    const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t frameBytes = pixels * sizeof(uint16_t);
    if (frameBytes == 0U) {
        return false;
    }
    if (m_fallbackFrame565 != nullptr && m_fallbackFramePixels >= pixels) {
        return true;
    }
    freeFallbackFrameBuffer();

#if defined(ESP32)
#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    if (hasPsram) {
        uint16_t* pBuf = static_cast<uint16_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (pBuf) {
            memset(pBuf, 0, frameBytes);
            m_fallbackFrame565 = pBuf;
            m_fallbackFramePixels = pixels;
            return true;
        }
    }
#else
    (void)hasPsram;
#endif
#if !defined(UNIT_TEST)
    if (!hasPsram &&
        (frameBytes > 16384U ||
         playerFree8BitHeap() < (frameBytes + 2560U) ||
         playerLargest8BitBlock() < frameBytes)) {
        return false;
    }
#endif
#else
    (void)hasPsram;
#endif

    uint16_t* buf = static_cast<uint16_t*>(malloc(frameBytes));
    if (!buf) {
        return false;
    }
    memset(buf, 0, frameBytes);
    m_fallbackFrame565 = buf;
    m_fallbackFramePixels = pixels;
    return true;
}

void ShowcaseStreamPlayer::freeFallbackFrameBuffer() {
    if (m_fallbackFrame565) {
        free(m_fallbackFrame565);
        m_fallbackFrame565 = nullptr;
    }
    m_fallbackFramePixels = 0U;
}

bool ShowcaseStreamPlayer::allocateSpscRing(size_t preferredBytes, bool hasPsram) {
    freeSpscRing();
    if (preferredBytes == 0U) {
        return false;
    }
#if defined(ESP32)
#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    if (hasPsram) {
        uint8_t* psBuf = static_cast<uint8_t*>(heap_caps_malloc(preferredBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!psBuf && preferredBytes > 262144U) {
            preferredBytes = 262144U;
            psBuf = static_cast<uint8_t*>(heap_caps_malloc(preferredBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
        if (!psBuf) {
            return false;
        }
        m_spscRingStorage = psBuf;
        m_spscRing.init(psBuf, preferredBytes);
        return true;
    }
#else
    (void)hasPsram;
#endif

    // Non-PSRAM (`esp32dev`): prefer `9,216 B` or `8,448 B` (`> 8,195 B` max `128x32` `FRAME_RAW`)
    // so every `128x32` frame is decoded atomically in 1 Core 1 tick with zero tearing.
    const size_t candidates[4] = {
        SRAM_SPSC_HI_RING_BYTES,
        SRAM_SPSC_DEFAULT_RING_BYTES,
        SRAM_SPSC_LOW_RING_BYTES,
        SRAM_SPSC_MIN_RING_BYTES
    };
    const size_t postAllocMinFree8[4] = {
        18432U,
        12288U,
        8192U,
        5120U
    };
    for (size_t i = 0U; i < 4U; ++i) {
        const size_t cand = candidates[i];
        if (cand > preferredBytes) {
            continue;
        }
        if (playerLargest8BitBlock() < cand) {
            continue;
        }
        uint8_t* mem = static_cast<uint8_t*>(malloc(cand));
        if (!mem) {
            continue;
        }
        if (playerFree8BitHeap() >= postAllocMinFree8[i]) {
            m_spscRingStorage = mem;
            m_spscRing.init(mem, cand);
            return true;
        }
        free(mem);
    }
    // Unit-test / synthetic explicit ring size fallback
    if (preferredBytes <= SRAM_SPSC_DEFAULT_RING_BYTES && playerLargest8BitBlock() >= preferredBytes) {
        uint8_t* mem = static_cast<uint8_t*>(malloc(preferredBytes));
        if (mem) {
            m_spscRingStorage = mem;
            m_spscRing.init(mem, preferredBytes);
            return true;
        }
    }
    return false;
#else
    (void)hasPsram;
    uint8_t* mem = static_cast<uint8_t*>(malloc(preferredBytes));
    if (!mem) {
        return false;
    }
    m_spscRingStorage = mem;
    m_spscRing.init(mem, preferredBytes);
    return true;
#endif
}

void ShowcaseStreamPlayer::freeSpscRing() {
    m_spscRing.init(nullptr, 0U);
    if (m_spscRingStorage) {
        free(m_spscRingStorage);
        m_spscRingStorage = nullptr;
    }
}

void ShowcaseStreamPlayer::resetCore1StreamState() {
    m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::LegacyFramePtr), std::memory_order_release);
    m_preloadReadPtr = nullptr;
    m_preloadBytesDownloaded.store(0U, std::memory_order_release);
    m_preloadComplete.store(false, std::memory_order_release);
    m_core1StreamFinished.store(false, std::memory_order_release);
    m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
    m_core1PreloadOffset = 16U;
    m_core1PreloadFrameIdx = 0U;
    m_core1NextFrameDueUs = 0U;
    m_core1DecPartialRem = 0U;
    m_core1DecoderInitialized = false;
    m_core1StreamArmed = false;
}

void ShowcaseStreamPlayer::releaseAll() {
    resetCore1StreamState();
    freeSpscRing();
    freeFallbackFrameBuffer();
    freeDecoder();
}

uint32_t ShowcaseStreamPlayer::computeSelfPacedWakeupMs(uint32_t dueMs, uint32_t nowMs) {
    if (dueMs != 0U) {
        const int32_t rem = static_cast<int32_t>(dueMs - nowMs);
        if (rem < -4) {
            return 8U;
        }
        if (rem <= 1) {
            return 2U;
        }
        if (rem < 16) {
            return static_cast<uint32_t>(rem);
        }
        return 16U;
    }
    return 8U;
}

uint32_t ShowcaseStreamPlayer::computeCore1NextFrameDueInMs(uint32_t nowMs) const {
    if (!isCore1Streaming()) {
        return 0xFFFFFFFFU;
    }
    if (m_presentedFrames.load(std::memory_order_relaxed) == 0U || m_core1DecPartialRem > 0U) {
        return 1U;
    }
    return computeSelfPacedWakeupMs(m_nextFrameDueMs.load(std::memory_order_relaxed), nowMs);
}

bool ShowcaseStreamPlayer::shouldMarkCore1Dirty(uint32_t nowMs) const {
    const ShowcaseStreamExecMode mode = execMode();
    if (mode == ShowcaseStreamExecMode::LegacyFramePtr) {
        return false;
    }
    const uint32_t dueMs = m_nextFrameDueMs.load(std::memory_order_relaxed);
    const bool frameDue = (!m_core1StreamArmed) ||
                          (m_core1DecPartialRem > 0U) ||
                          (dueMs == 0U) ||
                          (static_cast<int32_t>(nowMs - dueMs) >= -1);
    if (!frameDue) {
        return false;
    }
    if (mode == ShowcaseStreamExecMode::SpscRing) {
        if (m_core1DecPartialRem > 0U) {
            return m_spscRing.consumerSize() > 0U;
        }
        return hasBufferedFrameInSpscRing();
    }
#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    if (mode == ShowcaseStreamExecMode::PsramPreload) {
        const size_t dl = m_preloadBytesDownloaded.load(std::memory_order_acquire);
        return (dl > m_core1PreloadOffset) || m_preloadComplete.load(std::memory_order_acquire);
    }
#endif
    return false;
}

bool ShowcaseStreamPlayer::hasBufferedFrameInSpscRing() const {
    const size_t cap = m_spscRing.capacity();
    if (cap == 0U) {
        return false;
    }
    if (m_streamHeaderIsDmdz) {
        uint16_t payLen = 0U;
        if (!m_spscRing.peekFrameLength(payLen)) {
            return false;
        }
        const size_t totalWire = 2U + static_cast<size_t>(payLen);
        if (totalWire <= cap) {
            return m_spscRing.consumerSize() >= totalWire;
        }
        const size_t oversizedTrigger = std::max<size_t>(2U, std::min<size_t>(cap / 2U, 2048U));
        return m_spscRing.consumerSize() >= oversizedTrigger;
    }
    const size_t rawBytes = static_cast<size_t>(m_streamHeaderW) * static_cast<size_t>(m_streamHeaderH) * sizeof(uint16_t);
    if (rawBytes == 0U) {
        return false;
    }
    if (rawBytes <= cap) {
        return m_spscRing.consumerSize() >= rawBytes;
    }
    const size_t oversizedTrigger = std::max<size_t>(2U, std::min<size_t>(cap / 2U, 2048U));
    return m_spscRing.consumerSize() >= oversizedTrigger;
}

void ShowcaseStreamPlayer::armSpscStreamForTest(
    uint16_t w,
    uint16_t h,
    uint8_t version,
    bool isDmdz,
    uint16_t totalFrames,
    uint32_t fps
) {
    m_streamHeaderW = w > 0U ? w : 128U;
    m_streamHeaderH = h > 0U ? h : 32U;
    m_streamHeaderVersion = version;
    m_streamHeaderIsDmdz = isDmdz;
    m_streamTotalFrames = totalFrames;
    const uint32_t safeFps = std::max<uint32_t>(1U, fps);
    m_streamFrameIntervalUs = 1000000U / safeFps;

    m_presentedFrames.store(0U, std::memory_order_release);
    m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
    m_core1DecPartialRem = 0U;
    m_core1DecoderInitialized = false;
    m_core1StreamArmed = false;
    m_core1StreamFinished.store(false, std::memory_order_release);
    m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::SpscRing), std::memory_order_release);
}

void ShowcaseStreamPlayer::armPreloadStreamForTest(
    const uint8_t* preloadBuf,
    size_t bytesDownloaded,
    bool complete,
    uint16_t w,
    uint16_t h,
    uint8_t version,
    bool isDmdz,
    uint16_t totalFrames,
    uint32_t fps
) {
    m_preloadReadPtr = preloadBuf;
    m_preloadBytesDownloaded.store(bytesDownloaded, std::memory_order_release);
    m_preloadComplete.store(complete, std::memory_order_release);
    m_streamHeaderW = w > 0U ? w : 128U;
    m_streamHeaderH = h > 0U ? h : 32U;
    m_streamHeaderVersion = version;
    m_streamHeaderIsDmdz = isDmdz;
    m_streamTotalFrames = totalFrames;
    const uint32_t safeFps = std::max<uint32_t>(1U, fps);
    m_streamFrameIntervalUs = 1000000U / safeFps;

    m_presentedFrames.store(0U, std::memory_order_release);
    m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
    m_core1PreloadOffset = 16U;
    m_core1PreloadFrameIdx = 0U;
    m_core1DecPartialRem = 0U;
    m_core1DecoderInitialized = false;
    m_core1StreamArmed = false;
    m_core1StreamFinished.store(false, std::memory_order_release);
    m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::PsramPreload), std::memory_order_release);
}

void ShowcaseStreamPlayer::updatePreloadProgressForTest(size_t bytesDownloaded, bool complete) {
    m_preloadBytesDownloaded.store(bytesDownloaded, std::memory_order_release);
    m_preloadComplete.store(complete, std::memory_order_release);
}

bool ShowcaseStreamPlayer::runStreamSession(const ShowcaseStreamRunOptions& opts) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!opts.baseServerUrl || opts.baseServerUrl[0] == '\0' ||
        !opts.streamPath || opts.streamPath[0] == '\0' ||
        !opts.ioScratch || opts.ioScratchCap < MIN_SESSION_SCRATCH_BYTES) {
        return false;
    }

    auto checkStop = [&]() -> net::StreamStopReason {
        return opts.checkStop ? opts.checkStop(opts.userCtx) : net::StreamStopReason::Continue;
    };
    if (checkStop() != net::StreamStopReason::Continue) {
        return false;
    }

    const char* tag = (opts.logTag && opts.logTag[0] != '\0') ? opts.logTag : "Showcase";
    const uint32_t cardT0 = (opts.cardStartedAtMs != 0U) ? opts.cardStartedAtMs : millis();
    const uint32_t maxStreamMs = (opts.maxStreamMs >= 2000U) ? opts.maxStreamMs : 15000U;

    auto waitRemainingCardDwell = [&]() {
        if (opts.preStreamCardWaitMs == 0U) {
            return;
        }
        while (checkStop() == net::StreamStopReason::Continue &&
               (millis() - cardT0) < opts.preStreamCardWaitMs) {
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    };

    auto maybePurgeHttp = [&]() {
        if (opts.purgeOnComplete && checkStop() == net::StreamStopReason::Continue) {
            net::DmdzStreamClient::notifyHttpStreamPurge(
                opts.baseServerUrl,
                opts.streamPath,
                opts.clientId
            );
        }
    };

    if (!ensureDecoderAllocated(opts.hasPsram)) {
        waitRemainingCardDwell();
        return false;
    }

    if (opts.surfaceForReadOnlyCheck != nullptr) {
        refreshZeroCopyFromSurfaceReadOnly(opts.surfaceForReadOnlyCheck, opts.panelWidth, opts.panelHeight);
    }
    const bool zeroCopyEligible = m_zeroCopyEligible.load(std::memory_order_acquire);

    auto applyParsedHeader = [&](const core::drawing::DmdzStreamHeader& hdr) {
        m_streamHeaderW = hdr.width > 0U ? hdr.width : opts.panelWidth;
        m_streamHeaderH = hdr.height > 0U ? hdr.height : opts.panelHeight;
        m_streamHeaderVersion = hdr.version;
        m_streamHeaderIsDmdz = hdr.isDmdz;
        m_streamTotalFrames = hdr.totalFrames;
        const uint32_t safeFps = std::max<uint32_t>(1U, hdr.fps);
        m_streamFrameIntervalUs = 1000000U / safeFps;
    };

    auto ensureFallbackBufferForHeader = [&](uint16_t hdrW, uint16_t hdrH, bool& outAllocatedTemp) -> bool {
        outAllocatedTemp = false;
        const bool canZeroCopy = zeroCopyEligible && hdrW == opts.panelWidth && hdrH == opts.panelHeight;
        if (canZeroCopy) {
            return true;
        }
        const bool hadBefore = (m_fallbackFrame565 != nullptr &&
                                m_fallbackFramePixels >= static_cast<size_t>(hdrW) * static_cast<size_t>(hdrH));
        if (!acquireFallbackFrameBuffer(hdrW, hdrH, opts.hasPsram)) {
            return false;
        }
        outAllocatedTemp = !hadBefore && !opts.hasPsram;
        return true;
    };

    // =========================================================================
    // NON-PSRAM (`esp32dev`): Plain HTTP TCP -> 25 MHz SD Cache -> SPSC Ring
    // =========================================================================
    if (!opts.hasPsram) {
        if (!BoardProfile::current().isStorageAvailable() || sdMutex == nullptr) {
            LOGW(tag, "SD storage unavailable on non-PSRAM board; holding Info Card without video stream");
            waitRemainingCardDwell();
            maybePurgeHttp();
            return false;
        }

        NetworkBudget::ScopedDmdNetActivity streamAct(NetworkBudget::DmdNetActivity::Stream);
        ScratchSessionGuard<net::DmdzHttpStreamSession> httpGuard(opts.ioScratch, opts.ioScratchCap);
        if (!httpGuard.ptr ||
            !httpGuard.ptr->open(
                opts.baseServerUrl,
                opts.streamPath,
                opts.clientId,
                false,
                opts.checkStop,
                opts.userCtx,
                tag)) {
            waitRemainingCardDwell();
            maybePurgeHttp();
            return false;
        }

        const char* cachePath = opts.waitFirstFrameBeforeLive
            ? "/cache/showcase_sync.dmdz"
            : "/cache/showcase_live.dmdz";

        ScopedSdCacheFile sdCache;
        if (!sdCache.openReadWrite(cachePath) ||
            !sdCache.writeAt(0U, httpGuard.ptr->rawHeader16, 16U)) {
            LOGW(tag, "Failed to open/write SD stream cache '%s'", cachePath);
            httpGuard.ptr->close();
            waitRemainingCardDwell();
            maybePurgeHttp();
            return false;
        }

        const size_t sessionAlignedBytes =
            (sizeof(net::DmdzHttpStreamSession) + alignof(std::max_align_t) - 1U) &
            ~(alignof(std::max_align_t) - 1U);
        uint8_t fallbackStage[256];
        uint8_t* sdStageBuf = fallbackStage;
        size_t sdStageCap = sizeof(fallbackStage);
        if (opts.ioScratchCap > sessionAlignedBytes + 256U) {
            sdStageBuf = reinterpret_cast<uint8_t*>(opts.ioScratch + sessionAlignedBytes);
            sdStageCap = opts.ioScratchCap - sessionAlignedBytes;
        }

        size_t sdWritePos = 16U;
        size_t sdReadPos = 16U;
        bool tcpClosedCleanly = false;
        if (httpGuard.ptr->streamEof) {
            sdCache.flushFile();
            httpGuard.ptr->close();
            tcpClosedCleanly = true;
        }

        auto pumpTcpToSd = [&](size_t maxStepBytes, uint32_t sockWaitMs) -> bool {
            if (tcpClosedCleanly) {
                return true;
            }
            size_t totalStep = 0U;
            bool waited = false;
            while (totalStep < maxStepBytes && !httpGuard.ptr->streamEof) {
                const size_t want = std::min<size_t>(sdStageCap, maxStepBytes - totalStep);
                size_t got = 0U;
                const bool ok = httpGuard.ptr->readSome(
                    sdStageBuf,
                    want,
                    got,
                    waited ? 0U : sockWaitMs,
                    opts.checkStop,
                    opts.userCtx
                );
                waited = true;
                if (got > 0U) {
                    if (!sdCache.writeAt(sdWritePos, sdStageBuf, got)) {
                        httpGuard.ptr->close();
                        tcpClosedCleanly = true;
                        return false;
                    }
                    sdWritePos += got;
                    totalStep += got;
                }
                if (!ok) {
                    httpGuard.ptr->streamEof = true;
                    break;
                }
                if (got == 0U) {
                    break;
                }
            }
            if (httpGuard.ptr->streamEof && !tcpClosedCleanly) {
                sdCache.flushFile();
                httpGuard.ptr->close();
                tcpClosedCleanly = true;
            }
            return true;
        };

        // 1. Pre-download `.dmdz2` stream onto SD during Info Card dwell (or Late Preemption)
        if (opts.preStreamCardWaitMs > 0U) {
            while (checkStop() == net::StreamStopReason::Continue) {
                const uint32_t elapsed = millis() - cardT0;
                if (elapsed >= opts.preStreamCardWaitMs) {
                    break;
                }
                if (!tcpClosedCleanly) {
                    const uint32_t remCard = opts.preStreamCardWaitMs - elapsed;
                    if (!pumpTcpToSd(4096U, std::min<uint32_t>(20U, remCard))) {
                        break;
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(25));
                }
            }
            const uint32_t finishT0 = millis();
            while (!tcpClosedCleanly &&
                   checkStop() == net::StreamStopReason::Continue &&
                   (millis() - finishT0) < 1000U) {
                if (!pumpTcpToSd(4096U, 20U)) {
                    break;
                }
            }
        } else {
            // `FrontendSyncEngine` Late Preemption: download while local marquee plays uninterrupted
            const uint32_t syncDlT0 = millis();
            while (!tcpClosedCleanly &&
                   checkStop() == net::StreamStopReason::Continue &&
                   (millis() - syncDlT0) < 4000U) {
                if (!pumpTcpToSd(1536U, 15U)) {
                    break;
                }
                vTaskDelay(1);
            }
        }

        if (checkStop() != net::StreamStopReason::Continue || sdWritePos <= 16U) {
            httpGuard.ptr->close();
            sdCache.closeFile();
            if (sdWritePos <= 16U) {
                maybePurgeHttp();
            }
            return false;
        }

        // 2. Free poster (`+2 KB`) & allocate `8,448–9,216 B` SPSC ring for atomic `128x32` frames
        if (opts.onBeforeLiveStream) {
            opts.onBeforeLiveStream(opts.userCtx);
        }

        applyParsedHeader(httpGuard.ptr->header);

        if (m_spscRingStorage == nullptr && !allocateSpscRing(SRAM_SPSC_HI_RING_BYTES, false)) {
            httpGuard.ptr->close();
            sdCache.closeFile();
            maybePurgeHttp();
            return false;
        }

        bool tempFallback = false;
        if (!ensureFallbackBufferForHeader(m_streamHeaderW, m_streamHeaderH, tempFallback)) {
            httpGuard.ptr->close();
            sdCache.closeFile();
            freeSpscRing();
            maybePurgeHttp();
            return false;
        }

        auto pumpSdToRing = [&](size_t maxStepBytes) -> bool {
            size_t totalRead = 0U;
            while (sdReadPos < sdWritePos && totalRead < maxStepBytes) {
                const size_t contig = m_spscRing.contiguousWriteCap();
                if (contig == 0U) {
                    break;
                }
                const size_t availSd = sdWritePos - sdReadPos;
                const size_t remStep = maxStepBytes - totalRead;
                const size_t toRead = std::min<size_t>(
                    std::min<size_t>(contig, availSd),
                    std::min<size_t>(remStep, 2048U)
                );
                size_t got = 0U;
                if (!sdCache.readAt(sdReadPos, m_spscRing.writePtr(), toRead, got) || got == 0U) {
                    return false;
                }
                m_spscRing.commitWrite(got);
                sdReadPos += got;
                totalRead += got;
            }
            return true;
        };

        (void)pumpSdToRing(m_spscRing.capacity());

        if (opts.waitFirstFrameBeforeLive) {
            const uint32_t firstFrameT0 = millis();
            while (!hasBufferedFrameInSpscRing() &&
                   checkStop() == net::StreamStopReason::Continue &&
                   (millis() - firstFrameT0) < 1000U) {
                if (!tcpClosedCleanly) {
                    (void)pumpTcpToSd(2048U, 12U);
                }
                (void)pumpSdToRing(4096U);
                if (tcpClosedCleanly && sdReadPos >= sdWritePos) {
                    break;
                }
            }
            if (checkStop() != net::StreamStopReason::Continue || m_spscRing.empty()) {
                httpGuard.ptr->close();
                sdCache.closeFile();
                if (tempFallback) {
                    freeFallbackFrameBuffer();
                }
                freeSpscRing();
                if (m_spscRing.empty()) {
                    maybePurgeHttp();
                }
                return false;
            }
        }

        // 3. Arm Core 1 Zero-Copy playback (`0` Wi-Fi traffic when `tcpClosedCleanly == true`!)
        m_presentedFrames.store(0U, std::memory_order_release);
        m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
        m_core1DecPartialRem = 0U;
        m_core1DecoderInitialized = false;
        m_core1StreamArmed = false;
        m_core1StreamFinished.store(false, std::memory_order_release);
        m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::SpscRing), std::memory_order_release);

        if (opts.onEnterLiveStream) {
            opts.onEnterLiveStream(opts.userCtx);
        }

        const uint32_t streamT0 = millis();
        uint32_t eofStallSinceMs = 0U;
        size_t lastRingSize = m_spscRing.consumerSize();
        uint32_t lastPresented = 0U;

        while (checkStop() == net::StreamStopReason::Continue &&
               (millis() - streamT0) < maxStreamMs &&
               !m_core1StreamFinished.load(std::memory_order_acquire)) {
            if (!tcpClosedCleanly) {
                (void)pumpTcpToSd(4096U, 6U);
            }
            if (sdReadPos < sdWritePos && m_spscRing.producerFreeBytes() >= 512U) {
                if (!pumpSdToRing(tcpClosedCleanly ? 4096U : 2048U)) {
                    sdReadPos = sdWritePos;
                }
            }

            if (tcpClosedCleanly && sdReadPos >= sdWritePos) {
                const size_t curRingSize = m_spscRing.consumerSize();
                const uint32_t curPresented = m_presentedFrames.load(std::memory_order_relaxed);
                if (curRingSize == 0U && m_core1DecPartialRem == 0U) {
                    break;
                }
                if (curRingSize != lastRingSize || curPresented != lastPresented) {
                    lastRingSize = curRingSize;
                    lastPresented = curPresented;
                    eofStallSinceMs = millis();
                } else if (eofStallSinceMs == 0U) {
                    eofStallSinceMs = millis();
                } else if ((millis() - eofStallSinceMs) >= 400U) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(6));
            } else if (m_spscRing.producerFreeBytes() < 512U) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }

        const uint32_t framesShown = m_presentedFrames.load(std::memory_order_acquire);
        httpGuard.ptr->close();
        m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::LegacyFramePtr), std::memory_order_release);
        m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
        if (opts.onExitLiveStream) {
            opts.onExitLiveStream(opts.userCtx);
        }
        if (tempFallback) {
            freeFallbackFrameBuffer();
        }
        freeSpscRing();
        sdCache.closeFile();
        maybePurgeHttp();
        return framesShown > 0U;
    }

#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    // =========================================================================
    // PSRAM (`ESP32-S3`): 100% PSRAM-Only (`0` SD Card), RPi-Style Preload + 1 MB SPSC Fallback
    // =========================================================================
    NetworkBudget::ScopedDmdNetActivity streamAct(NetworkBudget::DmdNetActivity::Stream);
    ScratchSessionGuard<net::DmdzHttpStreamSession> httpGuard(opts.ioScratch, opts.ioScratchCap);
    if (!httpGuard.ptr ||
        !httpGuard.ptr->open(
            opts.baseServerUrl,
            opts.streamPath,
            opts.clientId,
            false,
            opts.checkStop,
            opts.userCtx,
            tag)) {
        waitRemainingCardDwell();
        maybePurgeHttp();
        return false;
    }

    applyParsedHeader(httpGuard.ptr->header);

    const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t maxPreloadPsram = (freePsram > PSRAM_MIN_FREE_RESERVE_BYTES)
        ? std::min<size_t>(PSRAM_MAX_PRELOAD_BYTES, freePsram - PSRAM_MIN_FREE_RESERVE_BYTES)
        : 0U;

    uint8_t* preloadBuf = nullptr;
    size_t preloadCap = 0U;
    if (httpGuard.ptr->hasContentLength &&
        httpGuard.ptr->contentLength >= 16U &&
        httpGuard.ptr->contentLength <= maxPreloadPsram) {
        preloadCap = httpGuard.ptr->contentLength;
        preloadBuf = static_cast<uint8_t*>(
            heap_caps_malloc(preloadCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
    }

    // Branch A: Full / Progressive PSRAM Preload (`Content-Length <= 4 MB`, `0` Wi-Fi during playback)
    if (preloadBuf != nullptr) {
        memcpy(preloadBuf, httpGuard.ptr->rawHeader16, 16U);
        size_t downloaded = 16U;
        m_preloadBytesDownloaded.store(16U, std::memory_order_release);
        m_preloadComplete.store(httpGuard.ptr->streamEof, std::memory_order_release);
        if (httpGuard.ptr->streamEof) {
            httpGuard.ptr->close();
        }

        auto pumpPsramPreload = [&](uint32_t waitMs) -> bool {
            if (m_preloadComplete.load(std::memory_order_relaxed)) {
                return true;
            }
            bool waited = false;
            while (downloaded < preloadCap && !httpGuard.ptr->streamEof) {
                const size_t want = std::min<size_t>(preloadCap - downloaded, 32768U);
                size_t got = 0U;
                const bool ok = httpGuard.ptr->readSome(
                    preloadBuf + downloaded,
                    want,
                    got,
                    waited ? 0U : waitMs,
                    opts.checkStop,
                    opts.userCtx
                );
                waited = true;
                if (got > 0U) {
                    downloaded += got;
                    m_preloadBytesDownloaded.store(downloaded, std::memory_order_release);
                }
                if (downloaded >= preloadCap || httpGuard.ptr->streamEof || !ok) {
                    m_preloadComplete.store(true, std::memory_order_release);
                    httpGuard.ptr->close();
                    return ok || downloaded > 16U;
                }
                if (got == 0U) {
                    break;
                }
            }
            if (downloaded >= preloadCap || httpGuard.ptr->streamEof) {
                m_preloadComplete.store(true, std::memory_order_release);
                httpGuard.ptr->close();
            }
            return true;
        };

        if (opts.preStreamCardWaitMs > 0U) {
            while (checkStop() == net::StreamStopReason::Continue) {
                const uint32_t elapsed = millis() - cardT0;
                if (elapsed >= opts.preStreamCardWaitMs) {
                    break;
                }
                if (!m_preloadComplete.load(std::memory_order_acquire)) {
                    const uint32_t remCard = opts.preStreamCardWaitMs - elapsed;
                    (void)pumpPsramPreload(std::min<uint32_t>(25U, remCard));
                } else {
                    vTaskDelay(pdMS_TO_TICKS(25));
                }
            }
        }

        const uint32_t finishBudgetMs = (opts.preStreamCardWaitMs > 0U) ? 1200U : 2500U;
        const uint32_t finishT0 = millis();
        while (!m_preloadComplete.load(std::memory_order_acquire) &&
               checkStop() == net::StreamStopReason::Continue &&
               (millis() - finishT0) < finishBudgetMs) {
            (void)pumpPsramPreload(20U);
        }

        if (opts.waitFirstFrameBeforeLive && !m_preloadComplete.load(std::memory_order_acquire)) {
            const uint32_t firstT0 = millis();
            while (checkStop() == net::StreamStopReason::Continue &&
                   (millis() - firstT0) < 1200U) {
                const size_t dl = m_preloadBytesDownloaded.load(std::memory_order_acquire);
                if (m_streamHeaderIsDmdz) {
                    if (dl >= 18U) {
                        const uint16_t f0Len = static_cast<uint16_t>(preloadBuf[16]) |
                                               (static_cast<uint16_t>(preloadBuf[17]) << 8);
                        if (dl >= 18U + static_cast<size_t>(f0Len)) {
                            break;
                        }
                    }
                } else {
                    const size_t rawBytes =
                        static_cast<size_t>(m_streamHeaderW) * static_cast<size_t>(m_streamHeaderH) * sizeof(uint16_t);
                    if (dl >= 16U + rawBytes) {
                        break;
                    }
                }
                if (m_preloadComplete.load(std::memory_order_acquire)) {
                    break;
                }
                (void)pumpPsramPreload(20U);
            }
        }

        if (checkStop() != net::StreamStopReason::Continue ||
            m_preloadBytesDownloaded.load(std::memory_order_acquire) <= 16U) {
            httpGuard.ptr->close();
            free(preloadBuf);
            if (checkStop() == net::StreamStopReason::Continue) {
                maybePurgeHttp();
            }
            return false;
        }

        if (opts.onBeforeLiveStream) {
            opts.onBeforeLiveStream(opts.userCtx);
        }

        uint32_t framesShown = 0U;
        bool tempFallback = false;
        if (ensureFallbackBufferForHeader(m_streamHeaderW, m_streamHeaderH, tempFallback)) {
            m_preloadReadPtr = preloadBuf;
            m_presentedFrames.store(0U, std::memory_order_release);
            m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
            m_core1PreloadOffset = 16U;
            m_core1PreloadFrameIdx = 0U;
            m_core1DecPartialRem = 0U;
            m_core1DecoderInitialized = false;
            m_core1StreamArmed = false;
            m_core1StreamFinished.store(false, std::memory_order_release);
            m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::PsramPreload), std::memory_order_release);

            if (opts.onEnterLiveStream) {
                opts.onEnterLiveStream(opts.userCtx);
            }

            const uint32_t streamT0 = millis();
            while (checkStop() == net::StreamStopReason::Continue &&
                   (millis() - streamT0) < maxStreamMs &&
                   !m_core1StreamFinished.load(std::memory_order_acquire)) {
                if (!m_preloadComplete.load(std::memory_order_acquire)) {
                    (void)pumpPsramPreload(16U);
                } else {
                    vTaskDelay(pdMS_TO_TICKS(18));
                }
            }

            framesShown = m_presentedFrames.load(std::memory_order_acquire);
            httpGuard.ptr->close();
            m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::LegacyFramePtr), std::memory_order_release);
            m_preloadReadPtr = nullptr;
            m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
            if (opts.onExitLiveStream) {
                opts.onExitLiveStream(opts.userCtx);
            }
            if (tempFallback) {
                freeFallbackFrameBuffer();
            }
        }

        httpGuard.ptr->close();
        free(preloadBuf);
        maybePurgeHttp();
        return framesShown > 0U;
    }

    // Branch B: Giant HD Clip (`> 4 MB` or chunked) -> Keep ALREADY-OPEN HTTP socket + `1 MB` PSRAM SPSC Ring
    if (!allocateSpscRing(PSRAM_STREAM_SPSC_RING_BYTES, true)) {
        httpGuard.ptr->close();
        waitRemainingCardDwell();
        maybePurgeHttp();
        return false;
    }

    if (opts.preStreamCardWaitMs > 0U) {
        while (checkStop() == net::StreamStopReason::Continue) {
            const uint32_t elapsed = millis() - cardT0;
            if (elapsed >= opts.preStreamCardWaitMs) {
                break;
            }
            if (!httpGuard.ptr->streamEof && m_spscRing.producerFreeBytes() >= 4096U) {
                const uint32_t remCard = opts.preStreamCardWaitMs - elapsed;
                if (!httpGuard.ptr->pumpToRing(
                        m_spscRing,
                        std::min<uint32_t>(25U, remCard),
                        opts.checkStop,
                        opts.userCtx)) {
                    httpGuard.ptr->streamEof = true;
                    httpGuard.ptr->close();
                } else if (httpGuard.ptr->streamEof) {
                    httpGuard.ptr->close();
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(25));
            }
        }
    }

    if (checkStop() != net::StreamStopReason::Continue) {
        httpGuard.ptr->close();
        freeSpscRing();
        return false;
    }

    if (opts.onBeforeLiveStream) {
        opts.onBeforeLiveStream(opts.userCtx);
    }

    bool tempFallback = false;
    if (!ensureFallbackBufferForHeader(m_streamHeaderW, m_streamHeaderH, tempFallback)) {
        httpGuard.ptr->close();
        freeSpscRing();
        maybePurgeHttp();
        return false;
    }

    if (opts.waitFirstFrameBeforeLive) {
        const uint32_t firstFrameT0 = millis();
        while (!hasBufferedFrameInSpscRing() &&
               !httpGuard.ptr->streamEof &&
               checkStop() == net::StreamStopReason::Continue &&
               (millis() - firstFrameT0) < 2000U) {
            if (!httpGuard.ptr->pumpToRing(m_spscRing, 15U, opts.checkStop, opts.userCtx)) {
                httpGuard.ptr->streamEof = true;
                break;
            }
        }
        if (checkStop() != net::StreamStopReason::Continue || m_spscRing.empty()) {
            httpGuard.ptr->close();
            if (tempFallback) {
                freeFallbackFrameBuffer();
            }
            freeSpscRing();
            if (checkStop() == net::StreamStopReason::Continue) {
                maybePurgeHttp();
            }
            return false;
        }
    }

    m_presentedFrames.store(0U, std::memory_order_release);
    m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
    m_core1DecPartialRem = 0U;
    m_core1DecoderInitialized = false;
    m_core1StreamArmed = false;
    m_core1StreamFinished.store(false, std::memory_order_release);
    m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::SpscRing), std::memory_order_release);

    if (opts.onEnterLiveStream) {
        opts.onEnterLiveStream(opts.userCtx);
    }

    const uint32_t streamT0 = millis();
    uint32_t eofStallSinceMs = 0U;
    size_t lastRingSize = m_spscRing.consumerSize();
    uint32_t lastPresented = 0U;

    while (checkStop() == net::StreamStopReason::Continue &&
           (millis() - streamT0) < maxStreamMs &&
           !m_core1StreamFinished.load(std::memory_order_acquire)) {
        if (!httpGuard.ptr->streamEof) {
            if (m_spscRing.producerFreeBytes() >= 4096U) {
                if (!httpGuard.ptr->pumpToRing(m_spscRing, 14U, opts.checkStop, opts.userCtx)) {
                    httpGuard.ptr->streamEof = true;
                    httpGuard.ptr->close();
                } else if (httpGuard.ptr->streamEof) {
                    httpGuard.ptr->close();
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(8));
            }
        } else {
            const size_t curRingSize = m_spscRing.consumerSize();
            const uint32_t curPresented = m_presentedFrames.load(std::memory_order_relaxed);
            if (curRingSize == 0U && m_core1DecPartialRem == 0U) {
                break;
            }
            if (curRingSize != lastRingSize || curPresented != lastPresented) {
                lastRingSize = curRingSize;
                lastPresented = curPresented;
                eofStallSinceMs = millis();
            } else if (eofStallSinceMs == 0U) {
                eofStallSinceMs = millis();
            } else if ((millis() - eofStallSinceMs) >= 400U) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(8));
        }
    }

    const uint32_t framesShown = m_presentedFrames.load(std::memory_order_acquire);
    httpGuard.ptr->close();
    m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::LegacyFramePtr), std::memory_order_release);
    m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
    if (opts.onExitLiveStream) {
        opts.onExitLiveStream(opts.userCtx);
    }
    if (tempFallback) {
        freeFallbackFrameBuffer();
    }
    freeSpscRing();
    maybePurgeHttp();
    return framesShown > 0U;
#else
    return false;
#endif
#else
    (void)opts;
    return false;
#endif
}

bool ShowcaseStreamPlayer::renderFrameCore1(
    IDrawingSurface* surface,
    uint16_t defaultW,
    uint16_t defaultH
) {
    if (!surface || !m_decoder) {
        return false;
    }

    const ShowcaseStreamExecMode mode = execMode();
#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    if (mode != ShowcaseStreamExecMode::SpscRing && mode != ShowcaseStreamExecMode::PsramPreload) {
        return false;
    }
#else
    if (mode != ShowcaseStreamExecMode::SpscRing) {
        return false;
    }
#endif

    const uint32_t nowUs = micros();
    if (m_core1StreamArmed && m_core1DecPartialRem == 0U) {
        const int32_t diffUs = static_cast<int32_t>(nowUs - m_core1NextFrameDueUs);
        if (diffUs < -750) {
            return false;
        }
    }

    const uint16_t decW = m_streamHeaderW > 0U ? m_streamHeaderW : defaultW;
    const uint16_t decH = m_streamHeaderH > 0U ? m_streamHeaderH : defaultH;
    const size_t decPixels = static_cast<size_t>(decW) * static_cast<size_t>(decH);
    const size_t rawFrameBytes = decPixels * sizeof(uint16_t);
    if (rawFrameBytes == 0U) {
        return false;
    }

    // Pre-check data availability BEFORE borrowing `ScopedCanvasGuard` (since `acquireCanvas()` marks surface dirty!)
    uint16_t spscPayloadLen = 0U;
    bool spscOversizedStart = false;
#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    const uint8_t* preloadFramePayload = nullptr;
    uint16_t preloadPayloadLen = 0U;
#endif

    if (mode == ShowcaseStreamExecMode::SpscRing) {
        const size_t ringCap = m_spscRing.capacity();
        if (ringCap == 0U) {
            return false;
        }
        const size_t safeFullFrameThreshold = ringCap;
        if (m_streamHeaderIsDmdz) {
            if (m_core1DecPartialRem > 0U) {
                if (m_spscRing.consumerSize() == 0U) {
                    return false;
                }
            } else {
                if (!m_spscRing.peekFrameLength(spscPayloadLen)) {
                    return false;
                }
                const size_t totalWire = 2U + static_cast<size_t>(spscPayloadLen);
                if (totalWire <= safeFullFrameThreshold) {
                    if (m_spscRing.consumerSize() < totalWire) {
                        return false;
                    }
                } else {
                    spscOversizedStart = true;
                }
            }
        } else {
            if (m_spscRing.consumerSize() < rawFrameBytes) {
                return false;
            }
        }
    }
#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    else {
        const uint8_t* base = m_preloadReadPtr;
        const size_t downloaded = m_preloadBytesDownloaded.load(std::memory_order_acquire);
        const bool complete = m_preloadComplete.load(std::memory_order_acquire);
        if (!base || (m_streamTotalFrames > 0U && m_core1PreloadFrameIdx >= m_streamTotalFrames)) {
            m_core1StreamFinished.store(true, std::memory_order_release);
            return false;
        }
        if (m_streamHeaderIsDmdz) {
            if (m_core1PreloadOffset + 2U > downloaded) {
                if (complete) {
                    m_core1StreamFinished.store(true, std::memory_order_release);
                }
                return false;
            }
            preloadPayloadLen = static_cast<uint16_t>(base[m_core1PreloadOffset]) |
                                (static_cast<uint16_t>(base[m_core1PreloadOffset + 1U]) << 8);
            const size_t endOff = m_core1PreloadOffset + 2U + static_cast<size_t>(preloadPayloadLen);
            if (endOff > downloaded) {
                if (complete) {
                    m_core1StreamFinished.store(true, std::memory_order_release);
                }
                return false;
            }
            preloadFramePayload = base + m_core1PreloadOffset + 2U;
        } else {
            const size_t endOff = m_core1PreloadOffset + rawFrameBytes;
            if (endOff > downloaded) {
                if (complete) {
                    m_core1StreamFinished.store(true, std::memory_order_release);
                }
                return false;
            }
            preloadFramePayload = base + m_core1PreloadOffset;
        }
    }
#endif

    // Borrow Zero-Copy linear canvas via `ScopedCanvasGuard` when unrotated & matching geometry;
    // otherwise decode into Core 0-allocated `m_fallbackFrame565` and blit via `surface->blit565`.
    const bool tryZeroCopy = m_zeroCopyEligible.load(std::memory_order_acquire) &&
                             surface->hasCanvas() &&
                             surface->getRotation() == 0U &&
                             decW == defaultW &&
                             decH == defaultH;

    ScopedCanvasGuard canvasGuard(tryZeroCopy ? surface : nullptr);
    const bool usingZeroCopy = canvasGuard.isValid() &&
                               canvasGuard.width() == decW &&
                               canvasGuard.height() == decH &&
                               (canvasGuard.stridePixels() == 0U || canvasGuard.stridePixels() == decW);

    uint16_t* dstPixels = usingZeroCopy ? canvasGuard.data() : m_fallbackFrame565;
    if (!dstPixels || (!usingZeroCopy && decPixels > m_fallbackFramePixels)) {
        canvasGuard.release();
        return false;
    }

    if (!m_core1DecoderInitialized) {
        m_core1DecoderInitialized = true;
        m_decoder->resetStream(decW, decH, m_streamHeaderVersion);
        memset(dstPixels, 0, rawFrameBytes);
    }

    bool frameComplete = false;
    bool decodeOk = true;

    if (mode == ShowcaseStreamExecMode::SpscRing) {
        if (!m_streamHeaderIsDmdz) {
            const size_t popped = m_spscRing.pop(reinterpret_cast<uint8_t*>(dstPixels), rawFrameBytes);
            decodeOk = (popped == rawFrameBytes);
            frameComplete = decodeOk;
        } else if (m_core1DecPartialRem > 0U) {
            const size_t avail = m_spscRing.consumerSize();
            const size_t take = (avail < m_core1DecPartialRem) ? avail : m_core1DecPartialRem;
            const uint8_t* p1 = nullptr;
            const uint8_t* p2 = nullptr;
            size_t n1 = 0U;
            size_t n2 = 0U;
            m_spscRing.getConsumerSlices(0U, take, p1, n1, p2, n2);
            if (n1 > 0U && !m_decoder->feed(p1, n1, dstPixels)) {
                decodeOk = false;
            }
            if (decodeOk && n2 > 0U && !m_decoder->feed(p2, n2, dstPixels)) {
                decodeOk = false;
            }
            m_spscRing.consumeRead(take);
            m_core1DecPartialRem = static_cast<uint16_t>(m_core1DecPartialRem - take);
            if (!decodeOk) {
                m_core1DecPartialRem = 0U;
                m_core1StreamFinished.store(true, std::memory_order_release);
            } else if (m_core1DecPartialRem == 0U) {
                decodeOk = m_decoder->finishFrame();
                frameComplete = decodeOk;
            }
        } else if (spscOversizedStart) {
            m_spscRing.consumeRead(2U);
            if (!m_decoder->beginFrame(spscPayloadLen)) {
                m_core1StreamFinished.store(true, std::memory_order_release);
                canvasGuard.release();
                return false;
            }
            m_core1DecPartialRem = spscPayloadLen;
            const size_t avail = m_spscRing.consumerSize();
            const size_t take = (avail < m_core1DecPartialRem) ? avail : m_core1DecPartialRem;
            if (take > 0U) {
                const uint8_t* p1 = nullptr;
                const uint8_t* p2 = nullptr;
                size_t n1 = 0U;
                size_t n2 = 0U;
                m_spscRing.getConsumerSlices(0U, take, p1, n1, p2, n2);
                if (n1 > 0U && !m_decoder->feed(p1, n1, dstPixels)) {
                    decodeOk = false;
                }
                if (decodeOk && n2 > 0U && !m_decoder->feed(p2, n2, dstPixels)) {
                    decodeOk = false;
                }
                m_spscRing.consumeRead(take);
                m_core1DecPartialRem = static_cast<uint16_t>(m_core1DecPartialRem - take);
            }
            if (!decodeOk) {
                m_core1DecPartialRem = 0U;
                m_core1StreamFinished.store(true, std::memory_order_release);
            } else if (m_core1DecPartialRem == 0U) {
                decodeOk = m_decoder->finishFrame();
                frameComplete = decodeOk;
            }
        } else {
            const uint8_t* p1 = nullptr;
            const uint8_t* p2 = nullptr;
            size_t n1 = 0U;
            size_t n2 = 0U;
            m_spscRing.getConsumerSlices(2U, spscPayloadLen, p1, n1, p2, n2);
            decodeOk = m_decoder->beginFrame(spscPayloadLen);
            if (decodeOk && n1 > 0U) {
                decodeOk = m_decoder->feed(p1, n1, dstPixels);
            }
            if (decodeOk && n2 > 0U) {
                decodeOk = m_decoder->feed(p2, n2, dstPixels);
            }
            if (decodeOk) {
                decodeOk = m_decoder->finishFrame();
            }
            m_spscRing.consumeRead(2U + static_cast<size_t>(spscPayloadLen));
            frameComplete = decodeOk;
            if (!decodeOk) {
                m_core1StreamFinished.store(true, std::memory_order_release);
            }
        }
    }
#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    else {
        if (m_streamHeaderIsDmdz) {
            decodeOk = m_decoder->beginFrame(preloadPayloadLen) &&
                       (preloadPayloadLen == 0U || m_decoder->feed(preloadFramePayload, preloadPayloadLen, dstPixels)) &&
                       m_decoder->finishFrame();
            m_core1PreloadOffset += 2U + static_cast<size_t>(preloadPayloadLen);
        } else {
            memcpy(dstPixels, preloadFramePayload, rawFrameBytes);
            m_core1PreloadOffset += rawFrameBytes;
            decodeOk = true;
        }
        ++m_core1PreloadFrameIdx;
        frameComplete = decodeOk;
        if (!decodeOk ||
            (m_streamTotalFrames > 0U && m_core1PreloadFrameIdx >= m_streamTotalFrames)) {
            m_core1StreamFinished.store(true, std::memory_order_release);
        }
    }
#endif

    if (!frameComplete) {
        canvasGuard.release();
        return false;
    }

    if (usingZeroCopy) {
        canvasGuard.markDrawnAndRelease();
    } else {
        canvasGuard.release();
        if (surface->width() != static_cast<int>(decW) || surface->height() != static_cast<int>(decH)) {
            surface->clear(0);
        }
        surface->blit565(dstPixels, 0, 0, static_cast<int16_t>(decW), static_cast<int16_t>(decH));
    }

    const uint32_t presented = m_presentedFrames.fetch_add(1U, std::memory_order_relaxed) + 1U;
    if (m_streamTotalFrames > 0U && presented >= m_streamTotalFrames) {
        m_core1StreamFinished.store(true, std::memory_order_release);
    }

    const uint32_t postDecodeUs = micros();
    if (!m_core1StreamArmed) {
        m_core1StreamArmed = true;
        m_core1NextFrameDueUs = postDecodeUs + m_streamFrameIntervalUs;
    } else {
        m_core1NextFrameDueUs += m_streamFrameIntervalUs;
        // Smooth catchup without burst if behind by `> 2` frames
        const uint32_t maxLagUs = m_streamFrameIntervalUs * 2U;
        if (static_cast<int32_t>(postDecodeUs - m_core1NextFrameDueUs) > static_cast<int32_t>(maxLagUs)) {
            m_core1NextFrameDueUs = postDecodeUs + m_streamFrameIntervalUs;
        }
    }
    const int32_t remUs = static_cast<int32_t>(m_core1NextFrameDueUs - postDecodeUs);
    const uint32_t remMs = (remUs > 0) ? static_cast<uint32_t>((remUs + 999) / 1000) : 1U;
    m_nextFrameDueMs.store(millis() + remMs, std::memory_order_relaxed);
    return true;
}

} // namespace engines::showcase
