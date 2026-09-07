#pragma once
#include <Arduino.h>
class MatrixPanel_I2S_DMA;
#include "DashboardData.h"

/**
 * @class ClimateWidget
 * @brief Combined Outdoor Weather + Calibrated Indoor Sensor presentation widget.
 */
class ClimateWidget {
public:
    static void render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const WeatherData& weather, bool weatherValid, const IndoorData& indoor, float tempOffset, const DashboardTheme& theme, bool useFahrenheit, const String& lang = "en");
};
