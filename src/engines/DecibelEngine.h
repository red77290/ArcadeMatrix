#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../hal/HardwareHAL.h"

enum NoiseStatusLevel {
    NOISE_CALM,       ///< < 45 dB (😊 Calme)
    NOISE_NORMAL,     ///< 45-65 dB (🙂 Normal)
    NOISE_MODERATE,   ///< 65-75 dB (😐 Modéré)
    NOISE_VIGILANCE,  ///< 75-83 dB (⚠️ Vigilance)
    NOISE_LIMIT,      ///< 83-88 dB (🙁 Limite)
    NOISE_ALERT       ///< > 88 dB (🚨 Alerte)
};

/**
 * @class DecibelEngine
 * @brief Moteur du décibelmètre avec smileys Pixel Art, jauge VS Fighting et activation à la demande.
 */
class DecibelEngine {
public:
    explicit DecibelEngine(MatrixPanel_I2S_DMA* display);
    ~DecibelEngine();

    /**
     * @brief Appelé lorsque le module devient actif dans la roue (Lazy Sampling).
     */
    void onActivate();

    /**
     * @brief Appelé lorsque le module quitte l'écran.
     */
    void onDeactivate();

    /**
     * @brief Indique si le module est actif.
     */
    bool isActive() const { return active; }

    /**
     * @brief Effectue le rendu d'une frame du décibelmètre.
     * @return true si l'affichage a été fait
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
