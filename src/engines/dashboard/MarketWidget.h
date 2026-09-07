#pragma once
#include <Arduino.h>
#include <vector>
class MatrixPanel_I2S_DMA;
#include "DashboardData.h"

/**
 * @class MarketWidget
 * @brief Crypto & Stock Quotes Infinite Fluid Rolling Ticker widget.
 */
class MarketWidget {
public:
    static void render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const std::vector<MarketItem>& items, const DashboardTheme& theme);
};
