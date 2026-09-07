#include "DashboardCommon.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../icons/CryptoStockIcons.h"
#include <glcdfont.c>

int measureText(int len) {
    return (len > 0) ? (len * 6 - 1) : 0;
}

void drawClippedPixel(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color) {
    if (x >= minX && x < maxX && y >= minY && y < maxY) {
        matrix->drawPixel(x, y, color);
    }
}

void drawClippedChar(MatrixPanel_I2S_DMA* matrix, int x, int y, unsigned char c, int minX, int maxX, int minY, int maxY, uint16_t color) {
    if (!matrix) return;
    if (x + 5 < minX || x >= maxX || y + 7 < minY || y >= maxY) return;
    if (c == '`' || c == 0xF7 || c == 0xF8 || (uint8_t)c == 0xB0) c = 248;

    for (int8_t i = 0; i < 5; i++) {
        int px = x + i;
        if (px >= minX && px < maxX) {
            uint8_t line = pgm_read_byte(&font[c * 5 + i]);
            for (int8_t j = 0; j < 8; j++, line >>= 1) {
                if (line & 1) {
                    int py = y + j;
                    if (py >= minY && py < maxY) {
                        matrix->drawPixel(px, py, color);
                    }
                }
            }
        }
    }
}

void drawClippedString(MatrixPanel_I2S_DMA* matrix, const char* text, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color) {
    if (!matrix || !text || text[0] == '\0') return;
    int curX = x;
    while (*text) {
        drawClippedChar(matrix, curX, y, (unsigned char)*text, minX, maxX, minY, maxY, color);
        curX += 6;
        text++;
    }
}

void drawClippedString(MatrixPanel_I2S_DMA* matrix, const String& text, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color) {
    drawClippedString(matrix, text.c_str(), x, y, minX, maxX, minY, maxY, color);
}

void drawMiniWeatherIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const String& iconCode) {
    if (!matrix) return;
    uint16_t sunCol = matrix->color565(255, 200, 0);
    uint16_t moonCol = matrix->color565(240, 240, 180);
    uint16_t cloudCol = matrix->color565(180, 200, 220);
    uint16_t cloudDark = matrix->color565(130, 150, 170);
    uint16_t rainCol = matrix->color565(0, 160, 255);
    uint16_t thunderCol = matrix->color565(255, 240, 0);
    uint16_t snowCol = matrix->color565(230, 245, 255);
    uint16_t mistCol = matrix->color565(160, 180, 200);

    bool isNight = iconCode.endsWith("n");

    if (iconCode.startsWith("01")) {
        // Clear Day (Sun) or Clear Night (Moon)
        if (isNight) {
            drawClippedPixel(matrix, x + 4, y + 1, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 3, y + 2, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 2, y + 3, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 2, y + 4, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 3, y + 5, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 4, y + 6, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 4, y + 2, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 3, y + 3, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 3, y + 4, minX, maxX, minY, maxY, moonCol);
            drawClippedPixel(matrix, x + 4, y + 5, minX, maxX, minY, maxY, moonCol);
        } else {
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    drawClippedPixel(matrix, x + 2 + c, y + 2 + r, minX, maxX, minY, maxY, sunCol);
                }
            }
            drawClippedPixel(matrix, x + 3, y, minX, maxX, minY, maxY, sunCol);
            drawClippedPixel(matrix, x + 4, y, minX, maxX, minY, maxY, sunCol);
            drawClippedPixel(matrix, x + 3, y + 7, minX, maxX, minY, maxY, sunCol);
            drawClippedPixel(matrix, x + 4, y + 7, minX, maxX, minY, maxY, sunCol);
            drawClippedPixel(matrix, x, y + 3, minX, maxX, minY, maxY, sunCol);
            drawClippedPixel(matrix, x, y + 4, minX, maxX, minY, maxY, sunCol);
            drawClippedPixel(matrix, x + 7, y + 3, minX, maxX, minY, maxY, sunCol);
            drawClippedPixel(matrix, x + 7, y + 4, minX, maxX, minY, maxY, sunCol);
        }
    } else if (iconCode.startsWith("02")) {
        // Partly Cloudy
        uint16_t astroCol = isNight ? moonCol : sunCol;
        drawClippedPixel(matrix, x + 1, y, minX, maxX, minY, maxY, astroCol);
        drawClippedPixel(matrix, x + 2, y, minX, maxX, minY, maxY, astroCol);
        drawClippedPixel(matrix, x, y + 1, minX, maxX, minY, maxY, astroCol);
        drawClippedPixel(matrix, x + 1, y + 1, minX, maxX, minY, maxY, astroCol);
        drawClippedPixel(matrix, x + 2, y + 1, minX, maxX, minY, maxY, astroCol);
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 5; c++) {
                drawClippedPixel(matrix, x + 2 + c, y + 3 + r, minX, maxX, minY, maxY, cloudCol);
            }
        }
        drawClippedPixel(matrix, x + 4, y + 2, minX, maxX, minY, maxY, cloudCol);
        drawClippedPixel(matrix, x + 5, y + 2, minX, maxX, minY, maxY, cloudCol);
        drawClippedPixel(matrix, x + 7, y + 4, minX, maxX, minY, maxY, cloudCol);
        drawClippedPixel(matrix, x + 7, y + 5, minX, maxX, minY, maxY, cloudCol);
    } else if (iconCode.startsWith("09") || iconCode.startsWith("10")) {
        // Rain / Showers
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 6; c++) {
                drawClippedPixel(matrix, x + 1 + c, y + 1 + r, minX, maxX, minY, maxY, cloudCol);
            }
        }
        drawClippedPixel(matrix, x + 2, y + 5, minX, maxX, minY, maxY, rainCol);
        drawClippedPixel(matrix, x + 4, y + 6, minX, maxX, minY, maxY, rainCol);
        drawClippedPixel(matrix, x + 6, y + 5, minX, maxX, minY, maxY, rainCol);
    } else if (iconCode.startsWith("11")) {
        // Thunderstorm
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 6; c++) {
                drawClippedPixel(matrix, x + 1 + c, y + 1 + r, minX, maxX, minY, maxY, cloudDark);
            }
        }
        drawClippedPixel(matrix, x + 4, y + 4, minX, maxX, minY, maxY, thunderCol);
        drawClippedPixel(matrix, x + 3, y + 5, minX, maxX, minY, maxY, thunderCol);
        drawClippedPixel(matrix, x + 4, y + 5, minX, maxX, minY, maxY, thunderCol);
        drawClippedPixel(matrix, x + 2, y + 6, minX, maxX, minY, maxY, thunderCol);
        drawClippedPixel(matrix, x + 3, y + 6, minX, maxX, minY, maxY, thunderCol);
        drawClippedPixel(matrix, x + 2, y + 7, minX, maxX, minY, maxY, thunderCol);
    } else if (iconCode.startsWith("13")) {
        // Snow
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 6; c++) {
                drawClippedPixel(matrix, x + 1 + c, y + 1 + r, minX, maxX, minY, maxY, cloudCol);
            }
        }
        drawClippedPixel(matrix, x + 2, y + 5, minX, maxX, minY, maxY, snowCol);
        drawClippedPixel(matrix, x + 4, y + 6, minX, maxX, minY, maxY, snowCol);
        drawClippedPixel(matrix, x + 6, y + 5, minX, maxX, minY, maxY, snowCol);
    } else if (iconCode.startsWith("50")) {
        // Mist / Fog
        for (int c = 0; c < 6; c++) {
            drawClippedPixel(matrix, x + 1 + c, y + 2, minX, maxX, minY, maxY, mistCol);
            drawClippedPixel(matrix, x + c,     y + 4, minX, maxX, minY, maxY, mistCol);
            drawClippedPixel(matrix, x + 2 + c, y + 6, minX, maxX, minY, maxY, mistCol);
        }
    } else {
        // Clouds / Overcast
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 6; c++) {
                drawClippedPixel(matrix, x + 1 + c, y + 2 + r, minX, maxX, minY, maxY, cloudCol);
            }
        }
        drawClippedPixel(matrix, x + 3, y + 1, minX, maxX, minY, maxY, cloudCol);
        drawClippedPixel(matrix, x + 4, y + 1, minX, maxX, minY, maxY, cloudCol);
    }
}

void drawMiniIndoorIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color) {
    if (!matrix) return;
    // Roof
    drawClippedPixel(matrix, x + 3, y, minX, maxX, minY, maxY, color);
    drawClippedPixel(matrix, x + 2, y + 1, minX, maxX, minY, maxY, color);
    drawClippedPixel(matrix, x + 4, y + 1, minX, maxX, minY, maxY, color);
    drawClippedPixel(matrix, x + 1, y + 2, minX, maxX, minY, maxY, color);
    drawClippedPixel(matrix, x + 5, y + 2, minX, maxX, minY, maxY, color);
    // Walls
    for (int r = 3; r <= 6; r++) {
        drawClippedPixel(matrix, x + 1, y + r, minX, maxX, minY, maxY, color);
        drawClippedPixel(matrix, x + 5, y + r, minX, maxX, minY, maxY, color);
    }
    // Floor
    for (int c = 1; c <= 5; c++) {
        drawClippedPixel(matrix, x + c, y + 6, minX, maxX, minY, maxY, color);
    }
}

