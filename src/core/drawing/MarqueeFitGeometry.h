#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace core::drawing {

enum class MarqueeFitMode : uint8_t {
    Fit = 0,
    Stretch = 1,
    Center = 2,
};

struct MarqueeFitPlacement {
    int srcW = 128;
    int srcH = 32;
    int dstW = 128;
    int dstH = 32;
    int offX = 0;
    int offY = 0;
    int intScaleX = 1;
    int intScaleY = 1;
    bool isIntegerScale = true;
};

inline bool asciiEqualsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

inline MarqueeFitMode parseMarqueeFitMode(const char* rawMode) {
    if (asciiEqualsIgnoreCase(rawMode, "stretch")) {
        return MarqueeFitMode::Stretch;
    }
    if (asciiEqualsIgnoreCase(rawMode, "center")) {
        return MarqueeFitMode::Center;
    }
    return MarqueeFitMode::Fit;
}

/**
 * @brief Computes exact proportional or 1:1 marquee placement on `panelW x panelH`.
 *
 * Unlike integer-only `max(1, min(mw/srcW, mh/srcH))` scaling:
 * - Non-integer aspect ratios (e.g. `132x28`, `177x32`, `86x26` on `256x64`) scale cleanly up to
 *   the panel's limiting axis instead of floating tiny at `1x`.
 * - Oversized marquees (e.g. `305x92` `final_burn_alpha.png`, `200x200` on `256x64`, or `256x64`
 *   on `128x32`) scale proportionally down into `panelW x panelH` instead of cropping with negative offsets.
 * - Integer multiples (e.g. `128x32` -> `256x64`, `64x32` -> `128x64`) set `isIntegerScale = true`
 *   so scanline decoders retain their zero-division integer fast path.
 */
inline MarqueeFitPlacement computeMarqueeFit(
    int srcW,
    int srcH,
    int panelW,
    int panelH,
    MarqueeFitMode mode = MarqueeFitMode::Fit
) {
    MarqueeFitPlacement out;
    if (srcW <= 0) srcW = 128;
    if (srcH <= 0) srcH = 32;
    if (panelW <= 0) panelW = 128;
    if (panelH <= 0) panelH = 32;

    out.srcW = srcW;
    out.srcH = srcH;

    if (mode == MarqueeFitMode::Stretch) {
        out.dstW = panelW;
        out.dstH = panelH;
    } else if (mode == MarqueeFitMode::Center) {
        out.dstW = srcW;
        out.dstH = srcH;
    } else {
        const int64_t lhs = static_cast<int64_t>(panelW) * static_cast<int64_t>(srcH);
        const int64_t rhs = static_cast<int64_t>(panelH) * static_cast<int64_t>(srcW);
        if (lhs <= rhs) {
            out.dstW = panelW;
            int scaledH = static_cast<int>(
                (static_cast<int64_t>(srcH) * static_cast<int64_t>(panelW) + srcW / 2) / srcW
            );
            if (scaledH < 1) scaledH = 1;
            if (scaledH > panelH) scaledH = panelH;
            out.dstH = scaledH;
        } else {
            out.dstH = panelH;
            int scaledW = static_cast<int>(
                (static_cast<int64_t>(srcW) * static_cast<int64_t>(panelH) + srcH / 2) / srcH
            );
            if (scaledW < 1) scaledW = 1;
            if (scaledW > panelW) scaledW = panelW;
            out.dstW = scaledW;
        }
    }

    out.offX = (panelW - out.dstW) / 2;
    out.offY = (panelH - out.dstH) / 2;

    if (out.dstW >= srcW && out.dstH >= srcH &&
        (out.dstW % srcW) == 0 && (out.dstH % srcH) == 0) {
        out.isIntegerScale = true;
        out.intScaleX = out.dstW / srcW;
        out.intScaleY = out.dstH / srcH;
    } else {
        out.isIntegerScale = false;
        out.intScaleX = 1;
        out.intScaleY = 1;
    }
    return out;
}

/**
 * @brief Maps source pixel coordinate `idx` (`0 <= idx < srcDim`) to gap-free destination span
 *        `[outStart, outEnd)` for both upscaling (`dstDim >= srcDim`) and downscaling (`dstDim < srcDim`).
 * @return true if `[outStart, outEnd)` is non-empty (`outEnd > outStart`).
 */
inline bool mapSourceSpanToDest(
    int idx,
    int srcDim,
    int dstDim,
    int off,
    int& outStart,
    int& outEnd
) {
    if (idx < 0 || idx >= srcDim || srcDim <= 0 || dstDim <= 0) {
        outStart = off;
        outEnd = off;
        return false;
    }
    outStart = off + static_cast<int>((static_cast<int64_t>(idx) * dstDim) / srcDim);
    outEnd = off + static_cast<int>((static_cast<int64_t>(idx + 1) * dstDim) / srcDim);
    return outEnd > outStart;
}

