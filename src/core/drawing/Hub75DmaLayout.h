/**
 * @file Hub75DmaLayout.h
 * @brief Canonical DMA payload accounting and admission overhead model for HUB75 bitplanes.
 */
#pragma once
#include <Arduino.h>
#include "DmaMemoryLayout.h"

class Hub75DmaLayout {
public:
    /**
     * @brief Computes canonical DMA payload and admission overhead accounting.
     * @param width Matrix total width (e.g. 64, 128, 256)
     * @param height Matrix total height (e.g. 32, 64)
     * @param colorDepth Color depth in bits per channel (2..8)
     * @param doubleBuffer Whether hardware double buffering is active
     * @param includeDescriptorOverhead If true, models link descriptor overhead (rows * depth * buffers * 16 bytes)
     * @return Total DMA bytes required
     */
    static inline size_t calculateBytes(uint16_t width, uint16_t height, uint8_t colorDepth, bool doubleBuffer, bool includeDescriptorOverhead = false) {
        size_t payload = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, doubleBuffer);
        if (includeDescriptorOverhead && height > 0) {
            uint16_t rows = height / 2;
            payload += (size_t)rows * colorDepth * (doubleBuffer ? 2 : 1) * 16;
        }
        return payload;
    }
};
