/**
 * @file MemoryManager.h
 * @brief Centralized capability-accurate memory manager and telemetry oracle for ArcadeMatrix.
 */
#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <atomic>

#if defined(ESP32)
#include "esp_heap_caps.h"
#endif

/**
 * @struct MemoryRequirements
 * @brief Formalized memory request specification incorporating capabilities and hardware alignment.
 */
struct MemoryRequirements {
    size_t bytes = 0;
    uint32_t capabilities = 0; // MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM
    size_t alignment = 4;
};

/**
 * @enum MemoryPressureLevel
 * @brief Discrete resource pressure level signaled across cores.
 */
enum class MemoryPressureLevel : uint8_t {
    Nominal = 0,   ///< Ample internal DRAM and DMA headroom
    Moderate = 1,  ///< Elevated heap pressure (transient buffers should be pruned)
    Critical = 2   ///< Severe fragmentation / low watermark (degrade to static/minimal display)
};

/**
 * @struct DomainMetrics
 * @brief Empirical metrics for a specific memory capability domain.
 */
struct DomainMetrics {
    size_t freeBytes = 0;
    size_t largestBlock = 0;
    size_t minWatermark = 0;
    uint8_t fragmentationPct = 0; ///< 100 - (largestBlock * 100 / freeBytes)
};

/**
 * @struct MemorySnapshot
 * @brief Real-time snapshot of system memory capabilities.
 *
 * NOTE: 'internal' (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) and 'dma' (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)
 * are capability-filtered views of the underlying heap, NOT disjoint memory pools. They must never be summed.
 */
struct MemorySnapshot {
    DomainMetrics internal;
    DomainMetrics dma;
    DomainMetrics psram;
    MemoryPressureLevel pressureLevel = MemoryPressureLevel::Nominal;
    bool hasPsram = false;
};

class MemoryManager {
public:
    static MemoryManager& instance();

    /**
     * @brief Captures a lock-free, zero-allocation snapshot of memory state.
     */
    MemorySnapshot captureSnapshot() const;

    /**
     * @brief Preflight admission check verifying whether an allocation can be accommodated.
     * @note This is a predictive check (observation), NOT a physical reservation guarantee.
     */
    bool canAllocate(const MemoryRequirements& req) const;

    /**
     * @brief Computes a system health score [0..100] based on DRAM headroom and fragmentation.
     */
    uint8_t computeHealthScore() const;

    /**
     * @brief Asynchronously posts memory pressure notification from Core 0.
     */
    inline void notifyPressure(MemoryPressureLevel level) {
        _pendingPressure.store(static_cast<uint8_t>(level), std::memory_order_release);
    }

    /**
     * @brief Consumes pending memory pressure notification on Core 1 without locks or allocations.
     */
    inline MemoryPressureLevel consumePendingPressure() {
        uint8_t raw = _pendingPressure.exchange(static_cast<uint8_t>(MemoryPressureLevel::Nominal), std::memory_order_acq_rel);
        return static_cast<MemoryPressureLevel>(raw);
    }

private:
    MemoryManager() = default;
    std::atomic<uint8_t> _pendingPressure{0};
};
