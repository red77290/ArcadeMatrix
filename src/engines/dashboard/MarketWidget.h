#pragma once
#include <Arduino.h>
#include <vector>
class IDrawingSurface;
#include "DashboardData.h"

/**
 * @class MarketWidget
 * @brief Crypto & Stock Quotes Infinite Fluid Rolling Ticker widget.
 */
class MarketWidget {
public:
    static void render(IDrawingSurface* matrix, const Rect& rect, const std::vector<MarketItem>& items, const DashboardTheme& theme);
};
