#pragma once

#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>

/**
 * @file NetworkBudget.h
 * @brief Internal DRAM admission control for TLS sessions.
 *
 * Provides admission thresholds and telemetry for TLS sessions and HTTP transactions.
 * While mbedTLS dynamic allocations are routed to PSRAM via the PSRAM-first allocator,
 * network transport buffers (lwIP PCBs, socket tables, AsyncTCP buffers) still require
 * internal DRAM.
 *
 * Safety boundaries:
 * - Hard safety limit: freeInternal >= 30 KB, largestInternalBlock >= 16896 bytes.
 * - Healthy target under nominal streaming: freeInternal >= 50 KB.
 */
namespace NetworkBudget {

/// Hard safety admission threshold for internal DRAM.
static constexpr uint32_t TLS_MIN_FREE_INTERNAL = 30u * 1024u; // 30,720 bytes

/// Contiguous allocation watermark to satisfy the 16 KB mbedTLS input record buffer.
static constexpr uint32_t TLS_MIN_LARGEST_BLOCK = 16896u; // 16.5 KB

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
 * Checks both the total free internal DRAM, largest contiguous block, and
 * DMA-capable heap for hardware SHA acceleration (esp-sha buffer allocation).
 *
 * @return true when there is enough internal DRAM and DMA headroom for a TLS session.
 */
inline bool canStartTlsSession() {
    const uint32_t free = freeInternal();
    const uint32_t largest = largestInternalBlock();
    const uint32_t freeDma = freeDmaInternal();
    const uint32_t largestDma = largestDmaInternalBlock();
    const bool admitted = (free >= TLS_MIN_FREE_INTERNAL && largest >= TLS_MIN_LARGEST_BLOCK &&
                           freeDma >= TLS_MIN_FREE_DMA && largestDma >= TLS_MIN_LARGEST_DMA_BLOCK);
    if (!admitted) {
        getTlsDeniedCount().fetch_add(1, std::memory_order_relaxed);
        static std::atomic<uint32_t> lastDenialLogMs{0};
        uint32_t now = millis();
        uint32_t last = lastDenialLogMs.load(std::memory_order_relaxed);
        if (now - last > 10000 && lastDenialLogMs.compare_exchange_strong(last, now)) {
            log_w("TLS admission denied: free=%u (req %u), largest=%u (req %u), freeDma=%u (req %u), largestDma=%u (req %u), total denied=%u",
                  free, TLS_MIN_FREE_INTERNAL, largest, TLS_MIN_LARGEST_BLOCK,
                  freeDma, TLS_MIN_FREE_DMA, largestDma, TLS_MIN_LARGEST_DMA_BLOCK,
                  getTlsDeniedCount().load(std::memory_order_relaxed));
        }
    }
    return admitted;
}

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
