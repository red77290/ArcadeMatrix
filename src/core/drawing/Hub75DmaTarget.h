/**
 * @file Hub75DmaTarget.h
 * @brief Abstract destination descriptor for HUB75 DMA framebuffers.
 *
 * Decouples the mathematical bitplane encoder from any specific library class
 * (e.g. MatrixPanel_I2S_DMA).
 */
#pragma once
#include <Arduino.h>

using DmaRowAccessor = uint16_t* (*)(void* userCtx, uint8_t row, uint8_t plane);

struct Hub75DmaTarget {
    uint8_t* buffer = nullptr;       ///< Pointer to physical DMA buffer plane data
    size_t bufferBytes = 0;          ///< Total allocated buffer bytes
    size_t strideBytes = 0;          ///< Stride between consecutive rows/planes
    uint16_t width = 0;              ///< Physical panel width
    uint16_t height = 0;             ///< Physical panel height
    uint16_t rowsPerFrame = 0;       ///< Number of scan rows per frame (e.g. height / 2 for 1/16 scan)
    uint8_t colorDepth = 0;          ///< Bitplane depth (1 to 8 bits)
    uint8_t activeBufferIndex = 0;   ///< Back-buffer index being drawn (0 or 1)

    DmaRowAccessor rowAccessor = nullptr; ///< Unified row accessor function
    void* accessorCtx = nullptr;          ///< User context for row accessor

    inline uint16_t* getRowPtr(uint8_t row, uint8_t plane) const {
        if (rowAccessor) {
            return rowAccessor(accessorCtx, row, plane);
        }
        if (buffer && plane < colorDepth && row < rowsPerFrame) {
            return reinterpret_cast<uint16_t*>(buffer + (plane * rowsPerFrame + row) * strideBytes);
        }
        return nullptr;
    }

    inline uint8_t* plane(uint8_t planeIndex) const {
        if (buffer && planeIndex < colorDepth) {
            return buffer + (planeIndex * strideBytes);
        }
        return reinterpret_cast<uint8_t*>(getRowPtr(0, planeIndex));
    }
};
