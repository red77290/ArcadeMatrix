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

// Classic ESP32 I2S TX FIFO transmits 16-bit words swapped within each 32-bit pair.
// The library applies this in its own drawPixel; raw bitplane encoding must do the same.
#ifndef MATRIX_TX_ADJUST
#if defined(ESP32_THE_ORIG) || ((defined(CONFIG_IDF_TARGET_ESP32) || defined(HARDWARE_PROFILE_ESP32_DEV)) && !defined(CONFIG_IDF_TARGET_ESP32S3))
#define MATRIX_TX_ADJUST(x_coord) (((x_coord) & 1U) ? ((x_coord) - 1) : ((x_coord) + 1))
#else
#define MATRIX_TX_ADJUST(x_coord) (x_coord)
#endif
#endif

// Inverted bitmask clearing R1G1B1R2G2B2 (bits 0..5)
static constexpr uint16_t HUB75_BITMASK_RGB12_CLEAR = 0b1111111111000000;

#include "Hub75DmaTarget.h"

struct Hub75EncodingParams {
    uint8_t colorDepth = 8;          ///< 1 to 8 BCM bitplanes
    uint8_t rowsPerFrame = 16;       ///< height / 2 (for 1/16 scan = 16, 1/32 scan = 32)
    uint16_t width = 64;             ///< Pixels per row
    uint16_t height = 32;            ///< Physical height
    const uint8_t* lutR = nullptr;   ///< 32-byte CIE 1931 gamma LUT for Red (5-bit)
    const uint8_t* lutG = nullptr;   ///< 64-byte CIE 1931 gamma LUT for Green (6-bit)
    const uint8_t* lutB = nullptr;   ///< 32-byte CIE 1931 gamma LUT for Blue (5-bit)
    uint8_t rotation = 0;            ///< Physical orientation (0 or 2 for 180 deg)
    DmaRowFlush rowFlush = nullptr;  ///< Optional per-row cache writeback hook while row planes sit hot in L1 DCache
    void* flushCtx = nullptr;        ///< User context passed to `rowFlush`
    DmaRowSync rowSync = nullptr;    ///< Optional per-row hardware DMA scanline sync hook (single-buffer beam-racing)
    void* syncCtx = nullptr;         ///< User context passed to `rowSync`
};

/**
 * @struct Hub75SingleDmaScanChaser
 * @brief Tear-free hardware DMA scanline chaser ("beam-racing") for single-DMA-buffer HUB75 panels.
 *
 * On 256x64 single-DMA-buffer panels (`ROWS_PER_FRAME = 32`), GDMA scans rows `0..31` in ~5.53 ms
 * (~173 us/row) while CPU encodes rows `0..31` in ~3.0 ms (~95 us/row). Without scanline chasing,
 * starting an encode near the end of a DMA scan overwrites `row 0` (`y = 0` & `y = 32`) with Frame N+1
 * right before GDMA finishes `row 31` (`y = 31` & `y = 63`) with Frame N, creating a visible seam at
 * the vertical center (`y = 31 / 32`).
 *
 * By gating `row 0` to start only when `dmaRow` is inside `[2 .. maxStartRow]` and keeping CPU writes
 * at least 2 scan rows behind `dmaRow` until the current hardware scan wraps `31 -> 0`, Scan S reads
 * 100% Frame N across all rows and Scan S+1 reads 100% Frame N+1 across all rows with zero extra RAM.
 */
struct Hub75SingleDmaScanChaser {
    using QueryDmaRowFn = int (*)(void* userCtx);
    using DelayUsFn = void (*)(uint32_t us);

    QueryDmaRowFn queryRow = nullptr;
    void* queryCtx = nullptr;
    DelayUsFn delayFn = nullptr;
    int rowsPerFrame = 0;
    int lastDmaRow = -1;
    bool active = false;
    bool scanWrapped = false;

    static constexpr uint32_t kStepDelayUs = 12U;
    static constexpr int kMaxStartWaitSteps = 900; // Hard cap: 10,800 us (covers full 90-180 Hz scan wrap)
    static constexpr int kMaxRowWaitSteps = 45;    // Hard cap: 540 us/row

