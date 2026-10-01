/**
 * @file Hub75DmaLayout.h
 * @brief Authoritative memory sizing oracle for HUB75 DMA bitplanes.
 */
#pragma once
#include <Arduino.h>
#include "DmaMemoryLayout.h"

class Hub75DmaLayout {
public:
    /**
     * @brief Computes exact hardware DMA buffer dimensions and byte requirements.
     * @param width Matrix total width (e.g. 64, 128, 256)
     * @param height Matrix total height (e.g. 32, 64)
     * @param colorDepth Color depth in bits per channel (2..8)
     * @param doubleBuffer Whether hardware double buffering is active
     * @return Total DMA bytes required
     */
    static inline size_t calculateBytes(uint16_t width, uint16_t height, uint8_t colorDepth, bool doubleBuffer) {
        return DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, doubleBuffer);
    }
};
