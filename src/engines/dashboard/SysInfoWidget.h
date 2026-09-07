#pragma once
#include <Arduino.h>
class MatrixPanel_I2S_DMA;
#include "DashboardData.h"

/**
 * @class SysInfoWidget
 * @brief System telemetry (CPU, RAM, WiFi RSSI, Uptime) animated presentation widget.
 */
class SysInfoWidget {
public:
    static void render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const SystemData& sys, const DashboardTheme& theme);
};
