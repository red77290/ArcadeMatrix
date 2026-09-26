#pragma once
#include <Arduino.h>
class IDrawingSurface;
#include "DashboardData.h"

/**
 * @class SysInfoWidget
 * @brief System telemetry (CPU, RAM, WiFi RSSI, Uptime) animated presentation widget.
 */
class SysInfoWidget {
public:
    static void render(IDrawingSurface* matrix, const Rect& rect, const SystemData& sys, const DashboardTheme& theme);
};
