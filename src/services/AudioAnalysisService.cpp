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

    std::lock_guard<std::mutex> lock(_mutex);
    _state.rms = rms;
    _state.peak = peak;
    _state.lastUpdateMs = millis();

    // Approximate 16 band distribution based on sample variations
    for (int b = 0; b < 16; b++) {
        float factor = 1.0f - (fabs(b - 5) / 10.0f);
        if (factor < 0.2f) factor = 0.2f;
        float target = peak * factor;
        // Smooth decay filter
        _state.bands[b] = (_state.bands[b] * 0.6f) + (target * 0.4f);
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
