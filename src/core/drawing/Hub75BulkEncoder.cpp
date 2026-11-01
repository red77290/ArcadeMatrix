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

    const int w = params.width;
    const int rpf = params.rowsPerFrame;
    const uint8_t depth = params.colorDepth;
    const uint8_t effDepth = (depth > 8U) ? 8U : depth;
    if (w <= 0 || rpf <= 0 || effDepth == 0U) {
        return 0;
    }

    int lastDirtyRow = rpf - 1;
#if defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(ESP32) || defined(UNIT_TEST)
    uint32_t dirtyMask = 0xFFFFFFFFU;
    if (rpf <= 32) {
        const uint32_t allRowsMask = (rpf == 32) ? 0xFFFFFFFFU : ((1U << rpf) - 1U);
        dirtyMask = params.dirtyScanRows & allRowsMask;
        if (dirtyMask == 0U) {
            return 0;
        }
        lastDirtyRow = 31 - __builtin_clz(dirtyMask);
    } else if (params.dirtyScanRows == 0U) {
        return 0;
    }
#endif

    uint32_t tStart = micros();
    const bool rot180 = (params.rotation == 2);
    const size_t stride = (canvasStridePixels > 0) ? canvasStridePixels : static_cast<size_t>(w);

    const uint8_t* const lutR = params.lutR;
    const uint8_t* const lutG = params.lutG;
    const uint8_t* const lutB = params.lutB;

    for (int y = 0; y <= lastDirtyRow; y++) {
        if (params.rowSync) {
            params.rowSync(params.syncCtx, static_cast<uint8_t>(y));
        }
#if defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(ESP32) || defined(UNIT_TEST)
        if (rpf <= 32 && ((dirtyMask >> y) & 1U) == 0U) {
            continue;
        }
#endif

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
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(UNIT_TEST)
        bool allPlanesValid = (effDepth > 0U);
#endif
        for (uint8_t p = 0; p < effDepth; p++) {
            dmaRows[p] = rowAccessor(accessorCtx, static_cast<uint8_t>(y), p);
            if (dmaRows[p]) {
                anyPlane = true;
            }
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(UNIT_TEST)
            else {
                allPlanesValid = false;
            }
#endif
        }
        if (!anyPlane) continue;

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(UNIT_TEST)
        if (effDepth == 8U && allPlanesValid) {
            uint16_t* const d0 = dmaRows[0];
            uint16_t* const d1 = dmaRows[1];
            uint16_t* const d2 = dmaRows[2];
            uint16_t* const d3 = dmaRows[3];
            uint16_t* const d4 = dmaRows[4];
            uint16_t* const d5 = dmaRows[5];
            uint16_t* const d6 = dmaRows[6];
            uint16_t* const d7 = dmaRows[7];

            for (int x = 0; x < w; x++) {
                const int sx = rot180 ? (w - 1 - x) : x;
                const uint16_t c1 = src1[sx];
                const uint16_t c2 = src2[sx];

                const uint32_t r1 = lutR[(c1 >> 11) & 0x1F];
                const uint32_t g1 = lutG[(c1 >> 5) & 0x3F];
                const uint32_t b1 = lutB[c1 & 0x1F];

                const uint32_t r2 = lutR[(c2 >> 11) & 0x1F];
                const uint32_t g2 = lutG[(c2 >> 5) & 0x3F];
                const uint32_t b2 = lutB[c2 & 0x1F];

                const int ax = MATRIX_TX_ADJUST(x);
                d0[ax] = static_cast<uint16_t>((d0[ax] & HUB75_BITMASK_RGB12_CLEAR) | ((r1 & 1U) | ((g1 & 1U) << 1) | ((b1 & 1U) << 2) | ((r2 & 1U) << 3) | ((g2 & 1U) << 4) | ((b2 & 1U) << 5)));
                d1[ax] = static_cast<uint16_t>((d1[ax] & HUB75_BITMASK_RGB12_CLEAR) | (((r1 >> 1) & 1U) | (((g1 >> 1) & 1U) << 1) | (((b1 >> 1) & 1U) << 2) | (((r2 >> 1) & 1U) << 3) | (((g2 >> 1) & 1U) << 4) | (((b2 >> 1) & 1U) << 5)));
                d2[ax] = static_cast<uint16_t>((d2[ax] & HUB75_BITMASK_RGB12_CLEAR) | (((r1 >> 2) & 1U) | (((g1 >> 2) & 1U) << 1) | (((b1 >> 2) & 1U) << 2) | (((r2 >> 2) & 1U) << 3) | (((g2 >> 2) & 1U) << 4) | (((b2 >> 2) & 1U) << 5)));
                d3[ax] = static_cast<uint16_t>((d3[ax] & HUB75_BITMASK_RGB12_CLEAR) | (((r1 >> 3) & 1U) | (((g1 >> 3) & 1U) << 1) | (((b1 >> 3) & 1U) << 2) | (((r2 >> 3) & 1U) << 3) | (((g2 >> 3) & 1U) << 4) | (((b2 >> 3) & 1U) << 5)));
                d4[ax] = static_cast<uint16_t>((d4[ax] & HUB75_BITMASK_RGB12_CLEAR) | (((r1 >> 4) & 1U) | (((g1 >> 4) & 1U) << 1) | (((b1 >> 4) & 1U) << 2) | (((r2 >> 4) & 1U) << 3) | (((g2 >> 4) & 1U) << 4) | (((b2 >> 4) & 1U) << 5)));
                d5[ax] = static_cast<uint16_t>((d5[ax] & HUB75_BITMASK_RGB12_CLEAR) | (((r1 >> 5) & 1U) | (((g1 >> 5) & 1U) << 1) | (((b1 >> 5) & 1U) << 2) | (((r2 >> 5) & 1U) << 3) | (((g2 >> 5) & 1U) << 4) | (((b2 >> 5) & 1U) << 5)));
                d6[ax] = static_cast<uint16_t>((d6[ax] & HUB75_BITMASK_RGB12_CLEAR) | (((r1 >> 6) & 1U) | (((g1 >> 6) & 1U) << 1) | (((b1 >> 6) & 1U) << 2) | (((r2 >> 6) & 1U) << 3) | (((g2 >> 6) & 1U) << 4) | (((b2 >> 6) & 1U) << 5)));
                d7[ax] = static_cast<uint16_t>((d7[ax] & HUB75_BITMASK_RGB12_CLEAR) | (((r1 >> 7) & 1U) | (((g1 >> 7) & 1U) << 1) | (((b1 >> 7) & 1U) << 2) | (((r2 >> 7) & 1U) << 3) | (((g2 >> 7) & 1U) << 4) | (((b2 >> 7) & 1U) << 5)));
            }
        } else
#endif
        {
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

        if (params.rowFlush) {
            params.rowFlush(params.flushCtx, static_cast<uint8_t>(y));
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
    Hub75EncodingParams effParams = params;
    if (!effParams.rowFlush && target.rowFlush) {
        effParams.rowFlush = target.rowFlush;
        effParams.flushCtx = target.accessorCtx;
    }
    if (!effParams.rowSync && target.rowSync) {
        effParams.rowSync = target.rowSync;
        effParams.syncCtx = target.accessorCtx;
    }
    if (target.rowAccessor) {
        return encode(canvas, canvasStridePixels, target.rowAccessor, target.accessorCtx, effParams);
    }
    auto targetAccessor = [](void* ctx, uint8_t row, uint8_t plane) -> uint16_t* {
        const auto* t = static_cast<const Hub75DmaTarget*>(ctx);
        return t->getRowPtr(row, plane);
    };
    return encode(canvas, canvasStridePixels, targetAccessor, const_cast<void*>(static_cast<const void*>(&target)), effParams);
}

#if defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(ESP32)
static inline uint64_t spreadByteBitsToPlanes64(uint8_t v) {
    uint64_t out = 0;
    for (uint8_t p = 0; p < 8U; ++p) {
        out |= static_cast<uint64_t>((v >> p) & 1U) << (p * 8U);
    }
    return out;
}

void Hub75BulkEncoder::generateSpreadLuts64(
    const uint8_t* lutR,
    const uint8_t* lutG,
    const uint8_t* lutB,
    Hub75SpreadLuts64& out
) {
    if (!lutR || !lutG || !lutB) return;
    for (int i = 0; i < 32; ++i) {
        out.r[i] = spreadByteBitsToPlanes64(lutR[i]);
        out.b[i] = spreadByteBitsToPlanes64(lutB[i]) << 2;
    }
    for (int i = 0; i < 64; ++i) {
        out.g[i] = spreadByteBitsToPlanes64(lutG[i]) << 1;
    }
}

void Hub75BulkEncoder::encodeRow8BitStaged(
    const uint16_t* srcTop,
    const uint16_t* srcBot,
    uint16_t width,
    uint8_t row,
    uint8_t rowsPerFrame,
    bool rot180,
    const Hub75SpreadLuts64& luts64,
    const uint16_t* latOeTemplate,
    uint16_t* rowOut
) {
    if (!srcTop || !srcBot || !latOeTemplate || !rowOut || width == 0U || rowsPerFrame == 0U) {
        return;
    }

    const uint8_t prevRow = (row == 0U) ? static_cast<uint8_t>(rowsPerFrame - 1U) : static_cast<uint8_t>(row - 1U);
    const uint16_t addr0 = static_cast<uint16_t>((static_cast<uint16_t>(prevRow) & 0x1FU) << 8);
    const uint16_t addrN = static_cast<uint16_t>((static_cast<uint16_t>(row) & 0x1FU) << 8);

#if !defined(__BYTE_ORDER__) || (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    const bool aligned32 =
        (MATRIX_TX_ADJUST(0) == 0) &&
        ((width & 1U) == 0U) &&
        (((reinterpret_cast<uintptr_t>(latOeTemplate) | reinterpret_cast<uintptr_t>(rowOut)) & 3U) == 0U);
    if (aligned32) {
        const uint32_t addr0_32 = static_cast<uint32_t>(addr0) | (static_cast<uint32_t>(addr0) << 16);
        const uint32_t addrN_32 = static_cast<uint32_t>(addrN) | (static_cast<uint32_t>(addrN) << 16);
        const uint16_t pairs = static_cast<uint16_t>(width >> 1);

        const auto* const t0 = reinterpret_cast<const uint32_t*>(latOeTemplate + 0U * width);
        const auto* const t1 = reinterpret_cast<const uint32_t*>(latOeTemplate + 1U * width);
        const auto* const t2 = reinterpret_cast<const uint32_t*>(latOeTemplate + 2U * width);
        const auto* const t3 = reinterpret_cast<const uint32_t*>(latOeTemplate + 3U * width);
        const auto* const t4 = reinterpret_cast<const uint32_t*>(latOeTemplate + 4U * width);
        const auto* const t5 = reinterpret_cast<const uint32_t*>(latOeTemplate + 5U * width);
        const auto* const t6 = reinterpret_cast<const uint32_t*>(latOeTemplate + 6U * width);
        const auto* const t7 = reinterpret_cast<const uint32_t*>(latOeTemplate + 7U * width);

        auto* const o0 = reinterpret_cast<uint32_t*>(rowOut + 0U * width);
        auto* const o1 = reinterpret_cast<uint32_t*>(rowOut + 1U * width);
        auto* const o2 = reinterpret_cast<uint32_t*>(rowOut + 2U * width);
        auto* const o3 = reinterpret_cast<uint32_t*>(rowOut + 3U * width);
        auto* const o4 = reinterpret_cast<uint32_t*>(rowOut + 4U * width);
        auto* const o5 = reinterpret_cast<uint32_t*>(rowOut + 5U * width);
        auto* const o6 = reinterpret_cast<uint32_t*>(rowOut + 6U * width);
        auto* const o7 = reinterpret_cast<uint32_t*>(rowOut + 7U * width);

        for (uint16_t i = 0U; i < pairs; ++i) {
            const uint16_t x0 = static_cast<uint16_t>(i << 1);
            const uint16_t sx0 = rot180 ? static_cast<uint16_t>(width - 1U - x0) : x0;
            const uint16_t sx1 = rot180 ? static_cast<uint16_t>(width - 2U - x0) : static_cast<uint16_t>(x0 + 1U);

            const uint16_t c1_0 = srcTop[sx0];
            const uint16_t c2_0 = srcBot[sx0];
            const uint16_t c1_1 = srcTop[sx1];
            const uint16_t c2_1 = srcBot[sx1];

            const uint64_t p0 =
                (luts64.r[(c1_0 >> 11) & 0x1FU] | luts64.g[(c1_0 >> 5) & 0x3FU] | luts64.b[c1_0 & 0x1FU]) |
                ((luts64.r[(c2_0 >> 11) & 0x1FU] | luts64.g[(c2_0 >> 5) & 0x3FU] | luts64.b[c2_0 & 0x1FU]) << 3);
            const uint64_t p1 =
                (luts64.r[(c1_1 >> 11) & 0x1FU] | luts64.g[(c1_1 >> 5) & 0x3FU] | luts64.b[c1_1 & 0x1FU]) |
                ((luts64.r[(c2_1 >> 11) & 0x1FU] | luts64.g[(c2_1 >> 5) & 0x3FU] | luts64.b[c2_1 & 0x1FU]) << 3);

            const uint32_t lo0 = static_cast<uint32_t>(p0);
            const uint32_t hi0 = static_cast<uint32_t>(p0 >> 32);
            const uint32_t lo1 = static_cast<uint32_t>(p1);
            const uint32_t hi1 = static_cast<uint32_t>(p1 >> 32);

            o0[i] = (t0[i] & 0x00C000C0U) | addr0_32 | (lo0 & 0x3FU)          | ((lo1 << 16) & 0x003F0000U);
            o1[i] = (t1[i] & 0x00C000C0U) | addrN_32 | ((lo0 >> 8) & 0x3FU)   | ((lo1 << 8)  & 0x003F0000U);
            o2[i] = (t2[i] & 0x00C000C0U) | addrN_32 | ((lo0 >> 16) & 0x3FU)  | (lo1         & 0x003F0000U);
            o3[i] = (t3[i] & 0x00C000C0U) | addrN_32 | (lo0 >> 24)            | ((lo1 >> 8)  & 0x003F0000U);
            o4[i] = (t4[i] & 0x00C000C0U) | addrN_32 | (hi0 & 0x3FU)          | ((hi1 << 16) & 0x003F0000U);
            o5[i] = (t5[i] & 0x00C000C0U) | addrN_32 | ((hi0 >> 8) & 0x3FU)   | ((hi1 << 8)  & 0x003F0000U);
            o6[i] = (t6[i] & 0x00C000C0U) | addrN_32 | ((hi0 >> 16) & 0x3FU)  | (hi1         & 0x003F0000U);
            o7[i] = (t7[i] & 0x00C000C0U) | addrN_32 | (hi0 >> 24)            | ((hi1 >> 8)  & 0x003F0000U);
        }
        return;
    }
#endif

    for (uint16_t x = 0U; x < width; ++x) {
        const uint16_t sx = rot180 ? static_cast<uint16_t>(width - 1U - x) : x;
        const uint16_t c1 = srcTop[sx];
        const uint16_t c2 = srcBot[sx];
        const uint64_t packed =
            (luts64.r[(c1 >> 11) & 0x1FU] | luts64.g[(c1 >> 5) & 0x3FU] | luts64.b[c1 & 0x1FU]) |
            ((luts64.r[(c2 >> 11) & 0x1FU] | luts64.g[(c2 >> 5) & 0x3FU] | luts64.b[c2 & 0x1FU]) << 3);
        const uint16_t ax = static_cast<uint16_t>(MATRIX_TX_ADJUST(x));
        rowOut[0U * width + ax] = static_cast<uint16_t>((latOeTemplate[0U * width + ax] & 0x00C0U) | addr0 | (packed & 0x3FU));
        for (uint8_t p = 1U; p < 8U; ++p) {
            rowOut[p * width + ax] = static_cast<uint16_t>((latOeTemplate[p * width + ax] & 0x00C0U) | addrN | ((packed >> (p * 8U)) & 0x3FU));
        }
    }
}
#endif
