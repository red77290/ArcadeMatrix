#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../include/core/EngineContract.h"
#include "../hal/HardwareHAL.h"

/**
 * @class SysInfoEngine
 * @brief Dynamic hardware monitor rendering CPU, RAM, Temperature, and Uptime with color grading.
 */
class SysInfoEngine : public IEngine {
public:
    SysInfoEngine();
    ~SysInfoEngine() override;

    EngineError initialize(EngineContext* context, const EngineConfig* engineConfig) override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void activate() override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* engineConfig) override;

    static uint16_t getMetricColor(MatrixPanel_I2S_DMA* matrix, float val, float warnThresh, float critThresh);

private:
    int theme;
    bool showCpu;
    bool showRam;
    bool showTemp;
    bool showUptime;
    bool useFahrenheit;
    int offsetX;
    int offsetY;

    // Simulated / tracked CPU load
    float smoothedCpuLoad;
    uint32_t lastCpuSampleTime;

    void drawGaugeBar(MatrixPanel_I2S_DMA* matrix, int x, int y, int w, int h, float percent, uint16_t color);
    void renderHudTheme(MatrixPanel_I2S_DMA* matrix, float cpuPct, float ramPct, float tempC, uint32_t uptimeSec);
    void renderCyberpunkTheme(MatrixPanel_I2S_DMA* matrix, float cpuPct, float ramPct, float tempC, uint32_t uptimeSec);
    void renderCompactTheme(MatrixPanel_I2S_DMA* matrix, float cpuPct, float ramPct, float tempC, uint32_t uptimeSec);
};

class SysInfoEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
