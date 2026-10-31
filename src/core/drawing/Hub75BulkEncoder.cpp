/**
 * @file Hub75BulkEncoder.cpp
 * @brief Implementation of the decoupled RGB565 to HUB75 bitplane bulk encoder.
 */
#include "Hub75BulkEncoder.h"

// Full 256-entry CIE 1931 perceptual lightness table for 8-bit output (0..255 -> 0..255)
static const uint8_t s_cie1931_8bit[256] = {
      0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,   3,   4,
      4,   4,   4,   4,   4,   5,   5,   5,   5,   5,   6,   6,   6,   6,   6,   7,
      7,   7,   7,   8,   8,   8,   8,   9,   9,   9,  10,  10,  10,  10,  11,  11,
     11,  12,  12,  12,  13,  13,  13,  14,  14,  15,  15,  15,  16,  16,  17,  17,
     17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  23,  24,  24,  25,
     25,  26,  26,  27,  28,  28,  29,  29,  30,  31,  31,  32,  32,  33,  34,  34,
     35,  36,  37,  37,  38,  39,  39,  40,  41,  42,  43,  43,  44,  45,  46,  47,
     47,  48,  49,  50,  51,  52,  53,  54,  54,  55,  56,  57,  58,  59,  60,  61,
     62,  63,  64,  65,  66,  67,  68,  70,  71,  72,  73,  74,  75,  76,  77,  79,
     80,  81,  82,  83,  85,  86,  87,  88,  90,  91,  92,  94,  95,  96,  98,  99,
    100, 102, 103, 105, 106, 108, 109, 110, 112, 113, 115, 116, 118, 120, 121, 123,
    124, 126, 128, 129, 131, 132, 134, 136, 138, 139, 141, 143, 145, 146, 148, 150,
    152, 154, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181,
    183, 185, 187, 189, 191, 193, 196, 198, 200, 202, 204, 207, 209, 211, 214, 216,
    218, 220, 223, 225, 228, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255
};

static inline uint8_t scaleChannelToDepth(uint8_t c8, uint8_t depth, uint16_t maxVal) {
    const uint16_t cie = s_cie1931_8bit[c8];
    if (depth >= 7U) {
        const uint8_t shift = static_cast<uint8_t>(8U - depth);
        const uint8_t round = (shift > 0U) ? static_cast<uint8_t>(1U << (shift - 1U)) : 0U;
        const uint16_t scaled = static_cast<uint16_t>((cie + round) >> shift);
        return (scaled > maxVal) ? static_cast<uint8_t>(maxVal) : static_cast<uint8_t>(scaled);
    }
    // At <= 6-bit depth, pure gamma-2.2 CIE crushes the bottom ~22% of sRGB to 0, turning cinema
    // shadows into pure red/green channel-clipping artifacts. Blend CIE with linear sRGB proportionally
    // to bit-depth reduction so dark gradients stay visible and balanced while black (0) stays 0.
    const uint32_t cieW = static_cast<uint32_t>(depth - 1U);
    const uint32_t linW = static_cast<uint32_t>(7U - depth);
    const uint32_t eff8 = (static_cast<uint32_t>(cie) * cieW + static_cast<uint32_t>(c8) * linW + 3U) / 6U;
    const uint32_t scaled = (eff8 * static_cast<uint32_t>(maxVal) + 127U) / 255U;
    return (scaled > maxVal) ? static_cast<uint8_t>(maxVal) : static_cast<uint8_t>(scaled);
}

void Hub75BulkEncoder::generateLuts(uint8_t depth, uint8_t* lutR, uint8_t* lutG, uint8_t* lutB) {
    if (!lutR || !lutG || !lutB) return;
    if (depth < 2) depth = 8;
    if (depth > 8) depth = 8;

    const uint16_t maxVal = static_cast<uint16_t>((1U << depth) - 1U);

    for (int r = 0; r < 32; r++) {
        uint8_t r8 = static_cast<uint8_t>((r << 3) | (r >> 2));
        lutR[r] = scaleChannelToDepth(r8, depth, maxVal);
        lutB[r] = lutR[r];
    }

    if (depth <= 5U) {
        // At <= 5-bit output (<= 32 levels), pair G6 entries `(2k, 2k+1)` with R5/B5 `k` so 100% of
        // neutral RGB565 greys `(r5 == b5 == g6 >> 1)` produce exact `R == G == B` with zero green cast.
        for (int g = 0; g < 64; g++) {
            lutG[g] = lutR[g >> 1];
        }
    } else {
        for (int g = 0; g < 64; g++) {
            uint8_t g8 = static_cast<uint8_t>((g << 2) | (g >> 4));
            lutG[g] = scaleChannelToDepth(g8, depth, maxVal);
        }
    }
}

