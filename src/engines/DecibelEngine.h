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
#include "../../include/core/EngineContract.h"

class DecibelEngine : public IEngine {
public:
    DecibelEngine();
    ~DecibelEngine();

    EngineError initialize(EngineContext* context, const EngineConfig* engineConfig) override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void activate() override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* engineConfig) override;

    // Removed onActivate, onDeactivate, isActive, loop

private:
    bool active;

    float currentDb;
    NoiseStatusLevel currentLevel;

    void updateStatusLevel(float db);
    void drawSmileyIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, NoiseStatusLevel level);
    void drawVsGauge(MatrixPanel_I2S_DMA* matrix, float db);
    uint16_t getGaugeColorForDb(MatrixPanel_I2S_DMA* matrix, float dbVal);
    uint16_t getLevelColor(MatrixPanel_I2S_DMA* matrix, NoiseStatusLevel level);
    const char* getLevelText(NoiseStatusLevel level);
};

