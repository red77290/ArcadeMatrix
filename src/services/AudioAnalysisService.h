#pragma once
#include <Arduino.h>
#include <mutex>

/**
 * @struct AudioVisualizerState
 * @brief Frequency spectrum analysis and volume envelope state.
 */
struct AudioVisualizerState {
    float bands[16] = {0.0f};
    float rms = 0.0f;
    float peak = 0.0f;
    uint32_t lastUpdateMs = 0;
};

/**
 * @class AudioAnalysisService
 * @brief Computes FFT frequency spectrum, RMS and peak energy from audio streams.
 * Shared between MusicEngine and VisualizerEngine.
 */
class AudioAnalysisService {
public:
    AudioAnalysisService();
    ~AudioAnalysisService() = default;

    /**
     * @brief Ingests PCM samples from active audio streams to compute FFT & envelope.
     */
    void processSamples(const int16_t* samples, size_t numSamples);

    /**
     * @brief Returns an immutable thread-safe snapshot of the current visualizer state.
     */
    AudioVisualizerState getVisualizerStateSnapshot();

    /**
     * @brief Resets spectrum bands and envelope to zero (silence).
     */
    void reset();

private:
    std::mutex _mutex;
    AudioVisualizerState _state;
};

extern AudioAnalysisService audioAnalysisService;
