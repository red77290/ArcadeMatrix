/**
 * @file PresentationTimingModel.h
 * @brief Physics-based timing model for HUB75 bitplane encoding and DMA transfer estimation.
 */
#pragma once
#include <Arduino.h>

/**
 * @struct PresentationTimingModel
 * @brief Computes realistic worst-case and nominal execution durations based on panel dimensions and depth.
 */
struct PresentationTimingModel {
    /**
     * @brief Estimates bitplane encoding duration on 240MHz Xtensa CPU.
     * @param width Matrix physical width in pixels
     * @param height Matrix physical height in pixels
     * @param depth Color depth in bits per channel (2 to 8)
     * @return Estimated encoding duration in microseconds
     */
    static inline uint32_t estimateEncodeUs(uint16_t width, uint16_t height, uint8_t depth) {
        // Xtensa LX6/LX7 at 240MHz packs ~50 million bitplane pixel ops per second (~20ns / pixel-plane)
        uint32_t totalBitplanePixels = (uint32_t)width * height * depth;
        uint32_t est = (totalBitplanePixels * 20) / 1000;
        return est < 15 ? 15 : est;
    }

    /**
     * @brief Estimates transfer / critical presentation section duration.
     * @param width Matrix physical width in pixels
     * @param height Matrix physical height in pixels
     * @param depth Color depth in bits per channel
     * @param isSingleBuffer Whether single buffer mode is active
     * @param isSpiramDma Whether DMA buffer resides in external SPIRAM
     * @return Estimated critical transfer duration in microseconds
     */
    static inline uint32_t estimateTransferUs(uint16_t width, uint16_t height, uint8_t depth,
                                              bool isSingleBuffer, bool isSpiramDma = false) {
        if (!isSingleBuffer) {
            // Double buffer mode: transfer is an instantaneous hardware DMA descriptor pointer flip
            return 25;
        }

        // Single buffer mode: the critical section spans safe window -> blank -> encode -> writeback -> unblank
        uint32_t encodeUs = estimateEncodeUs(width, height, depth);
        // SPIRAM cache writeback rate ~40 MB/s (25ns per byte)
        uint32_t dmaBytes = (uint32_t)width * (height / 2) * depth * sizeof(uint16_t);
        uint32_t writebackUs = isSpiramDma ? (dmaBytes / 40) : 0;
        return encodeUs + writebackUs + 25;
    }
};
