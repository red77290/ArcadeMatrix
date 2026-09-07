#pragma once
#include <Arduino.h>
class MatrixPanel_I2S_DMA;
#include "DashboardData.h"

int measureText(int len);

void drawClippedPixel(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawClippedChar(MatrixPanel_I2S_DMA* matrix, int x, int y, unsigned char c, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawClippedString(MatrixPanel_I2S_DMA* matrix, const char* text, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawClippedString(MatrixPanel_I2S_DMA* matrix, const String& text, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawMiniWeatherIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const String& iconCode);

void drawMiniIndoorIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawClippedMarketIcon8x8(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const String& symbol);

DashboardTheme getDashboardTheme(MatrixPanel_I2S_DMA* matrix, int themeId);
