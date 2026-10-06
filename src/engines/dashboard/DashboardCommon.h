#pragma once
#include <Arduino.h>
class IDrawingSurface;
#include "DashboardData.h"

int measureText(int len);

void drawClippedPixel(IDrawingSurface* matrix, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawClippedChar(IDrawingSurface* matrix, int x, int y, unsigned char c, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawClippedString(IDrawingSurface* matrix, const char* text, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawClippedString(IDrawingSurface* matrix, const String& text, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawMiniWeatherIcon(IDrawingSurface* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const String& iconCode);

void drawMiniIndoorIcon(IDrawingSurface* matrix, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color);

void drawClippedMarketIcon8x8(IDrawingSurface* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const String& symbol);

DashboardTheme getDashboardTheme(IDrawingSurface* matrix, int themeId);
