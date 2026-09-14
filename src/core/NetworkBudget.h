#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <atomic>

/**
 * @file NetworkBudget.h
 * @brief Internal DRAM admission control for TLS sessions.
 *
 * Every TLS handshake performed through WiFiClientSecure/mbedTLS issues dozens of
 * small allocations. Because the ESP32-S3 SDK is built with
 * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL = 4096, any allocation of 4 KB or less is
 * served from internal DRAM and can never fall back to PSRAM. Internal DRAM is
 * therefore the scarce resource on this platform, regardless of how much PSRAM is
 * free.
 *
 * When a handshake is attempted below the safe watermark it fails with
 * MBEDTLS_ERR_SSL_ALLOC_FAILED (-0x7F00, reported as -32512) and leaves the heap
 * more fragmented than before. Repeating that for every item of a fetch loop
 * produces the handshake storms observed in the field, and starves concurrent
 * consumers such as the SD/FATFS layer.
 *
 * These helpers let network producers on Core 0 skip a round cleanly instead of
 * hammering a heap that cannot satisfy them.
 */
namespace NetworkBudget {

/// Empirically validated admission threshold for current ESP32-S3 configuration.
/// Note: These are measured admission thresholds, not theoretical TLS memory guarantees.
static constexpr uint32_t TLS_MIN_FREE_INTERNAL = 30u * 1024u; // 30,720 bytes

/// Contiguous allocation watermark to satisfy the 16 KB mbedTLS input record buffer.
static constexpr uint32_t TLS_MIN_LARGEST_BLOCK = 16896u; // 16.5 KB

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
 * @brief Thread-safe telemetry counter tracking rejected TLS attempts.
 */
inline std::atomic<uint32_t>& getTlsDeniedCount() {
    static std::atomic<uint32_t> count{0};
    return count;
}

/**
 * @brief Tells whether a TLS handshake may reasonably be attempted right now.
 *
 * Checks both the total free internal DRAM and the largest contiguous block, since
 * a fragmented heap fails the handshake even when the total looks sufficient.
 *
 * @return true when there is enough internal DRAM headroom for a TLS session.
 */
inline bool canStartTlsSession() {
    const uint32_t free = freeInternal();
    const uint32_t largest = largestInternalBlock();
    const bool admitted = (free >= TLS_MIN_FREE_INTERNAL && largest >= TLS_MIN_LARGEST_BLOCK);
    if (!admitted) {
        getTlsDeniedCount().fetch_add(1, std::memory_order_relaxed);
        static std::atomic<uint32_t> lastDenialLogMs{0};
        uint32_t now = millis();
        uint32_t last = lastDenialLogMs.load(std::memory_order_relaxed);
        if (now - last > 10000 && lastDenialLogMs.compare_exchange_strong(last, now)) {
            log_w("TLS admission denied: free=%u (req %u), largest=%u (req %u), total denied=%u",
                  free, TLS_MIN_FREE_INTERNAL, largest, TLS_MIN_LARGEST_BLOCK,
                  getTlsDeniedCount().load(std::memory_order_relaxed));
        }
    }
    return admitted;
}

} // namespace NetworkBudget
