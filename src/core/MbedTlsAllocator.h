#pragma once
#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

/**
 * @struct MbedTlsAllocHeader
 * @brief Memory header prepended to all mbedTLS dynamic allocations.
 *
 * Sized to 16 bytes with 8-byte alignment (alignas(8)) to ensure the payload
 * returned to mbedTLS maintains natural 8-byte alignment for 64-bit types.
 */
struct alignas(8) MbedTlsAllocHeader {
    uint32_t magic;       ///< 0x544C534D ("TLSM")
    uint32_t size;        ///< Payload size in bytes
    uint8_t  region;      ///< 1 = PSRAM (SPIRAM), 0 = INTERNAL DRAM
    uint8_t  reserved[7]; ///< Padding ensuring exact 16-byte header size
};

/**
 * @struct MbedTlsMemoryTelemetry
 * @brief Lock-free atomic accounting of mbedTLS heap usage.
 */
struct MbedTlsMemoryTelemetry {
    std::atomic<uint32_t> allocCount{0};
    std::atomic<uint32_t> freeCount{0};
    std::atomic<uint32_t> psramBytesCurrent{0};
    std::atomic<uint32_t> internalBytesCurrent{0};
    std::atomic<uint32_t> fallbackCount{0};
    std::atomic<uint32_t> peakPsramBytes{0};
};

extern MbedTlsMemoryTelemetry g_mbedTlsTelemetry;

/**
 * @brief Registers the PSRAM-first mbedTLS allocator hook.
 * Must be called early in boot (HardwareHAL::begin()) before any TLS socket opens.
 */
void initMbedTlsPsramAllocator();
