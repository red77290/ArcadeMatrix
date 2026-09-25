/**
 * @file Hub75BulkEncoder.h
 * @brief High-performance decoupled RGB565 to HUB75 BCM bitplane encoder.
 *
 * Provides a pure, mathematical conversion from a linear RGB565 canvas into
 * multiplexed, packed HUB75 BCM DMA bitplanes. Decoupled from physical display drivers
 * via a row-accessor callback to permit off-target host testing.
 */
#pragma once
#include <Arduino.h>

#if defined(ESP32_THE_ORIG) || ((defined(CONFIG_IDF_TARGET_ESP32) || defined(HARDWARE_PROFILE_ESP32_DEV)) && !defined(CONFIG_IDF_TARGET_ESP32S3))
#define MATRIX_TX_ADJUST(x_coord) (((x_coord) & 1U) ? ((x_coord) - 1) : ((x_coord) + 1))
#else
#define MATRIX_TX_ADJUST(x_coord) (x_coord)
#endif

#ifndef BITMASK_RGB12_CLEAR
#define BITMASK_RGB12_CLEAR 0b1111111111000000
#endif

struct Hub75EncodingParams {
    uint8_t colorDepth = 8;          ///< 1 to 8 BCM bitplanes
    uint8_t rowsPerFrame = 16;       ///< height / 2 (for 1/16 scan = 16, 1/32 scan = 32)
    uint16_t width = 64;             ///< Pixels per row
    uint16_t height = 32;            ///< Physical height
    const uint8_t* lutR = nullptr;   ///< 32-byte CIE 1931 gamma LUT for Red (5-bit)
    const uint8_t* lutG = nullptr;   ///< 64-byte CIE 1931 gamma LUT for Green (6-bit)
    const uint8_t* lutB = nullptr;   ///< 32-byte CIE 1931 gamma LUT for Blue (5-bit)
    uint8_t rotation = 0;            ///< Physical orientation (0 or 2 for 180 deg)
};

class Hub75BulkEncoder {
public:
    /**
     * @brief Row accessor callback to obtain a pointer to the DMA line buffer for row y and bitplane p.
     */
    using DmaRowAccessor = uint16_t* (*)(void* userCtx, uint8_t row, uint8_t plane);

    /**
     * @brief Encodes an RGB565 linear canvas into packed HUB75 BCM bitplanes.
     *
     * @param canvas Pointer to linear RGB565 pixel data
     * @param canvasStridePixels Row stride in pixels (typically width)
     * @param rowAccessor Callback to obtain target DMA row buffer pointer
     * @param accessorCtx User context passed to rowAccessor
     * @param params Hardware encoding parameters (LUTs, depth, geometry)
     * @return uint32_t Duration of encoding in microseconds
     */
    static uint32_t encode(
        const uint16_t* canvas,
        size_t canvasStridePixels,
        DmaRowAccessor rowAccessor,
        void* accessorCtx,
        const Hub75EncodingParams& params
    );

    /**
     * @brief Generates calibrated CIE 1931 gamma correction tables for 5-bit and 6-bit source channels.
     *
     * @param depth Color depth (number of BCM bitplanes, 1..8)
     * @param lutR Output array for Red (32 entries)
     * @param lutG Output array for Green (64 entries)
     * @param lutB Output array for Blue (32 entries)
     */
    static void generateLuts(uint8_t depth, uint8_t* lutR, uint8_t* lutG, uint8_t* lutB);
};
