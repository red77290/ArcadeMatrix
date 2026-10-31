#include "ShowcaseStreamPlayer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(ESP32)
#include "../../core/NetworkBudget.h"
#include <esp_heap_caps.h>
#if !defined(UNIT_TEST)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#endif

namespace engines::showcase {

namespace {

static_assert(
    sizeof(net::DmdzSpscStreamSession) <= ShowcaseStreamPlayer::MIN_SESSION_SCRATCH_BYTES,
    "DmdzSpscStreamSession must fit inside MIN_SESSION_SCRATCH_BYTES (1472 B) with zero heap alloc"
);
static_assert(
    sizeof(net::DmdzPreloadSession) <= ShowcaseStreamPlayer::MIN_SESSION_SCRATCH_BYTES,
    "DmdzPreloadSession must fit inside MIN_SESSION_SCRATCH_BYTES (1472 B) with zero heap alloc"
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
        if (!psBuf && preferredBytes > 65536U) {
            preferredBytes = 65536U;
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

    // Tier 1 (`profile="sram"`): try `10,240 B` keeping `>= 20 KB` free 8-bit heap for `esp_wifi`/lwIP,
    // else `8,192 B` (`>= 14 KB` free), else `6,144 B` / `4,096 B` step-down.
    const size_t candidates[4] = {
        SRAM_SPSC_HI_RING_BYTES,
        SRAM_SPSC_DEFAULT_RING_BYTES,
        SRAM_SPSC_LOW_RING_BYTES,
        SRAM_SPSC_MIN_RING_BYTES
    };
    const size_t postAllocMinFree8[4] = {
        20480U,
        14336U,
        10240U,
        6144U
    };
    for (size_t i = 0U; i < 4U; ++i) {
        const size_t cand = candidates[i];
        if (cand > preferredBytes && i == 0U) {
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
    // Unit-test / synthetic small-ring fallback (e.g. `preferredBytes <= 4096`)
    if (preferredBytes <= SRAM_SPSC_MIN_RING_BYTES && playerLargest8BitBlock() >= preferredBytes) {
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
        return m_spscRing.consumerSize() > 0U;
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
    const size_t safeThreshold = (cap > 1600U) ? (cap - 1600U) : (cap / 2U);
    if (m_streamHeaderIsDmdz) {
        uint16_t payLen = 0U;
        if (!m_spscRing.peekFrameLength(payLen)) {
            return false;
        }
        const size_t need = std::min<size_t>(2U + static_cast<size_t>(payLen), safeThreshold);
        return m_spscRing.consumerSize() >= need;
    }
    const size_t rawBytes = static_cast<size_t>(m_streamHeaderW) * static_cast<size_t>(m_streamHeaderH) * sizeof(uint16_t);
    const size_t need = std::min<size_t>(rawBytes, safeThreshold);
    return need > 0U && m_spscRing.consumerSize() >= need;
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

    if (!ensureDecoderAllocated(opts.hasPsram)) {
        return false;
    }

    if (opts.surfaceForReadOnlyCheck != nullptr) {
        refreshZeroCopyFromSurfaceReadOnly(opts.surfaceForReadOnlyCheck, opts.panelWidth, opts.panelHeight);
    }
    const bool zeroCopyEligible = m_zeroCopyEligible.load(std::memory_order_acquire);
    const char* profile = net::selectDmdzStreamProfile(opts.hasPsram, opts.panelWidth, opts.panelHeight);
    const char* tag = (opts.logTag && opts.logTag[0] != '\0') ? opts.logTag : "Showcase";
    const uint32_t cardT0 = (opts.cardStartedAtMs != 0U) ? opts.cardStartedAtMs : millis();
    const uint32_t maxStreamMs = (opts.maxStreamMs >= 2000U) ? opts.maxStreamMs : 15000U;

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

    auto runSpscSessionLoop = [&](net::DmdzSpscStreamSession& sess, bool alreadyOpened, uint8_t wsCredits, uint16_t wsChunk) -> bool {
        if (!alreadyOpened) {
            if (!sess.open(
                    opts.baseServerUrl,
                    opts.streamPath,
                    opts.clientId,
                    profile,
                    wsCredits,
                    wsChunk,
                    m_spscRing,
                    opts.checkStop,
                    opts.userCtx,
                    tag)) {
                return false;
            }
        }
        m_streamHeaderW = sess.header.width > 0U ? sess.header.width : opts.panelWidth;
        m_streamHeaderH = sess.header.height > 0U ? sess.header.height : opts.panelHeight;
        m_streamHeaderVersion = sess.header.version;
        m_streamHeaderIsDmdz = sess.header.isDmdz;
        m_streamTotalFrames = sess.header.totalFrames;
        const uint32_t safeFps = std::max<uint32_t>(1U, sess.header.fps);
        m_streamFrameIntervalUs = 1000000U / safeFps;

        bool tempFallback = false;
        if (!ensureFallbackBufferForHeader(m_streamHeaderW, m_streamHeaderH, tempFallback)) {
            sess.close(false);
            freeSpscRing();
            return false;
        }

        // Late Preemption (`FrontendSyncEngine`): buffer Frame 0 BEFORE preempting the marquee!
        if (opts.waitFirstFrameBeforeLive) {
            const uint32_t firstFrameT0 = millis();
            while (!hasBufferedFrameInSpscRing() &&
                   !sess.streamEof &&
                   checkStop() == net::StreamStopReason::Continue &&
                   (millis() - firstFrameT0) < 1500U) {
                if (!sess.pump(m_spscRing, 15U, opts.checkStop, opts.userCtx)) {
                    break;
                }
            }
            if (checkStop() != net::StreamStopReason::Continue || m_spscRing.empty()) {
                sess.close(false);
                if (tempFallback) {
                    freeFallbackFrameBuffer();
                }
                freeSpscRing();
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
        uint32_t eofDrainStartMs = 0U;
        while (checkStop() == net::StreamStopReason::Continue &&
               (millis() - streamT0) < maxStreamMs &&
               !m_core1StreamFinished.load(std::memory_order_acquire)) {
            if (!sess.streamEof) {
                (void)sess.pump(m_spscRing, 14U, opts.checkStop, opts.userCtx);
            } else {
                if (m_spscRing.empty() && m_core1DecPartialRem == 0U) {
                    break;
                }
                if (eofDrainStartMs == 0U) {
                    eofDrainStartMs = millis();
                } else if ((millis() - eofDrainStartMs) > 2500U) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(8));
            }
        }

        const uint32_t framesShown = m_presentedFrames.load(std::memory_order_acquire);
        sess.close(true);

        m_execMode.store(static_cast<uint8_t>(ShowcaseStreamExecMode::LegacyFramePtr), std::memory_order_release);
        m_nextFrameDueMs.store(0U, std::memory_order_relaxed);
        if (opts.onExitLiveStream) {
            opts.onExitLiveStream(opts.userCtx);
        }
        if (tempFallback) {
            freeFallbackFrameBuffer();
        }
        freeSpscRing();
        return framesShown > 0U;
    };

    // =========================================================================
    // TIER 1 (`profile == "sram"`, `!opts.hasPsram`)
    // =========================================================================
    if (!opts.hasPsram) {
        NetworkBudget::ScopedDmdNetActivity streamAct(NetworkBudget::DmdNetActivity::Stream);
        ScratchSessionGuard<net::DmdzSpscStreamSession> spscGuard(opts.ioScratch, opts.ioScratchCap);
        bool preOpened = false;
        if (opts.preStreamCardWaitMs > 0U) {
            const uint32_t preConnectOffsetMs = (opts.preStreamCardWaitMs > 300U)
                ? (opts.preStreamCardWaitMs - 300U)
                : 0U;
            while (checkStop() == net::StreamStopReason::Continue) {
                const uint32_t elapsed = millis() - cardT0;
                if (elapsed >= opts.preStreamCardWaitMs) {
                    break;
                }
                if (!preOpened &&
                    zeroCopyEligible &&
                    spscGuard.ptr != nullptr &&
                    elapsed >= preConnectOffsetMs) {
                    if (m_spscRingStorage != nullptr || allocateSpscRing(SRAM_SPSC_HI_RING_BYTES, false)) {
                        preOpened = spscGuard.ptr->open(
                            opts.baseServerUrl,
                            opts.streamPath,
                            opts.clientId,
                            profile,
                            4U,
                            1360U,
                            m_spscRing,
                            opts.checkStop,
                            opts.userCtx,
                            tag
                        );
                    }
                }
                if (preOpened && !spscGuard.ptr->streamEof) {
                    const uint32_t remCard = opts.preStreamCardWaitMs - elapsed;
                    (void)spscGuard.ptr->pump(
                        m_spscRing,
                        std::min<uint32_t>(20U, remCard),
                        opts.checkStop,
                        opts.userCtx
                    );
                } else {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }

        if (checkStop() != net::StreamStopReason::Continue) {
            if (preOpened && spscGuard.ptr) {
                spscGuard.ptr->close(true);
            }
            freeSpscRing();
            return false;
        }

        if (opts.onBeforeLiveStream) {
            opts.onBeforeLiveStream(opts.userCtx);
        }

        if (spscGuard.ptr != nullptr &&
            (m_spscRingStorage != nullptr || allocateSpscRing(SRAM_SPSC_HI_RING_BYTES, false))) {
            return runSpscSessionLoop(*spscGuard.ptr, preOpened, 4U, 1360U);
        }

        if (preOpened && spscGuard.ptr) {
            spscGuard.ptr->close(true);
        }
        freeSpscRing();
        return false;
    }

#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    // =========================================================================
    // TIER 2 (`profile == "psram"`, `W*H <= 8192`) & TIER 3 (`profile == "psram_hd"`, `W*H > 8192`)
    // =========================================================================
    NetworkBudget::ScopedDmdNetActivity streamAct(NetworkBudget::DmdNetActivity::Stream);
    const bool isTier3Hd = (static_cast<uint32_t>(opts.panelWidth) * static_cast<uint32_t>(opts.panelHeight)) > 8192U;
    size_t maxPreloadCap = PSRAM_TIER2_MAX_PRELOAD_BYTES;
    if (isTier3Hd) {
        const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        maxPreloadCap = (freePsram > PSRAM_TIER3_MIN_FREE_RESERVE_BYTES)
            ? std::min<size_t>(PSRAM_TIER3_MAX_PRELOAD_BYTES, freePsram - PSRAM_TIER3_MIN_FREE_RESERVE_BYTES)
            : 0U;
    }

    auto maybePurgeHttp = [&]() {
        if (opts.purgeOnComplete) {
            net::DmdzStreamClient::notifyHttpStreamPurge(
                opts.baseServerUrl,
                opts.streamPath,
                opts.clientId,
                profile
            );
        }
    };

    bool usePreloadPlayback = false;
    {
        ScratchSessionGuard<net::DmdzPreloadSession> preloadGuard(opts.ioScratch, opts.ioScratchCap);
        bool preloadOpened = false;
        if (preloadGuard.ptr != nullptr && maxPreloadCap >= 131072U) {
            m_preloadBytesDownloaded.store(0U, std::memory_order_release);
            m_preloadComplete.store(false, std::memory_order_release);
            preloadOpened = preloadGuard.ptr->open(
                opts.baseServerUrl,
                opts.streamPath,
                opts.clientId,
                profile,
                maxPreloadCap,
                m_preloadBytesDownloaded,
                m_preloadComplete,
                opts.checkStop,
                opts.userCtx,
                tag
            );
        }

        if (opts.preStreamCardWaitMs > 0U) {
            while (checkStop() == net::StreamStopReason::Continue) {
                const uint32_t elapsed = millis() - cardT0;
                if (elapsed >= opts.preStreamCardWaitMs) {
                    break;
                }
                if (preloadOpened && !m_preloadComplete.load(std::memory_order_acquire)) {
                    const uint32_t remCard = opts.preStreamCardWaitMs - elapsed;
                    (void)preloadGuard.ptr->pump(
                        m_preloadBytesDownloaded,
                        m_preloadComplete,
                        std::min<uint32_t>(30U, remCard),
                        opts.checkStop,
                        opts.userCtx
                    );
                } else {
                    vTaskDelay(pdMS_TO_TICKS(25));
                }
            }
        }

        if (checkStop() != net::StreamStopReason::Continue) {
            if (preloadOpened && preloadGuard.ptr) {
                preloadGuard.ptr->releasePreloadBuffer();
                maybePurgeHttp();
            }
            return false;
        }

        if (preloadOpened && preloadGuard.ptr && preloadGuard.ptr->headerParsed && !preloadGuard.ptr->tooLargeForPreload) {
            if (!isTier3Hd) {
                // Tier 2: finish remaining tail before `LiveStream` (`0` Wi-Fi contention during playback)
                const uint32_t finishBudgetMs = (opts.preStreamCardWaitMs > 0U) ? 900U : 2500U;
                const uint32_t finishT0 = millis();
                while (!m_preloadComplete.load(std::memory_order_acquire) &&
                       checkStop() == net::StreamStopReason::Continue &&
                       (millis() - finishT0) < finishBudgetMs) {
                    (void)preloadGuard.ptr->pump(
                        m_preloadBytesDownloaded,
                        m_preloadComplete,
                        25U,
                        opts.checkStop,
                        opts.userCtx
                    );
                }
                if (m_preloadComplete.load(std::memory_order_acquire)) {
                    preloadGuard.ptr->closeSocket();
                }
            } else {
                // Tier 3: ensure `>= 128 KB` or `>= 15` frames buffered before progressive playback
                const uint32_t cushionT0 = millis();
                while (!m_preloadComplete.load(std::memory_order_acquire) &&
                       m_preloadBytesDownloaded.load(std::memory_order_acquire) < PSRAM_TIER3_START_CUSHION_BYTES &&
                       preloadGuard.ptr->framesBuffered < 15U &&
                       checkStop() == net::StreamStopReason::Continue &&
                       (millis() - cushionT0) < 1200U) {
                    (void)preloadGuard.ptr->pump(
                        m_preloadBytesDownloaded,
                        m_preloadComplete,
                        25U,
                        opts.checkStop,
                        opts.userCtx
                    );
                }
            }

            const bool firstFrameReady = !opts.waitFirstFrameBeforeLive || (preloadGuard.ptr->framesBuffered >= 1U);
            if (preloadGuard.ptr->preloadBuf != nullptr &&
                m_preloadBytesDownloaded.load(std::memory_order_acquire) > 16U &&
                firstFrameReady) {
                usePreloadPlayback = true;
            }
        }

        if (usePreloadPlayback) {
            if (opts.onBeforeLiveStream) {
                opts.onBeforeLiveStream(opts.userCtx);
            }

            m_streamHeaderW = preloadGuard.ptr->header.width > 0U ? preloadGuard.ptr->header.width : opts.panelWidth;
            m_streamHeaderH = preloadGuard.ptr->header.height > 0U ? preloadGuard.ptr->header.height : opts.panelHeight;
            m_streamHeaderVersion = preloadGuard.ptr->header.version;
            m_streamHeaderIsDmdz = preloadGuard.ptr->header.isDmdz;
            m_streamTotalFrames = preloadGuard.ptr->header.totalFrames;
            const uint32_t safeFps = std::max<uint32_t>(1U, preloadGuard.ptr->header.fps);
            m_streamFrameIntervalUs = 1000000U / safeFps;

            uint32_t framesShown = 0U;
            bool tempFallback = false;
            if (ensureFallbackBufferForHeader(m_streamHeaderW, m_streamHeaderH, tempFallback)) {
                m_preloadReadPtr = preloadGuard.ptr->preloadBuf;
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
                        (void)preloadGuard.ptr->pump(
                            m_preloadBytesDownloaded,
                            m_preloadComplete,
                            18U,
                            opts.checkStop,
                            opts.userCtx
                        );
                        if (m_preloadComplete.load(std::memory_order_acquire)) {
                            preloadGuard.ptr->closeSocket();
                        }
                    } else {
                        vTaskDelay(pdMS_TO_TICKS(20));
                    }
                }

                framesShown = m_presentedFrames.load(std::memory_order_acquire);
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

            preloadGuard.ptr->releasePreloadBuffer();
            maybePurgeHttp();
            return framesShown > 0U;
        }

        if (preloadOpened && preloadGuard.ptr) {
            preloadGuard.ptr->releasePreloadBuffer();
        }
    }

    // Fallback for Tier 2 (`> 1.5 MB` -> `256 KB` PSRAM SPSC WS `credits=12, chunk=1400`)
    // and Tier 3 (`> maxPreloadCap` -> `768 KB` PSRAM SPSC WS `credits=16, chunk=1400`)
    if (opts.onBeforeLiveStream) {
        opts.onBeforeLiveStream(opts.userCtx);
    }

    const size_t psramRingTarget = isTier3Hd ? PSRAM_TIER3_SPSC_RING_BYTES : PSRAM_TIER2_SPSC_RING_BYTES;
    const uint8_t psramWsCredits = isTier3Hd ? 16U : 12U;
    ScratchSessionGuard<net::DmdzSpscStreamSession> wsFallbackGuard(opts.ioScratch, opts.ioScratchCap);
    if (wsFallbackGuard.ptr != nullptr && allocateSpscRing(psramRingTarget, true)) {
        const bool ok = runSpscSessionLoop(*wsFallbackGuard.ptr, false, psramWsCredits, 1400U);
        if (!ok) {
            maybePurgeHttp();
        }
        return ok;
    }

    maybePurgeHttp();
    return false;
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
        const size_t safeFullFrameThreshold = (ringCap > 1600U) ? (ringCap - 1600U) : (ringCap / 2U);
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
