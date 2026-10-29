/**
 * @file Hub75BcmCalibration.cpp
 * @brief Implementation of the weight-calibrated HUB75 bit-plane LUT builder.
 */
#include "Hub75BcmCalibration.h"

#include <math.h>

namespace Hub75BcmCalibration {

float cieLinear(uint8_t v8) {
    const float lStar = (static_cast<float>(v8) / 255.0f) * 100.0f;
    if (lStar <= 8.0f) {
        return lStar / 902.3f;
    }
    const float f = (lStar + 16.0f) / 116.0f;
    return f * f * f;
}

void computePlaneWeights(
    uint8_t depth,
    uint8_t lsbMsbTransitionBit,
    uint16_t rowWidth,
    uint8_t latchBlanking,
    uint8_t brightness8,
    uint32_t outWeights[MAX_DEPTH]
) {
    if (depth == 0) return;
    if (depth > MAX_DEPTH) depth = MAX_DEPTH;
    const int d = static_cast<int>(depth);
    const int lsb = static_cast<int>(lsbMsbTransitionBit);
    const int bitshift = (d - lsb - 1) >> 1;
    const int usable = static_cast<int>(rowWidth) - static_cast<int>(latchBlanking);

    for (int i = 0; i < d; ++i) {
        // Descriptor chaining: one common pass for every plane, plus 2^(i - lsb - 1) repeats above lsb.
        const uint32_t passes = 1U + ((i > lsb) ? (1U << (i - lsb - 1)) : 0U);

        // OE windowing (setBrightnessOE): identical integer arithmetic to the library.
        const int bitplane = (2 * d - i) % d;
        int rightshift = bitplane - bitshift - 2;
        if (rightshift < 0) rightshift = 0;
        const int maxPixels = (usable > 0) ? (usable >> rightshift) : 1;
        int pixels = (maxPixels * static_cast<int>(brightness8)) >> 8;
        if (brightness8 > 0 && pixels == 0) pixels = 1;
        if (pixels > maxPixels - 1) pixels = maxPixels - 1;
        if (pixels < 0) pixels = 0;

        outWeights[i] = passes * static_cast<uint32_t>(pixels);
    }
}

uint32_t codeEnergy(const uint32_t* weights, uint8_t depth, uint8_t code) {
    uint32_t energy = 0;
    for (uint8_t i = 0; i < depth; ++i) {
        if (code & (1U << i)) {
            energy += weights[i];
        }
    }
    return energy;
}

void buildChannelLut(const uint32_t* weights, uint8_t depth, uint8_t inputBits, uint8_t* outLut) {
    if (!weights || !outLut || depth == 0 || depth > MAX_DEPTH || inputBits == 0 || inputBits > 8) return;

    const uint16_t maxCode = static_cast<uint16_t>((1U << depth) - 1U);
    const uint16_t levels = static_cast<uint16_t>(1U << inputBits);
    const float fullEnergy = static_cast<float>(codeEnergy(weights, depth, static_cast<uint8_t>(maxCode)));

    outLut[0] = 0;
    uint32_t prevEnergy = 0;
    for (uint16_t v = 1; v < levels; ++v) {
        if (v == levels - 1U) {
            outLut[v] = static_cast<uint8_t>(maxCode);
            break;
        }
        // Expand the channel to 8 bits exactly like RGB565 -> RGB888 replication.
        uint8_t v8;
        if (inputBits >= 8) {
            v8 = static_cast<uint8_t>(v);
        } else {
            const uint8_t up = static_cast<uint8_t>(8U - inputBits);
            v8 = static_cast<uint8_t>((v << up) | (v >> (inputBits - up > 0 ? inputBits - up : 0)));
        }
        const float target = cieLinear(v8) * fullEnergy;

        uint16_t bestCode = 0;
        uint32_t bestEnergy = 0;
        float bestErr = -1.0f;
        for (uint16_t c = 0; c <= maxCode; ++c) {
            const uint32_t e = codeEnergy(weights, depth, static_cast<uint8_t>(c));
            if (e < prevEnergy) continue; // Never emit less light than the previous channel level.
            const float diff = static_cast<float>(e) - target;
            const float err = (diff < 0.0f) ? -diff : diff;
            if (bestErr < 0.0f || err < bestErr || (err == bestErr && e < bestEnergy)) {
                bestErr = err;
                bestCode = c;
                bestEnergy = e;
            }
        }
        outLut[v] = static_cast<uint8_t>(bestCode);
        prevEnergy = bestEnergy;
    }
}

} // namespace Hub75BcmCalibration
