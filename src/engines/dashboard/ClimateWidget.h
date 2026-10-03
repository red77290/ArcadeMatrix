#pragma once
#include <Arduino.h>
class IDrawingSurface;
#include "DashboardData.h"

/**
 * @class ClimateWidget
 * @brief Combined Outdoor Weather + Calibrated Indoor Sensor presentation widget.
 */
class ClimateWidget {
public:
    static void render(IDrawingSurface* matrix, const Rect& rect, const WeatherData& weather, bool weatherValid, const IndoorData& indoor, float tempOffset, const DashboardTheme& theme, bool useFahrenheit, const String& lang = "en");
};