uint32_t Hub75BulkEncoder::encode(
    const uint16_t* canvas,
    size_t canvasStridePixels,
    DmaRowAccessor rowAccessor,
    void* accessorCtx,
    const Hub75EncodingParams& params
) {
    if (!canvas || !rowAccessor || !params.lutR || !params.lutG || !params.lutB) {
        return 0;
    }

    uint32_t tStart = micros();
    const int w = params.width;
    const int rpf = params.rowsPerFrame;
    const uint8_t depth = params.colorDepth;
    const bool rot180 = (params.rotation == 2);
    const size_t stride = (canvasStridePixels > 0) ? canvasStridePixels : w;

    const uint8_t effDepth = (depth > 8U) ? 8U : depth;
    const uint8_t* const lutR = params.lutR;
    const uint8_t* const lutG = params.lutG;
    const uint8_t* const lutB = params.lutB;

    for (int y = 0; y < rpf; y++) {
        const uint16_t* src1;
        const uint16_t* src2;

        if (!rot180) {
            src1 = canvas + (size_t)y * stride;
            src2 = canvas + (size_t)(y + rpf) * stride;
        } else {
            src1 = canvas + (size_t)(params.height - 1 - y) * stride;
            src2 = canvas + (size_t)(params.height - 1 - (y + rpf)) * stride;
        }

        uint16_t* dmaRows[8] = {nullptr};
        bool anyPlane = false;
        for (uint8_t p = 0; p < effDepth; p++) {
            dmaRows[p] = rowAccessor(accessorCtx, static_cast<uint8_t>(y), p);
            if (dmaRows[p]) {
                anyPlane = true;
            }
        }
        if (!anyPlane) continue;

        for (int x = 0; x < w; x++) {
            const int sx = rot180 ? (w - 1 - x) : x;
            const uint16_t c1 = src1[sx];
            const uint16_t c2 = src2[sx];

            const uint8_t r1 = lutR[(c1 >> 11) & 0x1F];
            const uint8_t g1 = lutG[(c1 >> 5) & 0x3F];
            const uint8_t b1 = lutB[c1 & 0x1F];

            const uint8_t r2 = lutR[(c2 >> 11) & 0x1F];
            const uint8_t g2 = lutG[(c2 >> 5) & 0x3F];
            const uint8_t b2 = lutB[c2 & 0x1F];

            const int ax = MATRIX_TX_ADJUST(x);
            for (uint8_t p = 0; p < effDepth; p++) {
                uint16_t* dmaRow = dmaRows[p];
                if (!dmaRow) continue;
                const uint16_t rgb =
                    static_cast<uint16_t>((r1 >> p) & 1U) |
                    (static_cast<uint16_t>((g1 >> p) & 1U) << 1) |
                    (static_cast<uint16_t>((b1 >> p) & 1U) << 2) |
                    (static_cast<uint16_t>((r2 >> p) & 1U) << 3) |
                    (static_cast<uint16_t>((g2 >> p) & 1U) << 4) |
                    (static_cast<uint16_t>((b2 >> p) & 1U) << 5);
                dmaRow[ax] = (dmaRow[ax] & HUB75_BITMASK_RGB12_CLEAR) | rgb;
            }
        }
    }

    return (micros() - tStart);
}

uint32_t Hub75BulkEncoder::encode(
    const uint16_t* canvas,
    size_t canvasStridePixels,
    const Hub75DmaTarget& target,
    const Hub75EncodingParams& params
) {
    if (target.rowAccessor) {
        return encode(canvas, canvasStridePixels, target.rowAccessor, target.accessorCtx, params);
    }
    auto targetAccessor = [](void* ctx, uint8_t row, uint8_t plane) -> uint16_t* {
        const auto* t = static_cast<const Hub75DmaTarget*>(ctx);
        return t->getRowPtr(row, plane);
    };
    return encode(canvas, canvasStridePixels, targetAccessor, const_cast<void*>(static_cast<const void*>(&target)), params);
}
