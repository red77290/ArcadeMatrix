/**
 * @file Hub75BulkEncoder.cpp
 * @brief Implementation of the decoupled RGB565 to HUB75 bitplane bulk encoder.
 */
#include "Hub75BulkEncoder.h"

// Standard CIE 1931 perceptual lightness table for 8-bit output (gamma ~2.2)
static const uint8_t s_cie1931_8bit[256] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,
      1,   1,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   4,   4,   4,   4,
      5,   5,   5,   5,   6,   6,   6,   7,   7,   7,   8,   8,   8,   9,   9,  10,
     10,  10,  11,  11,  12,  12,  13,  13,  14,  14,  15,  15,  16,  16,  17,  18,
     18,  19,  19,  20,  21,  21,  22,  23,  23,  24,  25,  25,  26,  27,  28,  28,
     29,  30,  31,  32,  32,  33,  34,  35,  36,  37,  38,  38,  39,  40,  41,  42,
     43,  44,  45,  46,  47,  48,  49,  51,  52,  53,  54,  55,  56,  57,  58,  60,
     61,  62,  63,  65,  66,  67,  68,  70,  71,  72,  74,  75,  77,  78,  79,  81,
     82,  84,  85,  87,  88,  90,  91,  93,  94,  96,  98,  99, 101, 103, 104, 106,
    108, 110, 111, 113, 115, 117, 119, 120, 122, 124, 126, 128, 130, 132, 134, 136,
    138, 140, 142, 144, 146, 148, 150, 153, 155, 157, 159, 162, 164, 166, 168, 171,
    173, 175, 178, 180, 183, 185, 188, 190, 193, 195, 198, 201, 203, 206, 208, 211,
    214, 216, 219, 222, 225, 227, 230, 233, 236, 239, 242, 245, 247, 250, 253, 255
};

void Hub75BulkEncoder::generateLuts(uint8_t depth, uint8_t* lutR, uint8_t* lutG, uint8_t* lutB) {
    if (!lutR || !lutG || !lutB) return;
    if (depth < 2) depth = 8;
    if (depth > 8) depth = 8;

    uint8_t shift = 8 - depth;
    uint8_t round = (shift > 0) ? (1 << (shift - 1)) : 0;
    uint16_t maxVal = (1 << depth) - 1;

    for (int r = 0; r < 32; r++) {
        uint8_t r8 = (r << 3) | (r >> 2);
        uint16_t val = s_cie1931_8bit[r8];
        uint16_t scaled = (val + round) >> shift;
        lutR[r] = (scaled > maxVal) ? (uint8_t)maxVal : (uint8_t)scaled;
    }

    for (int g = 0; g < 64; g++) {
        uint8_t g8 = (g << 2) | (g >> 4);
        uint16_t val = s_cie1931_8bit[g8];
        uint16_t scaled = (val + round) >> shift;
        lutG[g] = (scaled > maxVal) ? (uint8_t)maxVal : (uint8_t)scaled;
    }

    for (int b = 0; b < 32; b++) {
        uint8_t b8 = (b << 3) | (b >> 2);
        uint16_t val = s_cie1931_8bit[b8];
        uint16_t scaled = (val + round) >> shift;
        lutB[b] = (scaled > maxVal) ? (uint8_t)maxVal : (uint8_t)scaled;
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

        for (uint8_t p = 0; p < depth; p++) {
            uint16_t* dmaRow = rowAccessor(accessorCtx, y, p);
            if (!dmaRow) continue;

            for (int x = 0; x < w; x++) {
                uint16_t c1, c2;
                if (!rot180) {
                    c1 = src1[x];
                    c2 = src2[x];
                } else {
                    c1 = src1[w - 1 - x];
                    c2 = src2[w - 1 - x];
                }

                uint8_t r1 = (params.lutR[(c1 >> 11) & 0x1F] >> p) & 1;
                uint8_t g1 = (params.lutG[(c1 >> 5) & 0x3F] >> p) & 1;
                uint8_t b1 = (params.lutB[c1 & 0x1F] >> p) & 1;

                uint8_t r2 = (params.lutR[(c2 >> 11) & 0x1F] >> p) & 1;
                uint8_t g2 = (params.lutG[(c2 >> 5) & 0x3F] >> p) & 1;
                uint8_t b2 = (params.lutB[c2 & 0x1F] >> p) & 1;

                uint16_t rgb = r1 | (g1 << 1) | (b1 << 2) | (r2 << 3) | (g2 << 4) | (b2 << 5);
                int ax = MATRIX_TX_ADJUST(x);
                dmaRow[ax] = (dmaRow[ax] & BITMASK_RGB12_CLEAR) | rgb;
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
