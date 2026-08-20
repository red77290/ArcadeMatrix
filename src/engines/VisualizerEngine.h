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
#include "../../include/core/EngineContract.h"

class VisualizerEngine : public IEngine {
public:
    VisualizerEngine();
    ~VisualizerEngine();

    EngineError initialize(EngineContext* context, const EngineConfig* engineConfig) override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void activate() override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* engineConfig) override;

    bool isActive() const { return active; }

    /**
     * @brief Sets the visual display mode ("spectrum", "waveform", "radial", "neon_fire").
     */
    void setMode(const String& modeStr);



private:
    bool active;
    VisualizerMode currentMode;

    float peakHold[128];
    uint32_t lastPeakDecay;

    void drawSpectrum(MatrixPanel_I2S_DMA* matrix);
    void drawWaveform(MatrixPanel_I2S_DMA* matrix);
    void drawRadial(MatrixPanel_I2S_DMA* matrix);
    void drawNeonFire(MatrixPanel_I2S_DMA* matrix);

    uint16_t getSpectrumColor(int heightIndex, int maxHeight);
};