/**
 * @brief Recenters visibly non-black content on a decoded RGB565 canvas in-place (`0 B` heap).
 *
 * Many community Pixelcade `.png` logos have asymmetric black/transparent margins inside their
 * `128x32` frame (e.g. `L=0, R=8` or `T=10, B=0` once scaled to `256x64`). Shifting the visible
 * bounding box to the exact geometric center keeps full-bleed art untouched (`dx=0, dy=0`) while
 * centering off-axis logos to single-pixel accuracy.
 */
inline void recenterRgb565CanvasBounds(
    uint16_t* canvas,
    int width,
    int height,
    int* outShiftX = nullptr,
    int* outShiftY = nullptr
) {
    if (outShiftX) *outShiftX = 0;
    if (outShiftY) *outShiftY = 0;
    if (!canvas || width <= 0 || height <= 0) return;

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < height; ++y) {
        const uint16_t* row = canvas + static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            const uint16_t px = row[x];
            if (px == 0U) continue;
            // Ignore sub-threshold dark noise (`R5<=1, G6<=2, B5<=1` maps to 0 PWM under CIE gamma)
            const uint16_t r5 = (px >> 11) & 0x1FU;
            const uint16_t g6 = (px >> 5) & 0x3FU;
            const uint16_t b5 = px & 0x1FU;
            if (r5 > 1U || g6 > 2U || b5 > 1U) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return;
    }

    const int boxW = maxX - minX + 1;
    const int boxH = maxY - minY + 1;
    const int dx = ((width - boxW) / 2) - minX;
    const int dy = ((height - boxH) / 2) - minY;
    if (outShiftX) *outShiftX = dx;
    if (outShiftY) *outShiftY = dy;

    if (dy > 0 && dy < height) {
        for (int y = height - 1; y >= dy; --y) {
            std::memmove(
                canvas + static_cast<size_t>(y) * static_cast<size_t>(width),
                canvas + static_cast<size_t>(y - dy) * static_cast<size_t>(width),
                static_cast<size_t>(width) * sizeof(uint16_t)
            );
        }
        std::memset(canvas, 0, static_cast<size_t>(dy) * static_cast<size_t>(width) * sizeof(uint16_t));
    } else if (dy < 0 && (-dy) < height) {
        const int sh = -dy;
        for (int y = 0; y < height - sh; ++y) {
            std::memmove(
                canvas + static_cast<size_t>(y) * static_cast<size_t>(width),
                canvas + static_cast<size_t>(y + sh) * static_cast<size_t>(width),
                static_cast<size_t>(width) * sizeof(uint16_t)
            );
        }
        std::memset(
            canvas + static_cast<size_t>(height - sh) * static_cast<size_t>(width),
            0,
            static_cast<size_t>(sh) * static_cast<size_t>(width) * sizeof(uint16_t)
        );
    }

    if (dx > 0 && dx < width) {
        for (int y = 0; y < height; ++y) {
            uint16_t* row = canvas + static_cast<size_t>(y) * static_cast<size_t>(width);
            std::memmove(row + dx, row, static_cast<size_t>(width - dx) * sizeof(uint16_t));
            std::memset(row, 0, static_cast<size_t>(dx) * sizeof(uint16_t));
        }
    } else if (dx < 0 && (-dx) < width) {
        const int sh = -dx;
        for (int y = 0; y < height; ++y) {
            uint16_t* row = canvas + static_cast<size_t>(y) * static_cast<size_t>(width);
            std::memmove(row, row + sh, static_cast<size_t>(width - sh) * sizeof(uint16_t));
            std::memset(row + (width - sh), 0, static_cast<size_t>(sh) * sizeof(uint16_t));
        }
    }
}

/**
 * @brief Computes Adafruit_GFX cursor `(cursorX, cursorY)` so a glyph bounding box
 *        `(boundsX1, boundsY1, textW, textH)` returned by `getTextBounds(str, 0, 0, ...)`
 *        is visually centered on `surfaceW x surfaceH`.
 */
inline void computeTextVisualCenter(
    int surfaceW,
    int surfaceH,
    int boundsX1,
    int boundsY1,
    int textW,
    int textH,
    int offsetY,
    int& outCursorX,
    int& outCursorY
) {
    outCursorX = ((surfaceW - textW) / 2) - boundsX1;
    outCursorY = ((surfaceH - textH) / 2) - boundsY1 + offsetY;
}

} // namespace core::drawing
