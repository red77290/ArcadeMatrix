#pragma once
#include <Arduino.h>
class IDrawingSurface;
#include "DashboardData.h"

/**
 * @class PixelClockWidget
 * @brief Handcrafted Octagonal Analog Watch Face & Retro Digital HUD widget.
 */
class PixelClockWidget {
public:
    static void renderAnalog(IDrawingSurface* matrix, const Rect& rect, const DashboardTimeData& time, float subSecond, const DashboardTheme& theme, bool showSeconds, bool showDate);
    static void renderDigital(IDrawingSurface* matrix, const Rect& rect, const DashboardTimeData& time, const DashboardTheme& theme, bool showSeconds, bool showDate, const String& city = "PARIS", bool format24h = true);
};
