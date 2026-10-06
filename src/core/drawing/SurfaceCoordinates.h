/**
 * @file SurfaceCoordinates.h
 * @brief Centralized logical-to-physical coordinate transformation.
 *
 * Ensures mathematical equivalence between drawPixel() primitives and
 * block blit565() transfers under non-square panel geometries and all 4 rotation states.
 */
#pragma once
#include <Arduino.h>

struct Point {
    int16_t x;
    int16_t y;
};

class SurfaceCoordinates {
public:
    /**
     * @brief Maps logical coordinates to physical matrix coordinates.
     *
     * @param x Logical X coordinate (as provided by display engine)
     * @param y Logical Y coordinate (as provided by display engine)
     * @param physicalWidth Invariant physical matrix width (e.g. 64, 128, 256)
     * @param physicalHeight Invariant physical matrix height (e.g. 32, 64)
     * @param rotation Active display rotation (0..3)
     * @return Point Physical coordinates mapped into raw display frame buffer
     */
    static inline Point logicalToPhysical(
        int16_t x,
        int16_t y,
        int16_t physicalWidth,
        int16_t physicalHeight,
        uint8_t rotation
    ) {
        switch (rotation & 3) {
            case 1: // 90 deg clockwise (logical width is physicalHeight, logical height is physicalWidth)
                return Point{
                    static_cast<int16_t>(physicalWidth - 1 - y),
                    x
                };
            case 2: // 180 deg
                return Point{
                    static_cast<int16_t>(physicalWidth - 1 - x),
                    static_cast<int16_t>(physicalHeight - 1 - y)
                };
            case 3: // 270 deg clockwise
                return Point{
                    y,
                    static_cast<int16_t>(physicalHeight - 1 - x)
                };
            default: // 0 deg normal
                return Point{ x, y };
        }
    }

    /**
     * @brief Computes active logical dimensions from physical dimensions and rotation.
     */
    static inline void getLogicalDimensions(
        int16_t physicalWidth,
        int16_t physicalHeight,
        uint8_t rotation,
        int16_t& outLogicalWidth,
        int16_t& outLogicalHeight
    ) {
        if ((rotation & 1) != 0) {
            outLogicalWidth = physicalHeight;
            outLogicalHeight = physicalWidth;
        } else {
            outLogicalWidth = physicalWidth;
            outLogicalHeight = physicalHeight;
        }
    }
};
