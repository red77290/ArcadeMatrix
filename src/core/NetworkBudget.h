#pragma once

#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>

/**
 * @file NetworkBudget.h
 * @brief Internal DRAM admission control for TLS sessions.
 *
 * Provides admission thresholds and telemetry for TLS sessions and HTTP transactions.
 * mbedTLS on this board uses the stock ESP-IDF/Arduino allocator (100% internal DRAM,
 * never PSRAM -- see HardwareHAL::begin() for why: any mbedTLS PSRAM traffic contends
 * with the PSRAM-resident HUB75 framebuffer's GDMA engine and corrupts the display).
 * Because mbedTLS's ~32KB combined TLS record buffers and lwIP's PCBs/socket tables/
 * AsyncTCP buffers all compete for the same limited internal DRAM, every TLS handshake
 * must be admission-gated and serialized (see ScopedTlsHandshakeLock below).
 *
 * Safety boundaries:
 * - Hard safety limit: freeInternal >= 30 KB, largestInternalBlock >= 16896 bytes.
 * - Healthy target under nominal streaming: freeInternal >= 50 KB.
 */
namespace NetworkBudget {

/// Hard safety admission threshold for internal DRAM (needs ~33.8 KB record buffers + ~5 KB context/crypto + ~6 KB margin).
static constexpr uint32_t TLS_MIN_FREE_INTERNAL = 45u * 1024u; // 46,080 bytes

/// Contiguous allocation watermark to satisfy a single 16 KB mbedTLS record buffer.
static constexpr uint32_t TLS_MIN_LARGEST_BLOCK = 16896u; // 16.5 KB

/// Contiguous watermark guaranteed to satisfy BOTH 16 KB mbedTLS record buffers simultaneously.
static constexpr uint32_t TLS_MIN_COMBINED_BLOCK = 35328u; // 34.5 KB

/// Healthy operation target for internal DRAM with active stream.
static constexpr uint32_t HEALTHY_FREE_INTERNAL_TARGET = 50u * 1024u; // 51,200 bytes

/**
 * @brief Returns the total free internal DRAM in bytes.
 */
inline uint32_t freeInternal() {
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

/**
 * @brief Returns the largest contiguous free internal DRAM block in bytes.
 */
inline uint32_t largestInternalBlock() {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

/**
 * @brief Evaluates whether internal DRAM can accommodate both mbedTLS record buffers (in + out).
 *
 * mbedTLS setup allocates TWO separate record buffers (~16.7 KB each, ~33.4 KB total).
 * If the largest block is >= 34.5 KB, both buffers will fit in that single block.
 * If largest < 16.5 KB, even a single buffer cannot be allocated.
 * If in between, it probes whether two simultaneous 16.5 KB allocations can actually succeed,
 * eliminating false admissions that would otherwise crash mbedtls_ssl_setup with -32512.
 */
inline bool hasTlsRecordBufferHeadroom() {
    const uint32_t largest = largestInternalBlock();
    if (largest >= TLS_MIN_COMBINED_BLOCK) {
        return true;
    }
    if (largest < TLS_MIN_LARGEST_BLOCK) {
        return false;
    }
    void* b1 = heap_caps_malloc(TLS_MIN_LARGEST_BLOCK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!b1) return false;
    void* b2 = heap_caps_malloc(TLS_MIN_LARGEST_BLOCK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_caps_free(b1);
    if (!b2) return false;
    heap_caps_free(b2);
    return true;
}

/// Minimum free internal DMA-capable memory to satisfy hardware SHA and SDMMC bounce buffers.
static constexpr uint32_t TLS_MIN_FREE_DMA = 16384u; // 16 KB
static constexpr uint32_t TLS_MIN_LARGEST_DMA_BLOCK = 4096u; // 4 KB for esp-sha buffer

/**
 * @brief Returns the total free internal DMA-capable memory in bytes.
 */
inline uint32_t freeDmaInternal() {
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

/**
 * @brief Returns the largest contiguous free internal DMA-capable block in bytes.
 */
inline uint32_t largestDmaInternalBlock() {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

/**
 * @brief Thread-safe telemetry counter tracking rejected TLS attempts.
 */
inline std::atomic<uint32_t>& getTlsDeniedCount() {
    static std::atomic<uint32_t> count{0};
    return count;
}

/**
 * @brief Tells whether a TLS handshake may reasonably be attempted right now.
 *
 * Checks total free internal DRAM, dual-buffer contiguous capacity (mbedTLS in/out buffers),
 * and DMA-capable heap for hardware SHA acceleration (esp-sha buffer allocation).
 *
 * @return true when there is enough internal DRAM and DMA headroom for a TLS session.
 */
inline bool canStartTlsSession() {
    const uint32_t free = freeInternal();
    const uint32_t largest = largestInternalBlock();
    const uint32_t freeDma = freeDmaInternal();
    const uint32_t largestDma = largestDmaInternalBlock();
    const bool hasBuffers = hasTlsRecordBufferHeadroom();
    const bool admitted = (free >= TLS_MIN_FREE_INTERNAL && hasBuffers &&
                           freeDma >= TLS_MIN_FREE_DMA && largestDma >= TLS_MIN_LARGEST_DMA_BLOCK);
    if (!admitted) {
        getTlsDeniedCount().fetch_add(1, std::memory_order_relaxed);
        static std::atomic<uint32_t> lastDenialLogMs{0};
        uint32_t now = millis();
        uint32_t last = lastDenialLogMs.load(std::memory_order_relaxed);
        if (now - last > 10000 && lastDenialLogMs.compare_exchange_strong(last, now)) {
            log_w("TLS admission denied: free=%u (req %u), largest=%u (req %u), freeDma=%u (req %u), largestDma=%u (req %u), buffers=%s, total denied=%u",
                  free, TLS_MIN_FREE_INTERNAL, largest, TLS_MIN_LARGEST_BLOCK,
                  freeDma, TLS_MIN_FREE_DMA, largestDma, TLS_MIN_LARGEST_DMA_BLOCK,
                  hasBuffers ? "OK" : "INSUFFICIENT",
                  getTlsDeniedCount().load(std::memory_order_relaxed));
        }
    }
    return admitted;
}

inline uint32_t freePsram() {
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

inline SemaphoreHandle_t getTlsHandshakeMutex() {
    static SemaphoreHandle_t s_handshakeMutex = xSemaphoreCreateMutex();
    return s_handshakeMutex;
}

/**
 * @class ScopedTlsHandshakeLock
 * @brief Scoped lock for heavy TLS handshakes on Core 0.
 *
 * Invariant: Must only be held during the initial TLS handshake (connect()),
 * NEVER across the entire HTTP response transfer, to prevent starving other network tasks.
 *
 * Atomicity: the internal-DRAM admission check (canStartTlsSession()) is re-validated
 * HERE, while holding the handshake mutex, immediately before reporting success to the
 * caller. A separate "check budget, then separately try to acquire this lock" sequence
 * at the call site is NOT sufficient on its own: if the mutex is contended, an arbitrary
 * amount of time can pass waiting for it (up to the timeout), during which another task's
 * concurrent TLS handshake -- or any other internal-DRAM allocation (SD reads, JSON
 * parsing, sockets) -- can invalidate that earlier check by the time this task actually
 * gets the mutex. Re-checking here, atomically with acquiring the lock, closes that
 * TOCTOU window with respect to every other TLS handshake in the system (the dominant,
 * ~32KB-at-a-time source of internal DRAM pressure): nothing else can begin a TLS
 * handshake between "admission confirmed" and the reservation (this lock) being held.
 * Callers may still perform their own cheap canStartTlsSession() pre-check before even
 * attempting to construct this lock, purely as an optimization to avoid blocking on an
 * already-known-insufficient budget -- but that pre-check is never authoritative; only
 * this constructor's post-lock re-check is.
 */
class ScopedTlsHandshakeLock {
public:
    explicit ScopedTlsHandshakeLock(TickType_t timeout = pdMS_TO_TICKS(5000))
        : _locked(false), _deniedByBudget(false) {
        SemaphoreHandle_t m = getTlsHandshakeMutex();
        if (m && xSemaphoreTake(m, timeout) == pdTRUE) {
            // Authoritative admission check, performed atomically under the mutex: no other
            // TLS handshake can be in flight or start while we hold this lock, so this is the
            // freshest possible view of internal DRAM immediately before the caller connects.
            if (canStartTlsSession()) {
                _locked = true;
            } else {
                _deniedByBudget = true;
                xSemaphoreGive(m);
            }
        }
    }

    ~ScopedTlsHandshakeLock() {
        unlock();
    }

    void unlock() {
        if (_locked) {
            SemaphoreHandle_t m = getTlsHandshakeMutex();
            if (m) xSemaphoreGive(m);
            _locked = false;
        }
    }

    bool isLocked() const { return _locked; }
    explicit operator bool() const { return _locked; }
    bool isDeniedByBudget() const { return _deniedByBudget; }
    bool isContended() const { return !_locked && !_deniedByBudget; }

    ScopedTlsHandshakeLock(const ScopedTlsHandshakeLock&) = delete;
    ScopedTlsHandshakeLock& operator=(const ScopedTlsHandshakeLock&) = delete;

private:
    bool _locked;
    bool _deniedByBudget;
};

/**
 * @brief Architectural gate for plain HTTP connections.
 * Plain HTTP does not allocate mbedTLS context, consuming only ~1.5 KB DRAM.
 */
inline bool acquireHttp() {
    return true;
}

/**
 * @brief Releases plain HTTP connection reservation.
 */
inline void releaseHttp() {
}

} // namespace NetworkBudget
