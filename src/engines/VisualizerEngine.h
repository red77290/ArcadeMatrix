#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../hal/HardwareHAL.h"

enum VisualizerMode {
    VISUALIZER_SPECTRUM,
    VISUALIZER_WAVEFORM,
    VISUALIZER_RADIAL,
    VISUALIZER_NEON_FIRE
};

/**
 * @class VisualizerEngine
 * @brief Rhythmic music visualizer engine (takes priority over the rotation loop).
 */
class VisualizerEngine {
public:
    explicit VisualizerEngine(MatrixPanel_I2S_DMA* display);
    ~VisualizerEngine();

    /**
     * @brief Starts audio sampling and activates the visualizer.
     */
    void start();

    /**
     * @brief Stops the visualizer and suspends I2S sampling.
     */
    void stop();

    /**
     * @brief Indicates whether the visualizer is currently active.
     */
    bool isActive() const { return active; }

    /**
     * @brief Sets the visual display mode ("spectrum", "waveform", "radial", "neon_fire").
     */
    void setMode(const String& modeStr);

    /**
     * @brief Renders a single frame of the visualizer.
     * @return true if the frame was drawn
     */
    bool loop();

private:
    MatrixPanel_I2S_DMA* matrix;
    bool active;
    VisualizerMode currentMode;

    float peakHold[128];
    uint32_t lastPeakDecay;

    void drawSpectrum();
    void drawWaveform();
    void drawRadial();
    void drawNeonFire();

    uint16_t getSpectrumColor(int heightIndex, int maxHeight);
};

