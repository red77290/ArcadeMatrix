/**
 * @file Hub75DmaTarget.h
 * @brief Abstract destination descriptor for HUB75 DMA framebuffers.
 *
 * Decouples the mathematical bitplane encoder from any specific library class
 * (e.g. MatrixPanel_I2S_DMA).
 */
#pragma once
#include <Arduino.h>

struct Hub75DmaTarget {
    uint8_t* buffer = nullptr;       ///< Pointer to physical DMA buffer plane data
    size_t bufferBytes = 0;          ///< Total allocated buffer bytes
    size_t strideBytes = 0;          ///< Stride between consecutive rows/planes
    uint16_t width = 0;              ///< Physical panel width
    uint16_t height = 0;             ///< Physical panel height
    uint16_t rowsPerFrame = 0;       ///< Number of scan rows per frame (e.g. height / 2 for 1/16 scan)
    uint8_t colorDepth = 0;          ///< Bitplane depth (1 to 8 bits)
    uint8_t activeBufferIndex = 0;   ///< Back-buffer index being drawn (0 or 1)

    inline uint8_t* plane(uint8_t planeIndex) const {
        if (!buffer || planeIndex >= colorDepth) return nullptr;
        return buffer + (planeIndex * strideBytes);
    }
};
