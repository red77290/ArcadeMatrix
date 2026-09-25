/**
 * @file IPresentationBackend.h
 * @brief Abstract interface governing DMA presentation and timing policies.
 */
#pragma once
#include <Arduino.h>
#include "Hub75DmaTarget.h"
#include "IDrawingSurface.h"

struct PresentationPolicy {
    uint32_t maxBlankUs = 400;   ///< Maximum allowable transient OE blanking duration (default 400µs)
    uint32_t maxFrameUs = 16667; ///< Maximum frame period budget (16.6ms for 60 FPS)
    bool allowBlanking = true;   ///< Whether transient blanking is permitted
};

class IPresentationSynchronizer {
public:
    virtual ~IPresentationSynchronizer() = default;
    /**
     * @brief Waits for a safe presentation window (e.g. V-Blank or scanline pause).
     * @param timeoutUs Maximum time in microseconds to wait
     * @return true if safe window acquired, false on timeout
     */
    virtual bool waitForSafeWindow(uint32_t timeoutUs) = 0;
};

class IPresentationBackend {
public:
    virtual ~IPresentationBackend() = default;

    /**
     * @brief Prepares and returns the physical DMA target descriptor for encoding.
     */
    virtual Hub75DmaTarget acquireDmaTarget() = 0;

    /**
     * @brief Commits the packed buffer to the display hardware adhering to timing policy.
     * @param policy Timing constraints (blanking limits, frame deadline)
     * @return PresentationTiming Live performance telemetry
     */
    virtual PresentationTiming commit(const PresentationPolicy& policy) = 0;

    /**
     * @brief Calculates exact DMA RAM bytes required for the active configuration.
     */
    virtual size_t calculateDmaBytes() const = 0;

    /**
     * @brief Returns active synchronizer implementation (or nullptr if unavailable).
     */
    virtual IPresentationSynchronizer* getSynchronizer() { return nullptr; }
};
