#pragma once
#include <Arduino.h>
#include "core/EngineContract.h"

/**
 * @namespace LayoutHelper
 * @brief Pure stateless geometry functions for responsive display partitioning.
 */
namespace LayoutHelper {

    /**
     * @brief Strictly crops a rectangle to screen bounds without shifting its origin.
     */
    inline Rect intersectScreen(const Rect& r, uint16_t screenW, uint16_t screenH) {
        int32_t x1 = max((int32_t)0, (int32_t)r.x);
        int32_t y1 = max((int32_t)0, (int32_t)r.y);
        int32_t x2 = min((int32_t)screenW, (int32_t)r.x + (int32_t)r.width);
        int32_t y2 = min((int32_t)screenH, (int32_t)r.y + (int32_t)r.height);

        if (x2 <= x1 || y2 <= y1) {
            return Rect{ 0, 0, 0, 0 };
        }
        return Rect{ (int16_t)x1, (int16_t)y1, (uint16_t)(x2 - x1), (uint16_t)(y2 - y1) };
    }

    /**
     * @brief Translates a rectangle position to fit entirely inside screen bounds if possible.
     */
    inline Rect clampPosition(const Rect& r, uint16_t screenW, uint16_t screenH) {
        int16_t clampedW = min((uint16_t)r.width, screenW);
        int16_t clampedH = min((uint16_t)r.height, screenH);
        int16_t clampedX = max((int16_t)0, min((int16_t)r.x, (int16_t)(screenW - clampedW)));
        int16_t clampedY = max((int16_t)0, min((int16_t)r.y, (int16_t)(screenH - clampedH)));
        return Rect{ clampedX, clampedY, (uint16_t)clampedW, (uint16_t)clampedH };
    }

    /**
     * @brief Computes aspect-ratio preserved dimensions (Letterbox / Pillarbox) centered in bounds.
     */
    inline Rect aspectFit(uint16_t srcW, uint16_t srcH, const Rect& bounds) {
        if (srcW == 0 || srcH == 0 || bounds.width == 0 || bounds.height == 0) {
            return bounds;
        }

        float scaleX = (float)bounds.width / (float)srcW;
        float scaleY = (float)bounds.height / (float)srcH;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;

        uint16_t outW = max((uint16_t)1, (uint16_t)(srcW * scale));
        uint16_t outH = max((uint16_t)1, (uint16_t)(srcH * scale));
        int16_t outX = bounds.x + (int16_t)((bounds.width - outW) / 2);
        int16_t outY = bounds.y + (int16_t)((bounds.height - outH) / 2);

        return Rect{ outX, outY, outW, outH };
    }

    /**
     * @brief Slices the top `h` pixels of `src`.
     */
    inline Rect splitTop(const Rect& src, uint16_t h) {
        uint16_t sliceH = min(src.height, h);
        return Rect{ src.x, src.y, src.width, sliceH };
    }

    /**
     * @brief Slices the bottom `h` pixels of `src`.
     */
    inline Rect splitBottom(const Rect& src, uint16_t h) {
        uint16_t sliceH = min(src.height, h);
        return Rect{ src.x, (int16_t)(src.y + src.height - sliceH), src.width, sliceH };
    }

    /**
     * @brief Slices the left `w` pixels of `src`.
     */
    inline Rect splitLeft(const Rect& src, uint16_t w) {
        uint16_t sliceW = min(src.width, w);
        return Rect{ src.x, src.y, sliceW, src.height };
    }

    /**
     * @brief Slices the right `w` pixels of `src`.
     */
    inline Rect splitRight(const Rect& src, uint16_t w) {
        uint16_t sliceW = min(src.width, w);
        return Rect{ (int16_t)(src.x + src.width - sliceW), src.y, sliceW, src.height };
    }
}

/**
 * @struct GifPlaylistSelection
 * @brief Encapsulates primary and fallback storage directories for animated GIFs.
 */
struct GifPlaylistSelection {
    const char* primaryPath;
    const char* fallbackPath;
};

/**
 * @class GifSourceSelector
 * @brief Pure selector determining the appropriate GIF asset paths from DisplayGeometry.
 */
class GifSourceSelector {
public:
    static inline GifPlaylistSelection select(const DisplayGeometry& geometry) {
        if (geometry.layoutClass == LayoutClass::PORTRAIT || geometry.layoutClass == LayoutClass::TALL) {
            return { "/gifs_tate", "/gifs" };
        }
        // WIDE and SQUARE default to /gifs with fallback to /gifs_tate
        return { "/gifs", "/gifs_tate" };
    }
};
