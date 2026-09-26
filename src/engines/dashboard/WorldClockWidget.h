#pragma once
#include <Arduino.h>
#include <vector>
class IDrawingSurface;
#include "DashboardData.h"

/**
 * @class WorldClockWidget
 * @brief Secondary world timezone clocks renderer.
 */
class WorldClockWidget {
public:
    static void render(IDrawingSurface* matrix, const Rect& rect, const std::vector<WorldTimeItem>& worldTimes, const DashboardTheme& theme);
};
