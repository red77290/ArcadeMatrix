#include "MbedTlsAllocator.h"
#include "Logger.h"

MbedTlsMemoryTelemetry g_mbedTlsTelemetry;

static constexpr uint32_t MBEDTLS_ALLOC_MAGIC = 0x544C534D; // "TLSM"

static void* mbedtls_psram_calloc(size_t n, size_t size) {
    const size_t payloadSize = n * size;
    const size_t totalAlloc = sizeof(MbedTlsAllocHeader) + payloadSize;

    void* raw = nullptr;
    uint8_t region = 0;

    if (psramFound()) {
        raw = heap_caps_calloc(1, totalAlloc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (raw) {
            region = 1;
        }
    }

    if (!raw) {
        raw = heap_caps_calloc(1, totalAlloc, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!raw) {
            LOGE("mbedTLS", "CRITICAL: mbedtls_calloc failed! (requested %u bytes)", (unsigned)totalAlloc);
            return nullptr;
        }
        region = 0;
        if (psramFound()) {
            g_mbedTlsTelemetry.fallbackCount.fetch_add(1, std::memory_order_relaxed);
            LOGW("mbedTLS", "PSRAM allocation failed, fell back to internal DRAM (%u bytes)", (unsigned)payloadSize);
        }
    }

    auto* hdr = static_cast<MbedTlsAllocHeader*>(raw);
    hdr->magic = MBEDTLS_ALLOC_MAGIC;
    hdr->size = static_cast<uint32_t>(payloadSize);
    hdr->region = region;

    g_mbedTlsTelemetry.allocCount.fetch_add(1, std::memory_order_relaxed);

    if (region == 1) {
        uint32_t cur = g_mbedTlsTelemetry.psramBytesCurrent.fetch_add(payloadSize, std::memory_order_relaxed) + payloadSize;
        uint32_t peak = g_mbedTlsTelemetry.peakPsramBytes.load(std::memory_order_relaxed);
        while (cur > peak && !g_mbedTlsTelemetry.peakPsramBytes.compare_exchange_weak(peak, cur, std::memory_order_relaxed));
    } else {
        g_mbedTlsTelemetry.internalBytesCurrent.fetch_add(payloadSize, std::memory_order_relaxed);
    }

    return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(raw) + sizeof(MbedTlsAllocHeader));
}

static void mbedtls_psram_free(void* ptr) {
    if (!ptr) return;

    auto* hdr = reinterpret_cast<MbedTlsAllocHeader*>(static_cast<uint8_t*>(ptr) - sizeof(MbedTlsAllocHeader));
    if (hdr->magic == MBEDTLS_ALLOC_MAGIC) {
        uint32_t sz = hdr->size;
        uint8_t region = hdr->region;
        g_mbedTlsTelemetry.freeCount.fetch_add(1, std::memory_order_relaxed);

        if (region == 1) {
            g_mbedTlsTelemetry.psramBytesCurrent.fetch_sub(sz, std::memory_order_relaxed);
        } else {
            g_mbedTlsTelemetry.internalBytesCurrent.fetch_sub(sz, std::memory_order_relaxed);
        }

        hdr->magic = 0; // Poison header to detect double-free
        heap_caps_free(hdr);
    } else {
        // Fallback for pointers not allocated through our wrapper
        heap_caps_free(ptr);
    }
}

void initMbedTlsPsramAllocator() {
    int ret = mbedtls_platform_set_calloc_free(mbedtls_psram_calloc, mbedtls_psram_free);
    if (ret == 0) {
        LOGI("mbedTLS", "PSRAM-first allocator registered successfully (PSRAM available: %s).",
             psramFound() ? "YES" : "NO");
    } else {
        LOGE("mbedTLS", "Failed to register PSRAM-first allocator (error: %d)", ret);
    }
}
