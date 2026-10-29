/**
 * @file Hub75BcmCalibration.h
 * @brief Monotonic, weight-calibrated channel LUTs for ESP32-HUB75-MatrixPanel-DMA bit planes.
 *
 * The HUB75 DMA library emits each bit plane once in a common pass, then repeats planes above
 * `lsbMsbTransitionBit` `2^(i - lsb - 1)` extra times, and finally narrows the Output-Enable window
 * of low planes (`setBrightnessOE()`). The resulting light weight of plane `i` is
 * `passes(i) * oePixels(i)`, which is NOT binary: at 4 bits the weights are 5/3/2/1 (code 7 is
 * brighter than code 8) and at 8 bits with `lsbMsbTransitionBit = 2` the LSB is 8x too bright.
 *
 * This module mirrors that exact timing model and builds, for each RGB565 channel value, the
 * plane code whose real emitted light is the closest monotonic match to the CIE 1931 target.
 * Pure functions, zero heap, cold path only (pipeline reconfiguration / brightness change).
 */
#pragma once

#include <stdint.h>

namespace Hub75BcmCalibration {

static constexpr uint8_t MAX_DEPTH = 8;

/**
 * @brief Perceptual (CIE 1931 L*) 8-bit code to linear light in [0, 1].
 */
float cieLinear(uint8_t v8);

/**
 * @brief Computes the effective light weight of every bit plane (index 0 = LSB), mirroring the
 *        library's descriptor chaining (`setupDMA()`) and OE windowing (`setBrightnessOE()`).
 * @param depth Colour depth in bit planes (1..8).
 * @param lsbMsbTransitionBit Value computed by the library in `setupDMA()`.
 * @param rowWidth Pixels per DMA row (`PIXELS_PER_ROW`).
 * @param latchBlanking `HUB75_I2S_CFG::latch_blanking`.
 * @param brightness8 Library brightness (0..255).
 * @param outWeights Receives `depth` weights.
 */
void computePlaneWeights(
    uint8_t depth,
    uint8_t lsbMsbTransitionBit,
    uint16_t rowWidth,
    uint8_t latchBlanking,
    uint8_t brightness8,
    uint32_t outWeights[MAX_DEPTH]
);

/**
 * @brief Builds a monotonic LUT mapping every `inputBits`-bit channel value (5 for R/B, 6 for G)
 *        to the plane code whose real light (sum of plane weights) best matches the CIE target.
 *        Guarantees: `lut[0] == 0`, `lut[max] == (1 << depth) - 1`, emitted light never decreases.
 */
void buildChannelLut(const uint32_t* weights, uint8_t depth, uint8_t inputBits, uint8_t* outLut);

/**
 * @brief Emitted light of `code` for the given plane weights.
 */
uint32_t codeEnergy(const uint32_t* weights, uint8_t depth, uint8_t code);

} // namespace Hub75BcmCalibration
