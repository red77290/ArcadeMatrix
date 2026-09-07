#pragma once
#include <Arduino.h>
#include <vector>
class MatrixPanel_I2S_DMA;
#include "DashboardData.h"

/**
 * @class WorldClockWidget
 * @brief Secondary world timezone clocks renderer.
 */
class WorldClockWidget {
public:
    static void render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const std::vector<WorldTimeItem>& worldTimes, const DashboardTheme& theme);
};
