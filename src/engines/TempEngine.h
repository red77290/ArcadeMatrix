#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../hal/HardwareHAL.h"

/**
 * @class TempEngine
 * @brief Dynamic and adaptive rendering engine for Temperature & Humidity display.
 */
#include "../../include/core/EngineContract.h"

class TempEngine : public IEngine {
public:
    TempEngine();
    ~TempEngine();

    EngineError initialize(EngineContext* context, const EngineConfig* engineConfig) override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void activate() override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* engineConfig) override;

    void setUnit(const String& unitStr) {
        useFahrenheit = (unitStr.equalsIgnoreCase("F"));
    }

private:
    bool useFahrenheit;
    float tempOffset;
    int offsetX;
    int offsetY;

    void drawThermometerIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, uint16_t color);
    void drawWaterDropIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, uint16_t color);
    uint16_t getTemperatureColor(MatrixPanel_I2S_DMA* matrix, float tempC);
};

class TempEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};


