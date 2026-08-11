#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../hal/HardwareHAL.h"

/**
 * @class TempEngine
 * @brief Moteur d'affichage dynamique et adaptatif pour la Température & Humidité.
 */
class TempEngine {
public:
    explicit TempEngine(MatrixPanel_I2S_DMA* display);
    ~TempEngine();

    /**
     * @brief Effectue le rendu d'une frame de Température/Humidité sur la matrice.
     * @return true si l'affichage a pu être effectué
     */
    bool loop();

    /**
     * @brief Force l'unité de température ('C' ou 'F').
     */
    void setUnit(const String& unitStr) {
        useFahrenheit = (unitStr.equalsIgnoreCase("F"));
    }

private:
    MatrixPanel_I2S_DMA* matrix;
    bool useFahrenheit;

    void drawThermometerIcon(int x, int y, uint16_t color);
    void drawWaterDropIcon(int x, int y, uint16_t color);
    uint16_t getTemperatureColor(float tempC);
};