    void begin(QueryDmaRowFn query, void* ctx, int rpf, DelayUsFn delayCb = nullptr) {
        queryRow = query;
        queryCtx = ctx;
        delayFn = delayCb;
        rowsPerFrame = rpf;
        lastDmaRow = -1;
        scanWrapped = false;
        active = (query != nullptr && rpf >= 8);
    }

    inline void stepDelay() const {
        if (delayFn) {
            delayFn(kStepDelayUs);
        } else {
            delayMicroseconds(kStepDelayUs);
        }
    }

    void syncRow(uint8_t row) {
        if (!active || !queryRow) return;

        const int rpf = rowsPerFrame;
        const int y = static_cast<int>(row);
        if (y < 0 || y >= rpf) return;

        if (y == 0) {
            int d = queryRow(queryCtx);
            if (d < 0 || d >= rpf) {
                active = false;
                return;
            }
            // Gate `row 0` to start strictly right after scan wrap (`[2 .. 4]` on 1/32 scan,
            // `[2 .. 3]` on 1/16 scan). This guarantees tear-free output in BOTH timing regimes:
            //  1) `T_cpu < T_dma`: per-row guard `(y + 1) >= d` keeps CPU >= 2 rows behind Scan S.
            //  2) `T_cpu > T_dma` (Octal PSRAM round-robin contention on 256x64 8-bit): Scan S pulls
            //     ahead of CPU (100% Frame N), wraps `31 -> 0` when CPU is near the end, and CPU
            //     completes row 31 + cache writeback well before Scan S+1 reaches row 31 (100% Frame N+1).
            const int minStartRow = 2;
            const int maxStartRow = (rpf >= 32) ? 4 : 3;

            int steps = 0;
            while ((d < minStartRow || d > maxStartRow) && steps < kMaxStartWaitSteps) {
                stepDelay();
                ++steps;
                d = queryRow(queryCtx);
                if (d < 0 || d >= rpf) {
                    active = false;
                    return;
                }
            }
            lastDmaRow = d;
            scanWrapped = false;
            return;
        }

        if (scanWrapped) {
            return;
        }

        int d = queryRow(queryCtx);
        if (d < 0 || d >= rpf) {
            active = false;
            return;
        }
        if (d + 1 < lastDmaRow) {
            scanWrapped = true;
            return;
        }
        if (d > lastDmaRow) {
            lastDmaRow = d;
        }

        int steps = 0;
        while (!scanWrapped && (y + 1) >= d && steps < kMaxRowWaitSteps) {
            stepDelay();
            ++steps;
            d = queryRow(queryCtx);
            if (d < 0 || d >= rpf) {
                active = false;
                return;
            }
            if (d + 1 < lastDmaRow) {
                scanWrapped = true;
                return;
            }
            if (d > lastDmaRow) {
                lastDmaRow = d;
            }
        }
    }

    static void callback(void* ctx, uint8_t row) {
        auto* self = static_cast<Hub75SingleDmaScanChaser*>(ctx);
        if (self) {
            self->syncRow(row);
        }
    }
};

class Hub75BulkEncoder {
public:
    /**
     * @brief Row accessor callback to obtain a pointer to the DMA line buffer for row y and bitplane p.
     */
    using DmaRowAccessor = ::DmaRowAccessor;

    /**
     * @brief Encodes an RGB565 linear canvas into packed HUB75 BCM bitplanes via row accessor.
     */
    static uint32_t encode(
        const uint16_t* canvas,
        size_t canvasStridePixels,
        DmaRowAccessor rowAccessor,
        void* accessorCtx,
        const Hub75EncodingParams& params
    );

    /**
     * @brief Encodes an RGB565 linear canvas directly into a Hub75DmaTarget.
     */
    static uint32_t encode(
        const uint16_t* canvas,
        size_t canvasStridePixels,
        const Hub75DmaTarget& target,
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
