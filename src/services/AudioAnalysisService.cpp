#include "AudioAnalysisService.h"
#include <math.h>

AudioAnalysisService audioAnalysisService;

AudioAnalysisService::AudioAnalysisService() {
    reset();
}

void AudioAnalysisService::reset() {
    std::lock_guard<std::mutex> lock(_mutex);
    for (int i = 0; i < 16; i++) {
        _state.bands[i] = 0.0f;
    }
    _state.rms = 0.0f;
    _state.peak = 0.0f;
    _state.lastUpdateMs = millis();
}

static const float HANN_64[64] = {
    0.0000f, 0.0024f, 0.0096f, 0.0215f, 0.0381f, 0.0588f, 0.0834f, 0.1114f,
    0.1424f, 0.1760f, 0.2117f, 0.2489f, 0.2872f, 0.3259f, 0.3646f, 0.4026f,
    0.4394f, 0.4746f, 0.5076f, 0.5379f, 0.5652f, 0.5891f, 0.6092f, 0.6253f,
    0.6372f, 0.6446f, 0.6475f, 0.6457f, 0.6393f, 0.6284f, 0.6130f, 0.5934f,
    0.5700f, 0.5431f, 0.5132f, 0.4807f, 0.4460f, 0.4098f, 0.3725f, 0.3346f,
    0.2965f, 0.2588f, 0.2219f, 0.1863f, 0.1524f, 0.1206f, 0.0913f, 0.0650f,
    0.0420f, 0.0230f, 0.0084f, 0.0000f, 0.0000f, 0.0084f, 0.0230f, 0.0420f,
    0.0650f, 0.0913f, 0.1206f, 0.1524f, 0.1863f, 0.2219f, 0.2588f, 0.2965f
};

// 64-point Radix-2 Real-to-Complex FFT
static void computeFFT64(const float* inReal, float* outMag) {
    float real[64];
    float imag[64] = {0};

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

void AudioAnalysisService::processSamples(const int16_t* samples, size_t numSamples) {
    if (!samples || numSamples == 0) return;

    double sumSquares = 0.0;
    int16_t maxPeak = 0;

    // First pass: RMS and Peak
    for (size_t i = 0; i < numSamples; i++) {
        int16_t s = samples[i];
        sumSquares += ((double)s * (double)s);
        int16_t absS = s < 0 ? -s : s;
        if (absS > maxPeak) maxPeak = absS;
    }

    float rms = (float)sqrt(sumSquares / (double)numSamples) / 32768.0f;
    float peak = (float)maxPeak / 32768.0f;

    // Second pass: 64-point FFT
    float fftIn[64];
    float fftMag[32];
    size_t take = min(numSamples, (size_t)64);
    for (size_t i = 0; i < take; i++) {
        fftIn[i] = (float)samples[i] / 32768.0f;
    }
    for (size_t i = take; i < 64; i++) {
        fftIn[i] = 0.0f;
    }

    computeFFT64(fftIn, fftMag);

    std::lock_guard<std::mutex> lock(_mutex);
    _state.rms = rms;
    _state.peak = peak;
    _state.lastUpdateMs = millis();

    // Map 32 raw FFT magnitude bins into 16 log-spaced visualizer bands
    // Bins 0..1 (Sub-bass), 2..3 (Bass), 4..7 (Mids), 8..15 (Highs), 16..31 (Presence)
    for (int b = 0; b < 16; b++) {
        int binIdx = (b < 4) ? b : (4 + (b - 4) * 2);
        if (binIdx >= 32) binIdx = 31;
        float rawVal = (fftMag[binIdx] * 4.0f);
        if (rawVal > 1.0f) rawVal = 1.0f;
        // Smooth exponential moving average filter
        _state.bands[b] = (_state.bands[b] * 0.65f) + (rawVal * 0.35f);
    }
}

AudioVisualizerState AudioAnalysisService::getVisualizerStateSnapshot() {
    std::lock_guard<std::mutex> lock(_mutex);
    // If no samples received for > 300ms, decay naturally to silence
    uint32_t now = millis();
    if (now - _state.lastUpdateMs > 300) {
        for (int i = 0; i < 16; i++) {
            _state.bands[i] *= 0.8f;
        }
        _state.rms *= 0.8f;
        _state.peak *= 0.8f;
    }
    return _state;
}
