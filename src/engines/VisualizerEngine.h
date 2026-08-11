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
 * @brief Moteur du visualiseur de musique rhythmique (prioritaire sur la roue de rotation).
 */
class VisualizerEngine {
public:
    explicit VisualizerEngine(MatrixPanel_I2S_DMA* display);
    ~VisualizerEngine();

    /**
     * @brief Démarre l'échantillonnage audio et active le visualiseur.
     */
    void start();

    /**
     * @brief Arrête le visualiseur et suspend l'échantillonnage I2S.
     */
    void stop();

    /**
     * @brief Indique si le visualiseur est actuellement actif.
     */
    bool isActive() const { return active; }

    /**
     * @brief Définit le mode d'affichage visuel ("spectrum", "waveform", "radial", "neon_fire").
     */
    void setMode(const String& modeStr);

    /**
     * @brief Effectue le rendu d'une frame du visualiseur.
     * @return true si la frame a été dessinée
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
