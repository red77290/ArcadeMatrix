#pragma once
#include <math.h>
#include <stddef.h>

/**
 * @file FFT64.h
 * @brief Shared, allocation-free 64-point real-to-complex FFT used by every audio
 *        spectrum consumer.
 *
 * This is the single source of truth for spectral analysis. Both the streamed-audio
 * path (AudioAnalysisService, fed by WebRadio / Bluetooth on Core 0) and the
 * microphone path (HardwareHAL::getAudioSpectrum, called from the Core 1 render
 * loop) must produce visually identical bars, so they must share the same
 * transform and the same band mapping.
 *
 * Core 1 hot-path contract: no dynamic allocation, no mutex, no static mutable
 * state. All scratch memory lives on the caller's stack (~512 bytes).
 */
namespace arcade_audio {

static constexpr size_t FFT_SIZE = 64;
static constexpr size_t FFT_BINS = 32;  // Real input -> only N/2 bins are meaningful

static const float HANN_64[FFT_SIZE] = {
    0.0000f, 0.0024f, 0.0096f, 0.0215f, 0.0381f, 0.0588f, 0.0834f, 0.1114f,
    0.1424f, 0.1760f, 0.2117f, 0.2489f, 0.2872f, 0.3259f, 0.3646f, 0.4026f,
    0.4394f, 0.4746f, 0.5076f, 0.5379f, 0.5652f, 0.5891f, 0.6092f, 0.6253f,
    0.6372f, 0.6446f, 0.6475f, 0.6457f, 0.6393f, 0.6284f, 0.6130f, 0.5934f,
    0.5700f, 0.5431f, 0.5132f, 0.4807f, 0.4460f, 0.4098f, 0.3725f, 0.3346f,
    0.2965f, 0.2588f, 0.2219f, 0.1863f, 0.1524f, 0.1206f, 0.0913f, 0.0650f,
    0.0420f, 0.0230f, 0.0084f, 0.0000f, 0.0000f, 0.0084f, 0.0230f, 0.0420f,
    0.0650f, 0.0913f, 0.1206f, 0.1524f, 0.1863f, 0.2219f, 0.2588f, 0.2965f
};

/**
 * @brief 64-point Radix-2 Cooley-Tukey FFT with a Hann window.
 * @param inReal  64 normalized samples in [-1.0, 1.0].
 * @param outMag  Receives FFT_BINS magnitudes. Bin k covers k * sampleRate / 64 Hz.
 */
inline void computeFFT64(const float* inReal, float* outMag) {
    float real[FFT_SIZE];
    float imag[FFT_SIZE] = {0};

    // Apply Hann window and bit-reversal
    for (int i = 0; i < 64; i++) {
        // 6-bit reversal
        unsigned int j = ((i & 0x01) << 5) | ((i & 0x02) << 3) | ((i & 0x04) << 1) |
                         ((i & 0x08) >> 1) | ((i & 0x10) >> 3) | ((i & 0x20) >> 5);
        real[j] = inReal[i] * HANN_64[i];
    }

    // Cooley-Tukey butterfly stages (6 stages for N=64)
    for (int len = 2; len <= 64; len <<= 1) {
        float angle = -2.0f * (float)M_PI / (float)len;
        float wlenReal = cosf(angle);
        float wlenImag = sinf(angle);

        for (int i = 0; i < 64; i += len) {
            float wReal = 1.0f;
            float wImag = 0.0f;

            for (int j = 0; j < len / 2; j++) {
                int uIdx = i + j;
                int vIdx = i + j + len / 2;

                float uR = real[uIdx];
                float uI = imag[uIdx];
                float vR = real[vIdx] * wReal - imag[vIdx] * wImag;
                float vI = real[vIdx] * wImag + imag[vIdx] * wReal;

                real[uIdx] = uR + vR;
                imag[uIdx] = uI + vI;
                real[vIdx] = uR - vR;
                imag[vIdx] = uI - vI;

                float nextWReal = wReal * wlenReal - wImag * wlenImag;
                float nextWImag = wReal * wlenImag + wImag * wlenReal;
                wReal = nextWReal;
                wImag = nextWImag;
            }
        }
    }

    // Calculate magnitude for first 32 frequency bins
    for (int k = 0; k < 32; k++) {
        outMag[k] = sqrtf(real[k] * real[k] + imag[k] * imag[k]);
    }
}

/**
 * @brief Maps a visualizer band index onto an FFT bin, log-spaced towards the lows.
 *
 * The reference recipe is expressed on a 16-band scale (bins 0..3 map one-to-one so
 * sub-bass and bass each get their own bar, then bins are stretched by two so mids
 * and highs share the remaining bars). Rescaling the band index onto that 16-band
 * axis reproduces the reference mapping exactly for numBands == 16 and generalizes
 * it to any other bar count the WebUI may request.
 */
inline size_t bandToBin(size_t band, size_t numBands) {
    if (numBands == 0) return 0;
    float t = (float)band * 16.0f / (float)numBands;
    float bin = (t < 4.0f) ? t : (4.0f + (t - 4.0f) * 2.0f);
    if (bin < 0.0f) bin = 0.0f;
    size_t idx = (size_t)bin;
    if (idx >= FFT_BINS) idx = FFT_BINS - 1;
    return idx;
}

}  // namespace arcade_audio
