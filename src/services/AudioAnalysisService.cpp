#include "AudioAnalysisService.h"
#include <math.h>
#include "FFT64.h"

using arcade_audio::computeFFT64;

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
