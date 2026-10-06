/**
 * @file DmaMemoryLayout.h
 * @brief Canonical mathematical DMA memory accounting for HUB75 bitplanes.
 */
#pragma once
#include <Arduino.h>

/**
 * @struct DmaMemoryLayout
 * @brief Computes exact hardware DMA buffer dimensions and byte requirements.
 */
struct DmaMemoryLayout {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t colorDepth = 8;
    bool doubleBuffer = false;

    constexpr DmaMemoryLayout(uint16_t w, uint16_t h, uint8_t depth, bool dbl)
        : width(w), height(h), colorDepth(depth), doubleBuffer(dbl) {}

    /// Number of multiplexed scanline rows per frame (height / 2 for standard 1/16 and 1/32 panels)
    inline size_t rows() const { return height / 2; }

    /// Number of BCM bitplanes per row
    inline size_t planes() const { return colorDepth; }

    /// Stride of a single row plane in bytes (width * sizeof(uint16_t))
    inline size_t stride() const { return (size_t)width * sizeof(uint16_t); }

    /// Total bytes required to store all bitplanes for a single multiplexed row
    inline size_t bytesPerRow() const { return planes() * stride(); }

    /// Total bytes required for a single full frame buffer
    inline size_t bytesPerBuffer() const { return rows() * bytesPerRow(); }

    /// Total DMA memory required across active buffer count (single vs double buffering)
    inline size_t totalBytes() const { return doubleBuffer ? (bytesPerBuffer() * 2) : bytesPerBuffer(); }

    /**
     * @brief Static helper to calculate total DMA bytes required.
     */
    static inline size_t calculateTotalBytes(uint16_t w, uint16_t h, uint8_t depth, bool dbl) {
        return DmaMemoryLayout(w, h, depth, dbl).totalBytes();
    }
};
