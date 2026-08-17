#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../hal/HardwareHAL.h"

enum NoiseStatusLevel {
    NOISE_CALM,       ///< < 45 dB (😊 Calm)
    NOISE_NORMAL,     ///< 45-65 dB (🙂 Normal)
    NOISE_MODERATE,   ///< 65-75 dB (😐 Moderate)
    NOISE_VIGILANCE,  ///< 75-83 dB (⚠️ Caution)
    NOISE_LIMIT,      ///< 83-88 dB (🙁 High)
    NOISE_ALERT       ///< > 88 dB (🚨 Alert)
};

/**
 * @class DecibelEngine
 * @brief Decibel meter engine with Pixel Art smileys, VS Fighting health gauge, and on-demand sampling.
 */
class DecibelEngine {
public:
    explicit DecibelEngine(MatrixPanel_I2S_DMA* display);
    ~DecibelEngine();

    /**
     * @brief Called when the engine becomes active in the rotation loop (Lazy Sampling).
     */
    void onActivate();

    /**
     * @brief Called when the engine leaves the screen.
     */
    void onDeactivate();

    /**
     * @brief Indicates whether the module is currently active.
     */
    bool isActive() const { return active; }

    /**
     * @brief Renders a single frame of the decibel meter.
     * @return true if the frame was drawn
     */
    bool loop();

private:
    MatrixPanel_I2S_DMA* matrix;
    bool active;

    float currentDb;
    NoiseStatusLevel currentLevel;

    void updateStatusLevel(float db);
    void drawSmileyIcon(int x, int y, NoiseStatusLevel level);
    void drawVsGauge(float db);
    uint16_t getGaugeColorForDb(float dbVal);
    uint16_t getLevelColor(NoiseStatusLevel level);
    const char* getLevelText(NoiseStatusLevel level);
};

