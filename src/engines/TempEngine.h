#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../hal/HardwareHAL.h"

/**
 * @class TempEngine
 * @brief Dynamic and adaptive rendering engine for Temperature & Humidity display.
 */
class TempEngine {
public:
    explicit TempEngine(MatrixPanel_I2S_DMA* display);
    ~TempEngine();

    /**
     * @brief Renders a single Temperature/Humidity frame onto the matrix.
     * @return true if the frame was drawn
     */
    bool loop();

    /**
     * @brief Forces the temperature unit ('C' or 'F').
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