void drawClippedMarketIcon8x8(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const String& symbol) {
    if (!matrix) return;
    const uint16_t* iconData = nullptr;

    if (symbol.equalsIgnoreCase("BTC")) iconData = ICON_BTC_8x8;
    else if (symbol.equalsIgnoreCase("ETH")) iconData = ICON_ETH_8x8;
    else if (symbol.equalsIgnoreCase("SOL")) iconData = ICON_SOL_8x8;
    else if (symbol.equalsIgnoreCase("AAPL")) iconData = ICON_AAPL_8x8;
    else if (symbol.equalsIgnoreCase("NVDA")) iconData = ICON_NVDA_8x8;
    else if (symbol.equalsIgnoreCase("TSLA")) iconData = ICON_TSLA_8x8;
    else if (symbol.equalsIgnoreCase("MSFT")) iconData = ICON_MSFT_8x8;
    else if (symbol.equalsIgnoreCase("DOGE")) iconData = ICON_DOGE_8x8;

    if (iconData) {
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                uint16_t color = iconData[r * 8 + c];
                if (color != 0) {
                    drawClippedPixel(matrix, x + c, y + r, minX, maxX, minY, maxY, color);
                }
            }
        }
    } else {
        uint16_t borderCol = matrix->color565(255, 180, 0);
        uint16_t fillBg = matrix->color565(30, 20, 5);
        for (int r = 1; r < 7; r++) {
            for (int c = 1; c < 7; c++) {
                drawClippedPixel(matrix, x + c, y + r, minX, maxX, minY, maxY, fillBg);
            }
        }
        for (int c = 2; c < 6; c++) {
            drawClippedPixel(matrix, x + c, y, minX, maxX, minY, maxY, borderCol);
            drawClippedPixel(matrix, x + c, y + 7, minX, maxX, minY, maxY, borderCol);
            drawClippedPixel(matrix, x, y + c, minX, maxX, minY, maxY, borderCol);
            drawClippedPixel(matrix, x + 7, y + c, minX, maxX, minY, maxY, borderCol);
        }
        drawClippedPixel(matrix, x + 1, y + 1, minX, maxX, minY, maxY, borderCol);
        drawClippedPixel(matrix, x + 6, y + 1, minX, maxX, minY, maxY, borderCol);
        drawClippedPixel(matrix, x + 1, y + 6, minX, maxX, minY, maxY, borderCol);
        drawClippedPixel(matrix, x + 6, y + 6, minX, maxX, minY, maxY, borderCol);

        char initial = (symbol.length() > 0) ? symbol[0] : '$';
        drawClippedChar(matrix, x + 1, y, (unsigned char)initial, minX, maxX, minY, maxY, matrix->color565(255, 255, 255));
    }
}

DashboardTheme getDashboardTheme(MatrixPanel_I2S_DMA* matrix, int themeId) {
    DashboardTheme th;
    if (!matrix) return th;

    switch (themeId) {
        case 1: // Amber / Retro HUD
            th.primary   = matrix->color565(255, 180, 0);
            th.secondary = matrix->color565(255, 220, 100);
            th.accent    = matrix->color565(255, 90, 0);
            th.panelBg   = matrix->color565(20, 15, 5);
            th.text      = matrix->color565(255, 240, 200);
            th.textDim   = matrix->color565(120, 80, 20);
            th.border    = matrix->color565(80, 50, 10);
            th.green     = matrix->color565(50, 220, 50);
            th.red       = matrix->color565(255, 60, 60);
            break;

        case 2: // Minimalist Luxury
            th.primary   = matrix->color565(255, 255, 255);
            th.secondary = matrix->color565(180, 220, 255);
            th.accent    = matrix->color565(0, 180, 255);
            th.panelBg   = matrix->color565(5, 10, 20);
            th.text      = matrix->color565(255, 255, 255);
            th.textDim   = matrix->color565(100, 120, 150);
            th.border    = matrix->color565(40, 60, 90);
            th.green     = matrix->color565(0, 230, 100);
            th.red       = matrix->color565(255, 50, 70);
            break;

        case 3: // Matrix Phosphor Green
            th.primary   = matrix->color565(0, 255, 70);
            th.secondary = matrix->color565(0, 200, 50);
            th.accent    = matrix->color565(180, 255, 180);
            th.panelBg   = matrix->color565(0, 20, 5);
            th.text      = matrix->color565(0, 240, 60);
            th.textDim   = matrix->color565(0, 90, 20);
            th.border    = matrix->color565(0, 60, 15);
            th.green     = matrix->color565(0, 255, 70);
            th.red       = matrix->color565(255, 60, 60);
            break;

        case 0: // Cyberpunk Neon (Default)
        default:
            th.primary   = matrix->color565(0, 230, 255);
            th.secondary = matrix->color565(255, 0, 140);
            th.accent    = matrix->color565(0, 255, 120);
            th.panelBg   = matrix->color565(10, 5, 20);
            th.text      = matrix->color565(255, 255, 255);
            th.textDim   = matrix->color565(90, 70, 120);
            th.border    = matrix->color565(60, 20, 80);
            th.green     = matrix->color565(0, 255, 120);
            th.red       = matrix->color565(255, 40, 80);
            break;
    }
    return th;
}
