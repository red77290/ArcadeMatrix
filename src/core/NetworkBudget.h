#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

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

/// Minimum total free internal DRAM required before starting a TLS session.
///
/// Derived from the peak cost of one session with the Arduino-ESP32 mbedTLS build
/// (CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN): a 16 KB input record buffer, a 4 KB output
/// record buffer, X.509 chain parsing and the handshake/session state add up to roughly
/// 30 KB, on top of whatever the SD/FATFS layer and AsyncWebServer need concurrently.
/// Field logs showed handshakes still failing at ~59 KB free, so the earlier 46 KB
/// watermark was far too optimistic.
static constexpr uint32_t TLS_MIN_FREE_INTERNAL = 56u * 1024u;

/// Minimum contiguous internal DRAM block required before starting a TLS session.
///
/// This is the binding constraint: the 16384-byte input record buffer is a single
/// allocation, so a fragmented heap fails the handshake even when the total free size
/// looks comfortable. 18 KB covers the buffer plus its header overhead.
static constexpr uint32_t TLS_MIN_LARGEST_BLOCK = 18u * 1024u;

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
 * @brief Tells whether a TLS handshake may reasonably be attempted right now.
 *
 * Checks both the total free internal DRAM and the largest contiguous block, since
 * a fragmented heap fails the handshake even when the total looks sufficient.
 *
 * @return true when there is enough internal DRAM headroom for a TLS session.
 */
inline bool canStartTlsSession() {
    return freeInternal() >= TLS_MIN_FREE_INTERNAL &&
           largestInternalBlock() >= TLS_MIN_LARGEST_BLOCK;
}

} // namespace NetworkBudget
