#include "DashboardEngine.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "icons/CryptoStockIcons.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"
#include "../core/I18n.h"
#include "../api/OpenWeatherMapProvider.h"
#include "../api/YahooFinanceProvider.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// ============================================================================
// Layout Calculator Implementation (Dual-Orientation Responsive Geometry)
// ============================================================================

DashboardLayout DashboardLayoutCalculator::calculate(const DisplayGeometry& geometry, const DashboardConfigParams& config) {
    DashboardLayout l;
    int w = geometry.width;
    int h = geometry.height;

    bool isTate = (geometry.layoutClass == LayoutClass::PORTRAIT || geometry.layoutClass == LayoutClass::TALL || w < 48 || h > (w * 3) / 2);
    bool isWide = (geometry.layoutClass == LayoutClass::WIDE || w >= 128);
    bool isSquare = (w == h || (w >= 48 && h >= 48 && abs(w - h) <= 16));

    if (isTate) {
        // ====================================================================
        // PORTRAIT TOWER LAYOUT (e.g. 64x256, 32x128, 64x128)
        // ====================================================================
        l.isVerticalTower = true;
        int curY = 0;

        if (h >= 240) {
            // Full 64x256 Tall Tower
            if (config.showClock) {
                l.hasClock = true;
                l.clockRect = Rect(0, (int16_t)curY, (uint16_t)w, 64);
                curY += 65;
            }
            if (config.showWorldClock) {
                l.hasWorldClock = true;
                l.worldClockRect = Rect(0, (int16_t)curY, (uint16_t)w, 44);
                curY += 45;
            }
            if (config.showWeather || config.showIndoorTemp) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)curY, (uint16_t)w, 54);
                curY += 55;
            }
            if (config.showMarkets && curY < h) {
                l.hasMarket = true;
                int remH = h - curY;
                l.marketRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(config.showSysInfo ? (remH - 16) : remH));
                curY += l.marketRect.height + 1;
            }
            if (config.showSysInfo && curY < h) {
                l.hasSysInfo = true;
                l.sysInfoRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(h - curY));
            }
        } else if (h >= 120) {
            // Medium 64x128 / 32x128 Tower
            if (config.showClock) {
                l.hasClock = true;
                l.clockRect = Rect(0, (int16_t)curY, (uint16_t)w, 48);
                curY += 49;
            }
            if (config.showWeather || config.showIndoorTemp) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)curY, (uint16_t)w, 36);
                curY += 37;
            }
            if (config.showMarkets && curY < h) {
                l.hasMarket = true;
                l.marketRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(h - curY));
            } else if (config.showWorldClock && curY < h) {
                l.hasWorldClock = true;
                l.worldClockRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(h - curY));
            }
        } else {
            // Small 32x64 / 64x64 Tower
            if (config.showClock) {
                l.hasClock = true;
                l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)(h / 2));
            }
            if (config.showWeather || config.showIndoorTemp) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)(h / 2 + 1), (uint16_t)w, (uint16_t)(h - (h / 2) - 1));
            }
        }
    } else if (isWide) {
        // ====================================================================
        // WIDESCREEN DESK CLOCK LAYOUT (e.g. 256x64, 192x64, 128x64, 128x32)
        // ====================================================================
        l.isHorizontalDeck = true;

        bool hasTopWidgets = config.showWorldClock || config.showWeather || config.showIndoorTemp || config.showSysInfo;
        bool hasBotWidgets = config.showMarkets;
        bool hasAnyContent = hasTopWidgets || hasBotWidgets;

        int contentX = 0;
        int contentW = w;

        // 1. Clock Placement & Auto-Expansion
        if (config.showClock) {
            l.hasClock = true;
            if (!hasAnyContent) {
                // Clock is the only active widget -> takes 100% full screen
                l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
                return l;
            } else {
                // Clock occupies left column
                int clockW = min(h, (w >= 200) ? 64 : (w / 3));
                l.clockRect = Rect(0, 0, (uint16_t)clockW, (uint16_t)h);
                contentX = clockW + 2;
                contentW = w - contentX;
            }
        }

        // 2. Right Content Area (Top Row & Bottom Row Auto-Scaling)
        if (hasTopWidgets && hasBotWidgets) {
            // Dual Row Split
            int topH = (h / 2) - 1;
            int botY = topH + 2;
            int botH = h - botY;

            int topCount = (config.showWorldClock ? 1 : 0) + 
                           ((config.showWeather || config.showIndoorTemp) ? 1 : 0) + 
                           (config.showSysInfo ? 1 : 0);
            int curTopX = contentX;
            int remainingW = contentW;
            int widgetsLeft = topCount;

            if (config.showWorldClock) {
                l.hasWorldClock = true;
                int slotW = (widgetsLeft == 1) ? remainingW : (remainingW / widgetsLeft);
                if (topCount == 3) slotW = remainingW * 35 / 100;
                l.worldClockRect = Rect((int16_t)curTopX, 0, (uint16_t)slotW, (uint16_t)topH);
                curTopX += slotW + 2;
                remainingW -= (slotW + 2);
                widgetsLeft--;
            }
            if (config.showWeather || config.showIndoorTemp) {
                l.hasClimate = true;
                int slotW = (widgetsLeft == 1) ? remainingW : (remainingW / widgetsLeft);
                l.climateRect = Rect((int16_t)curTopX, 0, (uint16_t)slotW, (uint16_t)topH);
                curTopX += slotW + 2;
                remainingW -= (slotW + 2);
                widgetsLeft--;
            }
            if (config.showSysInfo && widgetsLeft > 0) {
                l.hasSysInfo = true;
                l.sysInfoRect = Rect((int16_t)curTopX, 0, (uint16_t)max(20, remainingW), (uint16_t)topH);
            }

            // Bottom Row (Markets)
            l.hasMarket = true;
            l.marketRect = Rect((int16_t)contentX, (int16_t)botY, (uint16_t)contentW, (uint16_t)botH);

        } else if (hasTopWidgets && !hasBotWidgets) {
            // Only Top Widgets -> Expand to Full Height h
            int topCount = (config.showWorldClock ? 1 : 0) + 
                           ((config.showWeather || config.showIndoorTemp) ? 1 : 0) + 
                           (config.showSysInfo ? 1 : 0);
            int curTopX = contentX;
            int remainingW = contentW;
            int widgetsLeft = topCount;

            if (config.showWorldClock) {
                l.hasWorldClock = true;
                int slotW = (widgetsLeft == 1) ? remainingW : (remainingW / widgetsLeft);
                if (topCount == 3) slotW = remainingW * 35 / 100;
                l.worldClockRect = Rect((int16_t)curTopX, 0, (uint16_t)slotW, (uint16_t)h);
                curTopX += slotW + 2;
                remainingW -= (slotW + 2);
                widgetsLeft--;
            }
            if (config.showWeather || config.showIndoorTemp) {
                l.hasClimate = true;
                int slotW = (widgetsLeft == 1) ? remainingW : (remainingW / widgetsLeft);
                l.climateRect = Rect((int16_t)curTopX, 0, (uint16_t)slotW, (uint16_t)h);
                curTopX += slotW + 2;
                remainingW -= (slotW + 2);
                widgetsLeft--;
            }
            if (config.showSysInfo && widgetsLeft > 0) {
                l.hasSysInfo = true;
                l.sysInfoRect = Rect((int16_t)curTopX, 0, (uint16_t)max(20, remainingW), (uint16_t)h);
            }

        } else if (!hasTopWidgets && hasBotWidgets) {
            // Only Market Ticker -> Expands to Full Height h
            l.hasMarket = true;
            l.marketRect = Rect((int16_t)contentX, 0, (uint16_t)contentW, (uint16_t)h);
        }

    } else if (isSquare) {
        // ====================================================================
        // SQUARE MATRIX (64x64, 32x32)
        // ====================================================================
        bool hasClim = config.showWeather || config.showIndoorTemp;
        bool hasMkt = config.showMarkets;
        bool hasWc = config.showWorldClock;
        bool hasAnyContent = hasClim || hasMkt || hasWc;

        if (config.showClock && !hasAnyContent) {
            l.hasClock = true;
            l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
        } else if (config.showClock && hasAnyContent) {
            int topH = h * 55 / 100; // Clock takes upper 55%
            int botY = topH + 1;
            int botH = h - botY;

            l.hasClock = true;
            l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)topH);

            if (hasClim && (hasMkt || hasWc)) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)botY, (uint16_t)(w / 2 - 1), (uint16_t)botH);
                if (hasMkt) {
                    l.hasMarket = true;
                    l.marketRect = Rect((int16_t)(w / 2), (int16_t)botY, (uint16_t)(w - w / 2), (uint16_t)botH);
                } else {
                    l.hasWorldClock = true;
                    l.worldClockRect = Rect((int16_t)(w / 2), (int16_t)botY, (uint16_t)(w - w / 2), (uint16_t)botH);
                }
            } else if (hasClim) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)botY, (uint16_t)w, (uint16_t)botH);
            } else if (hasMkt) {
                l.hasMarket = true;
                l.marketRect = Rect(0, (int16_t)botY, (uint16_t)w, (uint16_t)botH);
            } else if (hasWc) {
                l.hasWorldClock = true;
                l.worldClockRect = Rect(0, (int16_t)botY, (uint16_t)w, (uint16_t)botH);
            }
        } else if (!config.showClock && hasAnyContent) {
            int halfH = h / 2;
            if (hasClim && hasMkt) {
                l.hasClimate = true;
                l.climateRect = Rect(0, 0, (uint16_t)w, (uint16_t)(halfH - 1));
                l.hasMarket = true;
                l.marketRect = Rect(0, (int16_t)(halfH + 1), (uint16_t)w, (uint16_t)(h - halfH - 1));
            } else if (hasClim) {
                l.hasClimate = true;
                l.climateRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
            } else if (hasMkt) {
                l.hasMarket = true;
                l.marketRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
            }
        }

    } else {
        // ====================================================================
        // STANDARD COMPACT 64x32
        // ====================================================================
        bool hasClim = config.showWeather || config.showIndoorTemp;
        bool hasMkt = config.showMarkets;
        bool hasAnyContent = hasClim || hasMkt;

        if (config.showClock && !hasAnyContent) {
            l.hasClock = true;
            l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
        } else if (!config.showClock && hasAnyContent) {
            if (hasClim) {
                l.hasClimate = true;
                l.climateRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
            } else if (hasMkt) {
                l.hasMarket = true;
                l.marketRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
            }
        } else {
            int leftW = w / 2;
            l.hasClock = true;
            l.clockRect = Rect(0, 0, (uint16_t)leftW, (uint16_t)h);
            if (hasClim) {
                l.hasClimate = true;
                l.climateRect = Rect((int16_t)(leftW + 1), 0, (uint16_t)(w - leftW - 1), (uint16_t)h);
            }
        }
    }

    return l;
}

// ============================================================================
// Pixel Clock Widget (Handcrafted Octagonal Watch Face & Retro Digital HUD)
// ============================================================================

void PixelClockWidget::renderAnalog(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const DashboardTimeData& time, float subSecond, const DashboardTheme& theme, bool showSeconds, bool showDate) {
    if (!matrix || rect.width < 14 || rect.height < 14) return;

    int cx = rect.x + rect.width / 2;
    int cy = rect.y + rect.height / 2;
    int radius = (min(rect.width, rect.height) / 2) - 1;
    if (radius < 6) radius = 6;

    // 1. Draw 3D Octagonal Pixel Bezel
    int s = radius * 41 / 100;
    int r = radius;

    // Top & Left Bezel
    matrix->drawLine(cx - s, cy - r, cx + s, cy - r, theme.border);
    matrix->drawLine(cx - r, cy - s, cx - s, cy - r, theme.accent);
    matrix->drawLine(cx - r, cy - s, cx - r, cy + s, theme.accent);
    matrix->drawLine(cx - r, cy + s, cx - s, cy + r, theme.border);

    // Bottom & Right Bezel
    matrix->drawLine(cx - s, cy + r, cx + s, cy + r, theme.border);
    matrix->drawLine(cx + s, cy + r, cx + r, cy + s, theme.panelBg);
    matrix->drawLine(cx + r, cy - s, cx + r, cy + s, theme.panelBg);
    matrix->drawLine(cx + s, cy - r, cx + r, cy - s, theme.border);

    // 2. 12 Golden Hour Pips
    for (int i = 0; i < 12; i++) {
        float angle = (i * 30.0f) * (PI / 180.0f) - (PI / 2.0f);
        float cosA = cosf(angle);
        float sinA = sinf(angle);

        bool isCardinal = (i % 3 == 0);
        int rOuter = radius - 2;
        int rInner = isCardinal ? max(1, radius - 4) : max(1, radius - 3);

        int x1 = cx + (int)(rOuter * cosA);
        int y1 = cy + (int)(rOuter * sinA);
        int x2 = cx + (int)(rInner * cosA);
        int y2 = cy + (int)(rInner * sinA);

        uint16_t pipColor = isCardinal ? theme.primary : theme.textDim;
        if (radius >= 14 && isCardinal) {
            matrix->drawLine(x1, y1, x2, y2, pipColor);
        } else {
            matrix->drawPixel(x1, y1, pipColor);
        }
    }

    // 3. Hour Hand
    float hourAngle = ((time.hours % 12 + time.minutes / 60.0f) / 12.0f) * 2.0f * PI - (PI / 2.0f);
    int hourLen = max(3, (int)(radius * 0.50f));
    int hx = cx + (int)(hourLen * cosf(hourAngle));
    int hy = cy + (int)(hourLen * sinf(hourAngle));
    matrix->drawLine(cx, cy, hx, hy, theme.text);
    matrix->drawLine(cx + 1, cy, hx + 1, hy, theme.text);

    // 4. Minute Hand
    float minAngle = ((time.minutes + time.seconds / 60.0f) / 60.0f) * 2.0f * PI - (PI / 2.0f);
    int minLen = max(4, (int)(radius * 0.78f));
    int mx = cx + (int)(minLen * cosf(minAngle));
    int my = cy + (int)(minLen * sinf(minAngle));
    matrix->drawLine(cx, cy, mx, my, theme.secondary);

    // 5. Sweeping Second Hand
    if (showSeconds && radius >= 8) {
        float secAngle = ((time.seconds + subSecond) / 60.0f) * 2.0f * PI - (PI / 2.0f);
        int secLen = max(4, (int)(radius * 0.88f));
        int sx = cx + (int)(secLen * cosf(secAngle));
        int sy = cy + (int)(secLen * sinf(secAngle));
        matrix->drawLine(cx, cy, sx, sy, theme.red);
    }

    // 6. Center Jewel Pivot Dot
    matrix->drawPixel(cx, cy, theme.primary);

    // 7. Date Badge
    if (showDate && radius >= 22) {
        char dayBuf[8];
        snprintf(dayBuf, sizeof(dayBuf), "%02d", time.day);
        int badgeW = 14;
        int badgeH = 7;
        int badgeX = cx - badgeW / 2;
        int badgeY = cy + radius / 2 - 2;

        matrix->fillRect(badgeX, badgeY, badgeW, badgeH, theme.panelBg);
        matrix->drawRect(badgeX, badgeY, badgeW, badgeH, theme.border);
        matrix->setFont(nullptr);
        matrix->setTextSize(1);
        matrix->setTextColor(theme.primary);
        matrix->setCursor(badgeX + 2, badgeY);
        matrix->print(dayBuf);
    }
}

void PixelClockWidget::renderDigital(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const DashboardTimeData& time, const DashboardTheme& theme, bool showSeconds, bool showDate, const String& city, bool format24h) {
    if (!matrix || rect.width < 20 || rect.height < 12) return;

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    int textY = rect.y + 3;
    if (rect.height >= 32 && !city.isEmpty()) {
        matrix->setFont(nullptr);
        matrix->setTextSize(1);
        matrix->setTextColor(theme.secondary);
        int cityX = rect.x + (rect.width - (city.length() * 6)) / 2;
        matrix->setCursor(cityX, textY);
        matrix->print(city);
        textY += 10;
    }

    char timeBuf[16];
    if (format24h) {
        if (showSeconds) {
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", time.hours, time.minutes, time.seconds);
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", time.hours, time.minutes);
        }
    } else {
        int h12 = time.hours % 12;
        if (h12 == 0) h12 = 12;
        const char* ampm = (time.hours >= 12) ? "PM" : "AM";
        if (showSeconds) {
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d:%02d%s", h12, time.minutes, time.seconds, (time.hours >= 12) ? "p" : "a");
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s", h12, time.minutes, ampm);
        }
    }

    matrix->setFont(nullptr);
    matrix->setTextSize(1);
    matrix->setTextColor(theme.text);
    int timeX = rect.x + (rect.width - (strlen(timeBuf) * 6)) / 2;
    matrix->setCursor(timeX, textY);
    matrix->print(timeBuf);

    if (showDate && rect.height >= 48) {
        char dateBuf[16];
        snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d", time.day, time.month, time.year);
        matrix->setTextColor(theme.textDim);
        int dateX = rect.x + (rect.width - (strlen(dateBuf) * 6)) / 2;
        matrix->setCursor(dateX, textY + 11);
        matrix->print(dateBuf);
    }
}

// ============================================================================
// Clipping & Rendering Helpers for Smooth Infinite Tickers
// ============================================================================

#include <glcdfont.c>

static void drawClippedPixel(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color) {
    if (x >= minX && x < maxX && y >= minY && y < maxY) {
        matrix->drawPixel(x, y, color);
    }
}

static void drawClippedChar(MatrixPanel_I2S_DMA* matrix, int x, int y, unsigned char c, int minX, int maxX, int minY, int maxY, uint16_t color) {
    if (!matrix) return;
    if (x + 5 < minX || x >= maxX || y + 7 < minY || y >= maxY) return;
    if (c == '`' || c == 0xF7 || c == 0xF8 || (uint8_t)c == 0xB0) c = 247;

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

static void drawClippedString(MatrixPanel_I2S_DMA* matrix, const String& text, int x, int y, int minX, int maxX, int minY, int maxY, uint16_t color) {
    if (!matrix || text.isEmpty()) return;
    int curX = x;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        drawClippedChar(matrix, curX, y, (unsigned char)c, minX, maxX, minY, maxY, color);
        curX += 6;
    }
}

static void drawClippedMarketIcon8x8(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const String& symbol) {
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
        for (int c = 0; c < 8; c++) {
            drawClippedPixel(matrix, x + c, y, minX, maxX, minY, maxY, matrix->color565(255, 180, 0));
            drawClippedPixel(matrix, x + c, y + 7, minX, maxX, minY, maxY, matrix->color565(255, 180, 0));
            drawClippedPixel(matrix, x, y + c, minX, maxX, minY, maxY, matrix->color565(255, 180, 0));
            drawClippedPixel(matrix, x + 7, y + c, minX, maxX, minY, maxY, matrix->color565(255, 180, 0));
        }
        drawClippedPixel(matrix, x + 3, y + 3, minX, maxX, minY, maxY, matrix->color565(255, 255, 255));
    }
}

// ============================================================================
// World Clock Widget (Infinite Smooth Horizontal/Vertical Ticker)
// ============================================================================

void WorldClockWidget::render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const std::vector<WorldTimeItem>& worldTimes, const DashboardTheme& theme) {
    if (!matrix || rect.width < 20 || rect.height < 12) return;

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    size_t count = worldTimes.size();
    if (count == 0) return;

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    int itemW = 44; // [NYC] at top, 14:25 at bottom
    int totalW = (int)count * itemW;

    if (rect.width >= 70 && rect.height <= 34) {
        bool needsScroll = (totalW > (rect.width - 4));
        uint32_t now = millis();
        // Calm, readable scroll speed: ~10 pixels / second
        int scrollOffset = needsScroll ? ((int)((now * 10) / 1000) % max(1, totalW)) : 0;

        for (size_t i = 0; i < count; i++) {
            int slotBaseX = (int)(i * itemW) - scrollOffset;

            if (needsScroll) {
                while (slotBaseX < -itemW) slotBaseX += totalW;
                while (slotBaseX > rect.width) slotBaseX -= totalW;
            } else {
                int spacing = rect.width / (int)count;
                slotBaseX = (int)(i * spacing) + 2;
            }

            for (int k = 0; k < (needsScroll ? 2 : 1); k++) {
                int posX = rect.x + 2 + slotBaseX + (k * totalW);
                if (posX + itemW < minX || posX >= maxX) continue;

                drawClippedString(matrix, "[" + worldTimes[i].code + "]", posX, rect.y + 3, minX, maxX, minY, maxY, theme.secondary);

                char buf[8];
                snprintf(buf, sizeof(buf), "%02d:%02d", worldTimes[i].hours, worldTimes[i].minutes);
                drawClippedString(matrix, buf, posX, rect.y + 12, minX, maxX, minY, maxY, theme.primary);
            }
        }
    } else {
        int rowH = 14;
        int totalH = (int)count * rowH;
        uint32_t now = millis();
        int scrollOffsetY = (totalH > rect.height) ? ((int)((now * 8) / 1000) % max(1, totalH)) : 0;

        for (size_t i = 0; i < count; i++) {
            int slotBaseY = (int)(i * rowH) - scrollOffsetY;
            while (slotBaseY < -rowH) slotBaseY += totalH;
            while (slotBaseY > rect.height) slotBaseY -= totalH;

            int posY = rect.y + 2 + slotBaseY;
            if (posY + rowH < minY || posY >= maxY) continue;

            drawClippedString(matrix, "[" + worldTimes[i].code + "]", rect.x + 3, posY, minX, maxX, minY, maxY, theme.secondary);

            char buf[8];
            snprintf(buf, sizeof(buf), "%02d:%02d", worldTimes[i].hours, worldTimes[i].minutes);
            drawClippedString(matrix, buf, rect.x + 34, posY, minX, maxX, minY, maxY, theme.text);
        }
    }

    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);
}

// ============================================================================
// Climate Widget (Outdoor Weather + Calibrated Indoor SHTC3 with Carousel)
// ============================================================================

static void drawMiniWeatherIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const String& iconCode) {
    if (!matrix) return;
    uint16_t sunCol = matrix->color565(255, 200, 0);
    uint16_t cloudCol = matrix->color565(180, 200, 220);
    uint16_t rainCol = matrix->color565(0, 160, 255);

    if (iconCode.startsWith("01") || iconCode.startsWith("02")) {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                drawClippedPixel(matrix, x + 2 + c, y + 2 + r, minX, maxX, minY, maxY, sunCol);
            }
        }
        drawClippedPixel(matrix, x + 3, y, minX, maxX, minY, maxY, sunCol);
        drawClippedPixel(matrix, x + 3, y + 7, minX, maxX, minY, maxY, sunCol);
        drawClippedPixel(matrix, x, y + 3, minX, maxX, minY, maxY, sunCol);
        drawClippedPixel(matrix, x + 7, y + 3, minX, maxX, minY, maxY, sunCol);
    } else if (iconCode.startsWith("09") || iconCode.startsWith("10")) {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 6; c++) {
                drawClippedPixel(matrix, x + 1 + c, y + 1 + r, minX, maxX, minY, maxY, cloudCol);
            }
        }
        drawClippedPixel(matrix, x + 2, y + 5, minX, maxX, minY, maxY, rainCol);
        drawClippedPixel(matrix, x + 4, y + 6, minX, maxX, minY, maxY, rainCol);
        drawClippedPixel(matrix, x + 6, y + 5, minX, maxX, minY, maxY, rainCol);
    } else {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 6; c++) {
                drawClippedPixel(matrix, x + 1 + c, y + 2 + r, minX, maxX, minY, maxY, cloudCol);
            }
        }
        drawClippedPixel(matrix, x + 3, y + 1, minX, maxX, minY, maxY, cloudCol);
        drawClippedPixel(matrix, x + 4, y + 1, minX, maxX, minY, maxY, cloudCol);
    }
}

void ClimateWidget::render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const WeatherData& weather, bool weatherValid, const IndoorData& indoor, float tempOffset, const DashboardTheme& theme, bool useFahrenheit, const String& lang) {
    if (!matrix || rect.width < 20 || rect.height < 14) return;

    Lang l = I18n::parseLang(lang);

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    // Carousel pages: 0: Outdoor Weather, 1: Indoor Sensor SHTC3
    int numPages = (weatherValid && indoor.valid) ? 2 : 1;
    uint32_t period = 4500;
    uint32_t phase = millis() % period;
    int page = (millis() / period) % numPages;

    int slideY = 0;
    if (phase < 400 && numPages > 1) {
        float progress = (float)phase / 400.0f;
        slideY = (int)((1.0f - progress) * (rect.height - 4));
    }

    auto renderSlide = [&](int p, int offsetY) {
        int baseY = rect.y + 3 + offsetY;
        if (p == 0 && weatherValid) {
            drawMiniWeatherIcon(matrix, rect.x + 3, baseY, minX, maxX, minY, maxY, weather.iconCode);
            char outBuf[12];
            float outT = useFahrenheit ? (weather.temp * 1.8f + 32.0f) : weather.temp;
            snprintf(outBuf, sizeof(outBuf), "%.0f%s", outT, useFahrenheit ? "F" : "C");
            drawClippedString(matrix, outBuf, rect.x + 13, baseY, minX, maxX, minY, maxY, theme.primary);

            if (rect.height >= 26) {
                String desc = I18n::getWeatherCondition(weather.description, l);
                if (desc.isEmpty()) desc = I18n::getOutdoorLabel(l);
                desc.toUpperCase();
                drawClippedString(matrix, desc.substring(0, 8), rect.x + 3, baseY + 11, minX, maxX, minY, maxY, theme.textDim);
            }
        } else if (indoor.valid) {
            float inT = useFahrenheit ? (indoor.temperatureF + tempOffset) : (indoor.temperatureC + tempOffset);
            char inBuf[16];
            snprintf(inBuf, sizeof(inBuf), "%s%.1f%s", I18n::getIndoorLabel(l), inT, useFahrenheit ? "F" : "C");
            drawClippedString(matrix, inBuf, rect.x + 3, baseY, minX, maxX, minY, maxY, theme.accent);

            if (rect.height >= 24) {
                char humBuf[12];
                snprintf(humBuf, sizeof(humBuf), "%.0f%%RH", indoor.humidityPct);
                drawClippedString(matrix, humBuf, rect.x + 3, baseY + 11, minX, maxX, minY, maxY, theme.textDim);

                int barW = rect.width - 8;
                int fillW = constrain((int)(barW * (indoor.humidityPct / 100.0f)), 0, barW);
                int barY = baseY + 19;
                if (barY < maxY - 1 && barY >= minY) {
                    matrix->drawRect(rect.x + 3, barY, barW, 3, theme.border);
                    matrix->fillRect(rect.x + 4, barY + 1, fillW, 1, theme.accent);
                }
            }
        } else {
            drawClippedString(matrix, I18n::getClimateLabel(l), rect.x + 3, baseY, minX, maxX, minY, maxY, theme.textDim);
        }
    };

    if (slideY > 0 && numPages > 1) {
        int prevPage = (page - 1 + numPages) % numPages;
        renderSlide(prevPage, -slideY);
        renderSlide(page, (rect.height - 4) - slideY);
    } else {
        renderSlide(page, 0);
    }

    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);
}

// ============================================================================
// Market Widget (Crypto & Stock Quotes Infinite Fluid Rolling Ticker)
// ============================================================================

static void formatMarketPrice(char* buf, size_t bufSize, float price) {
    if (price <= 0.0f) {
        snprintf(buf, bufSize, "--");
    } else if (price >= 100000.0f) {
        snprintf(buf, bufSize, "$%.0fK", price / 1000.0f);
    } else if (price >= 1000.0f) {
        snprintf(buf, bufSize, "$%.1fK", price / 1000.0f);
    } else if (price >= 100.0f) {
        snprintf(buf, bufSize, "$%.1f", price);
    } else if (price >= 1.0f) {
        snprintf(buf, bufSize, "$%.2f", price);
    } else if (price >= 0.1f) {
        snprintf(buf, bufSize, "$%.3f", price);
    } else if (price >= 0.001f) {
        snprintf(buf, bufSize, "$%.4f", price);
    } else if (price >= 0.00001f) {
        snprintf(buf, bufSize, "$%.5f", price);
    } else {
        snprintf(buf, bufSize, "$%.6f", price);
    }
}

void MarketWidget::render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const std::vector<MarketItem>& items, const DashboardTheme& theme) {
    if (!matrix || rect.width < 20 || rect.height < 12) return;

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    size_t count = items.size();
    if (count == 0) return;

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    if (rect.width >= 100 && rect.height <= 36) {
        // Horizontal Infinite Continuous Rolling Ticker
        int itemW = 56;
        int totalW = (int)count * itemW;
        uint32_t now = millis();
        // Calm, highly readable crawl speed: ~12 pixels / second
        int scrollOffset = (int)((now * 12) / 1000) % max(1, totalW);

        for (size_t i = 0; i < count; i++) {
            int slotBaseX = (int)(i * itemW) - scrollOffset;

            while (slotBaseX < -itemW) slotBaseX += totalW;
            while (slotBaseX > rect.width) slotBaseX -= totalW;

            for (int k = 0; k < 2; k++) {
                int posX = rect.x + 2 + slotBaseX + (k * totalW);
                if (posX + itemW < minX || posX >= maxX) continue;

                drawClippedMarketIcon8x8(matrix, posX, rect.y + 3, minX, maxX, minY, maxY, items[i].symbol);
                drawClippedString(matrix, items[i].symbol, posX + 10, rect.y + 3, minX, maxX, minY, maxY, theme.text);

                char priceBuf[16];
                formatMarketPrice(priceBuf, sizeof(priceBuf), items[i].price);
                drawClippedString(matrix, priceBuf, posX + 10, rect.y + 11, minX, maxX, minY, maxY, theme.primary);

                uint16_t trendCol = (items[i].change24h >= 0) ? theme.green : theme.red;
                char chgBuf[10];
                snprintf(chgBuf, sizeof(chgBuf), "%c%.1f%%", items[i].change24h >= 0 ? '+' : '-', fabsf(items[i].change24h));
                drawClippedString(matrix, chgBuf, posX + 10, rect.y + 19, minX, maxX, minY, maxY, trendCol);
            }
        }
    } else {
        int rowH = 14;
        int totalH = (int)count * rowH;
        uint32_t now = millis();
        int scrollOffsetY = (totalH > rect.height) ? ((int)((now * 10) / 1000) % max(1, totalH)) : 0;

        for (size_t i = 0; i < count; i++) {
            int slotBaseY = (int)(i * rowH) - scrollOffsetY;
            while (slotBaseY < -rowH) slotBaseY += totalH;
            while (slotBaseY > rect.height) slotBaseY -= totalH;

            int posY = rect.y + 2 + slotBaseY;
            if (posY + rowH < minY || posY >= maxY) continue;

            drawClippedMarketIcon8x8(matrix, rect.x + 3, posY + 2, minX, maxX, minY, maxY, items[i].symbol);
            drawClippedString(matrix, items[i].symbol, rect.x + 13, posY, minX, maxX, minY, maxY, theme.text);

            char priceBuf[16];
            formatMarketPrice(priceBuf, sizeof(priceBuf), items[i].price);
            drawClippedString(matrix, priceBuf, rect.x + 13, posY + 7, minX, maxX, minY, maxY, theme.primary);
        }
    }

    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);
}

// ============================================================================
// System Info Widget (Fluid Animated System Carousel)
// ============================================================================

void SysInfoWidget::render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const SystemData& sys, const DashboardTheme& theme) {
    if (!matrix || rect.width < 16 || rect.height < 8) return;

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    int numPages = 3;
    uint32_t period = 4000;
    uint32_t phase = millis() % period;
    int page = (millis() / period) % numPages;

    int slideY = 0;
    if (phase < 400) {
        float progress = (float)phase / 400.0f;
        slideY = (int)((1.0f - progress) * (rect.height - 4));
    }

    int rssiBars = 1;
    if (sys.wifiRssi > -60) rssiBars = 4;
    else if (sys.wifiRssi > -70) rssiBars = 3;
    else if (sys.wifiRssi > -85) rssiBars = 2;

    auto renderSlide = [&](int p, int offsetY) {
        int baseY = rect.y + 3 + offsetY;
        if (p == 0) {
            char ramBuf[12];
            if (rect.width < 46) {
                snprintf(ramBuf, sizeof(ramBuf), "R:%.0f%%", sys.ramUsagePct);
            } else {
                snprintf(ramBuf, sizeof(ramBuf), "RAM:%.0f%%", sys.ramUsagePct);
            }
            drawClippedString(matrix, ramBuf, rect.x + 2, baseY, minX, maxX, minY, maxY, theme.textDim);

            // WiFi signal bars in bottom corner
            int wx = rect.x + rect.width - 15;
            int wy = baseY + rect.height - 8;
            for (int b = 0; b < 4; b++) {
                int bH = (b + 1) * 2;
                uint16_t c = (b < rssiBars) ? theme.accent : theme.border;
                for (int h = 0; h < bH; h++) {
                    drawClippedPixel(matrix, wx + (b * 3), wy - h, minX, maxX, minY, maxY, c);
                    drawClippedPixel(matrix, wx + (b * 3) + 1, wy - h, minX, maxX, minY, maxY, c);
                }
            }
        } else if (p == 1) {
            char upBuf[12];
            int hrs = (int)(sys.uptimeSec / 3600);
            int mins = (int)((sys.uptimeSec % 3600) / 60);
            if (rect.width < 46) {
                snprintf(upBuf, sizeof(upBuf), "%02d:%02d", hrs, mins);
            } else {
                snprintf(upBuf, sizeof(upBuf), "UP:%02d:%02d", hrs, mins);
            }
            drawClippedString(matrix, upBuf, rect.x + 2, baseY, minX, maxX, minY, maxY, theme.text);

            if (rect.height >= 22) {
                char sigBuf[10];
                snprintf(sigBuf, sizeof(sigBuf), "%ddBm", sys.wifiRssi);
                drawClippedString(matrix, sigBuf, rect.x + 2, baseY + 10, minX, maxX, minY, maxY, theme.textDim);
            }
        } else {
            char psramBuf[12];
            uint32_t freePsram = ESP.getFreePsram();
            if (freePsram > 1024 * 1024) {
                if (rect.width < 46) {
                    snprintf(psramBuf, sizeof(psramBuf), "%.1fM", (float)freePsram / (1024.0f * 1024.0f));
                } else {
                    snprintf(psramBuf, sizeof(psramBuf), "PS:%.1fM", (float)freePsram / (1024.0f * 1024.0f));
                }
            } else {
                if (rect.width < 46) {
                    snprintf(psramBuf, sizeof(psramBuf), "%uK", ESP.getFreeHeap() / 1024);
                } else {
                    snprintf(psramBuf, sizeof(psramBuf), "HP:%uK", ESP.getFreeHeap() / 1024);
                }
            }
            drawClippedString(matrix, psramBuf, rect.x + 2, baseY, minX, maxX, minY, maxY, theme.primary);

            if (rect.height >= 22) {
                drawClippedString(matrix, "CPU:OK", rect.x + 2, baseY + 10, minX, maxX, minY, maxY, theme.accent);
            }
        }
    };

    if (slideY > 0) {
        int prevPage = (page - 1 + numPages) % numPages;
        renderSlide(prevPage, -slideY);
        renderSlide(page, (rect.height - 4) - slideY);
    } else {
        renderSlide(page, 0);
    }

    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);
}

// ============================================================================
// Dashboard Engine Orchestrator
// ============================================================================

DashboardEngine::DashboardEngine()
    : matrix(nullptr), m_layoutDirty(true), m_weatherProvider(nullptr),
      m_lastBatchFetch(0), m_lastWeatherFetch(0), m_lastSensorFetch(0), m_lastSystemFetch(0), m_lastMarketFetch(0),
      m_lastSecondSeen(-1), m_secondStartMillis(0),
      m_weatherApiKey(""), m_weatherCity("Paris"), m_weatherUnits("metric"),
      m_cachedTrackedMarkets("BTC,ETH,SOL,NVDA"),
      m_fetchTaskHandle(nullptr), m_taskRunning(false),
      m_forceFetchMarkets(false), m_forceFetchWeather(false) {
    m_weatherProvider = new OpenWeatherMapProvider();

    // Default valid weather so Outdoor widget displays immediately
    m_snapshot.weather.temp = 21.0f;
    m_snapshot.weather.label = "PARIS";
    m_snapshot.weather.iconCode = "01d";
    m_snapshot.weather.description = "Sunny";
    m_snapshot.weatherValid = true;

    m_snapshot.marketItems.push_back(MarketItem("BTC", 90000.0f, 2.5f, true));
    m_snapshot.marketItems.push_back(MarketItem("ETH", 3300.0f, -1.2f, true));
    m_snapshot.marketItems.push_back(MarketItem("SOL", 190.0f, 5.8f, true));
    m_snapshot.marketItems.push_back(MarketItem("NVDA", 135.0f, 3.4f, true));
    m_isActive = false;
}

DashboardEngine::~DashboardEngine() {
    m_taskRunning = false;
    m_isActive = false;
    if (m_fetchTaskHandle) {
        vTaskDelete(m_fetchTaskHandle);
        m_fetchTaskHandle = nullptr;
    }
    if (m_weatherProvider) {
        delete m_weatherProvider;
        m_weatherProvider = nullptr;
    }
}

EngineError DashboardEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    LOGI("Dashboard", "DashboardEngine::initialize called.");
    if (!context || !context->getMatrix()) {
        LOGE("Dashboard", "DashboardEngine::initialize: Invalid context or matrix!");
        return EngineError::InvalidConfig;
    }
    matrix = context->getMatrix();
    m_geometry = context->getGeometry();

    onConfigChanged(engineConfig);

    if (!m_fetchTaskHandle) {
        m_taskRunning = true;
        BaseType_t ret = xTaskCreatePinnedToCore(
            fetchTaskStatic,
            "DashFetch",
            10240,
            this,
            1,
            &m_fetchTaskHandle,
            0
        );
        if (ret != pdPASS) {
            LOGE("Dashboard", "Failed to create DashFetch worker task!");
            m_taskRunning = false;
        }
    }
    LOGI("Dashboard", "DashboardEngine::initialize complete.");
    return EngineError::OK;
}

void DashboardEngine::activate() {
    LOGI("Dashboard", "DashboardEngine::activate called.");
    m_isActive = true;
    m_forceFetchWeather = true;
    m_forceFetchMarkets = true;
    updateSnapshot();
    LOGI("Dashboard", "DashboardEngine::activate complete.");
}

void DashboardEngine::deactivate() {
    LOGI("Dashboard", "DashboardEngine::deactivate called.");
    m_isActive = false;
}

void DashboardEngine::fetchTaskStatic(void* param) {
    DashboardEngine* self = static_cast<DashboardEngine*>(param);
    if (self) {
        self->fetchTaskLoop();
    }
    vTaskDelete(NULL);
}

void DashboardEngine::fetchTaskLoop() {
    LOGI("Dashboard", "DashFetch background task started on Core 0.");
    vTaskDelay(pdMS_TO_TICKS(4000)); // Delay initial network queries so boot settles

    while (m_taskRunning) {
        if (m_isActive && WiFi.status() == WL_CONNECTED) {
            uint32_t now = millis();
            uint32_t intervalMs = (uint32_t)max(1, m_config.refreshIntervalMin) * 60000UL;

            bool shouldFetch = m_forceFetchWeather || m_forceFetchMarkets || (m_lastBatchFetch == 0) || (now - m_lastBatchFetch >= intervalMs);

            if (shouldFetch) {
                m_forceFetchWeather = false;
                m_forceFetchMarkets = false;
                m_lastBatchFetch = now;
                LOGI("Dashboard", "Executing sequential synchronized data fetch (interval=%d min)...", m_config.refreshIntervalMin);

                // 1. Fetch Weather first (à la queue-leu-leu)
                if (m_config.showWeather && m_isActive) {
                    fetchWeather();
                    vTaskDelay(pdMS_TO_TICKS(500)); // Yield to let Core 0 network memory settle
                }

                // 2. Fetch Market items strictly one by one (à la queue-leu-leu)
                if (m_config.showMarkets && m_isActive) {
                    fetchMarkets();
                    vTaskDelay(pdMS_TO_TICKS(300));
                }

                LOGI("Dashboard", "Sequential data fetch completed. Next refresh in %d min.", m_config.refreshIntervalMin);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    m_fetchTaskHandle = nullptr;
}

void DashboardEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;

    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    String sysUnit = guard->system.unit.length() > 0 ? guard->system.unit : "C";
    String sysLang = guard->system.lang.length() > 0 ? guard->system.lang : "en";
    float sysTempOffset = guard->system.temp_offset;
    bool sysFormat24h = guard->system.format24h;

    String oldMarkets = m_cachedTrackedMarkets;
    String oldCity = m_weatherCity;
    String oldApiKey = m_weatherApiKey;
    String oldLang = m_config.lang;
    String oldUnit = m_config.tempUnit;

    m_config.clockMode = static_cast<ClockMode>(engineConfig->getInt("clock_mode", 1)); // Default Analog
    m_config.theme = engineConfig->getInt("theme", 0);
    m_config.showClock = engineConfig->getBool("show_clock", true);
    m_config.showWeather = engineConfig->getBool("show_weather", true);
    m_config.showIndoorTemp = engineConfig->getBool("show_indoor_temp", true);
    m_config.showSysInfo = engineConfig->getBool("show_sysinfo", true);
    m_config.showDate = engineConfig->getBool("show_date", true);
    m_config.showSeconds = engineConfig->getBool("show_seconds", true);
    m_config.smoothSeconds = engineConfig->getBool("smooth_seconds", true);
    m_config.showWorldClock = engineConfig->getBool("show_world_clock", true);
    m_config.showMarkets = engineConfig->getBool("show_markets", true);
    m_config.worldClocks = engineConfig->getString("world_clocks", "NYC,TYO,LON");
    m_config.trackedMarkets = engineConfig->getString("tracked_markets", "BTC,ETH,SOL,NVDA");
    m_cachedTrackedMarkets = m_config.trackedMarkets;

    // Unit, temp offset, lang and 24h format stored as configured (supporting dynamic "system" inheritance)
    String unit = engineConfig->getString("temp_unit", "");
    if (unit.isEmpty()) unit = engineConfig->getString("units", "system");
    m_config.tempUnit = unit;
    m_config.tempOffsetStr = engineConfig->getString("temp_offset", "");
    m_config.lang = engineConfig->getString("lang", "system");
    m_config.format24hStr = engineConfig->getString("format_24h", "system");

    // Refresh interval: data update frequency in minutes (default 10 min)
    m_config.refreshIntervalMin = engineConfig->getInt("refresh_interval", 10);
    if (m_config.refreshIntervalMin < 1) m_config.refreshIntervalMin = 10;

    m_weatherApiKey = engineConfig->getString("weather_api_key", "");
    m_weatherCity = engineConfig->getString("weather_city", "");
    m_weatherUnits = engineConfig->getString("weather_units", "metric");

    if (m_weatherApiKey.isEmpty() || m_weatherCity.isEmpty()) {
        for (const auto& inst : guard->instances) {
            if (inst.engine_id == "weather") {
                if (m_weatherApiKey.isEmpty()) m_weatherApiKey = inst.config.getString("api_key", "");
                if (m_weatherCity.isEmpty()) m_weatherCity = inst.config.getString("city", "");
                if (m_weatherUnits.isEmpty()) m_weatherUnits = inst.config.getString("units", "metric");
                break;
            }
        }
    }

    m_config.city = m_weatherCity.isEmpty() ? "PARIS" : m_weatherCity;

    if (oldMarkets != m_cachedTrackedMarkets || m_config.showMarkets) {
        m_forceFetchMarkets = true;
        m_lastMarketFetch = 0;

        std::vector<String> symbols;
        String raw = m_cachedTrackedMarkets;
        int start = 0;
        while (start < (int)raw.length()) {
            int comma = raw.indexOf(',', start);
            String token = (comma == -1) ? raw.substring(start) : raw.substring(start, comma);
            token.trim();
            token.toUpperCase();
            if (token.length() > 0) symbols.push_back(token);
            if (comma == -1) break;
            start = comma + 1;
        }
        if (!symbols.empty()) {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            std::vector<MarketItem> placeholders;
            for (const auto& sym : symbols) {
                bool found = false;
                for (const auto& cur : m_snapshot.marketItems) {
                    if (cur.symbol == sym) {
                        placeholders.push_back(cur);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    placeholders.push_back(MarketItem(sym, 0.0f, 0.0f, false));
                }
            }
            m_snapshot.marketItems = placeholders;
        }
    }

    if (oldCity != m_weatherCity || oldApiKey != m_weatherApiKey || oldLang != m_config.lang || oldUnit != m_config.tempUnit) {
        m_forceFetchWeather = true;
        m_lastWeatherFetch = 0;
    }

    m_cachedLayout = DashboardLayoutCalculator::calculate(m_geometry, m_config);
    m_layoutDirty = false;
    updateWorldTimes();
    updateSnapshot();
}

void DashboardEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_geometry = geometry;
    m_cachedLayout = DashboardLayoutCalculator::calculate(m_geometry, m_config);
    m_layoutDirty = false;
}

DashboardTheme DashboardEngine::getTheme(int themeId) const {
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

void DashboardEngine::updateWorldTimes() {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_snapshot.worldTimes.clear();

    struct TzDef { const char* code; int offsetMin; };
    static const TzDef DEFS[] = {
        {"NYC", -240}, // New York (EDT, UTC-4)
        {"TYO", 540},  // Tokyo (JST, UTC+9)
        {"LON", 60},   // London (BST, UTC+1)
        {"PAR", 120},  // Paris (CEST, UTC+2)
        {"BER", 120},  // Berlin (CEST, UTC+2)
        {"ROM", 120},  // Rome (CEST, UTC+2)
        {"MAD", 120},  // Madrid (CEST, UTC+2)
        {"AMS", 120},  // Amsterdam (CEST, UTC+2)
        {"BRU", 120},  // Brussels (CEST, UTC+2)
        {"GVA", 120},  // Geneva (CEST, UTC+2)
        {"ZRH", 120},  // Zurich (CEST, UTC+2)
        {"VIE", 120},  // Vienna (CEST, UTC+2)
        {"PRG", 120},  // Prague (CEST, UTC+2)
        {"WAW", 120},  // Warsaw (CEST, UTC+2)
        {"ATH", 180},  // Athens (EEST, UTC+3)
        {"IST", 180},  // Istanbul (TRT, UTC+3)
        {"MOW", 180},  // Moscow (MSK, UTC+3)
        {"DXB", 240},  // Dubai (GST, UTC+4)
        {"REU", 240},  // Reunion (RET, UTC+4)
        {"MRU", 240},  // Mauritius (MUT, UTC+4)
        {"DOH", 180},  // Doha (AST, UTC+3)
        {"RUH", 180},  // Riyadh (AST, UTC+3)
        {"DEL", 330},  // New Delhi (IST, UTC+5:30)
        {"BOM", 330},  // Mumbai (IST, UTC+5:30)
        {"BKK", 420},  // Bangkok (ICT, UTC+7)
        {"JKT", 420},  // Jakarta (WIB, UTC+7)
        {"SIN", 480},  // Singapore (SGT, UTC+8)
        {"HKG", 480},  // Hong Kong (HKT, UTC+8)
        {"PEK", 480},  // Beijing (CST, UTC+8)
        {"SHA", 480},  // Shanghai (CST, UTC+8)
        {"TPE", 480},  // Taipei (CST, UTC+8)
        {"SEL", 540},  // Seoul (KST, UTC+9)
        {"SYD", 600},  // Sydney (AEST, UTC+10)
        {"MEL", 600},  // Melbourne (AEST, UTC+10)
        {"BNE", 600},  // Brisbane (AEST, UTC+10)
        {"AKL", 720},  // Auckland (NZST, UTC+12)
        {"HNL", -600}, // Honolulu (HST, UTC-10)
        {"ANC", -480}, // Anchorage (AKDT, UTC-8)
        {"LAX", -420}, // Los Angeles (PDT, UTC-7)
        {"SFO", -420}, // San Francisco (PDT, UTC-7)
        {"SEA", -420}, // Seattle (PDT, UTC-7)
        {"DEN", -360}, // Denver (MDT, UTC-6)
        {"CHI", -300}, // Chicago (CDT, UTC-5)
        {"DFW", -300}, // Dallas (CDT, UTC-5)
        {"MIA", -240}, // Miami (EDT, UTC-4)
        {"BOS", -240}, // Boston (EDT, UTC-4)
        {"YUL", -240}, // Montreal (EDT, UTC-4)
        {"YYZ", -240}, // Toronto (EDT, UTC-4)
        {"YVR", -420}, // Vancouver (PDT, UTC-7)
        {"MEX", -360}, // Mexico City (CST, UTC-6)
        {"BOG", -300}, // Bogota (COT, UTC-5)
        {"LIM", -300}, // Lima (PET, UTC-5)
        {"SCL", -240}, // Santiago (CLT, UTC-4)
        {"EZE", -180}, // Buenos Aires (ART, UTC-3)
        {"RIO", -180}, // Rio de Janeiro (BRT, UTC-3)
        {"SAO", -180}, // Sao Paulo (BRT, UTC-3)
        {"UTC", 0},    // UTC (UTC+0)
        {"GMT", 0}     // GMT (UTC+0)
    };

    String clocks = m_config.worldClocks;
    int start = 0;
    while (start < (int)clocks.length()) {
        int comma = clocks.indexOf(',', start);
        String token = (comma == -1) ? clocks.substring(start) : clocks.substring(start, comma);
        token.trim();
        token.toUpperCase();

        if (token.length() > 0) {
            bool matched = false;

            // Check custom offset notation (e.g. "REU:+4", "UTC+2", "NYC:-4")
            int colonIdx = token.indexOf(':');
            int plusIdx = token.indexOf('+');
            int minusIdx = token.indexOf('-');

            if (colonIdx != -1) {
                String code = token.substring(0, colonIdx);
                code.trim();
                float offH = token.substring(colonIdx + 1).toFloat();
                WorldTimeItem item;
                item.code = code.substring(0, 4);
                item.offsetMinutes = (int)(offH * 60.0f);
                m_snapshot.worldTimes.push_back(item);
                matched = true;
            } else if ((plusIdx > 0 && plusIdx < 5) || (minusIdx > 0 && minusIdx < 5)) {
                int signIdx = (plusIdx != -1) ? plusIdx : minusIdx;
                String code = token.substring(0, signIdx);
                code.trim();
                float offH = token.substring(signIdx).toFloat();
                WorldTimeItem item;
                item.code = code.substring(0, 4);
                item.offsetMinutes = (int)(offH * 60.0f);
                m_snapshot.worldTimes.push_back(item);
                matched = true;
            } else {
                for (const auto& def : DEFS) {
                    if (token == def.code) {
                        WorldTimeItem item;
                        item.code = def.code;
                        item.offsetMinutes = def.offsetMin;
                        m_snapshot.worldTimes.push_back(item);
                        matched = true;
                        break;
                    }
                }
            }

            if (!matched && token.length() <= 4) {
                // Default fallback: display custom token code with Paris offset
                WorldTimeItem item;
                item.code = token;
                item.offsetMinutes = 120;
                m_snapshot.worldTimes.push_back(item);
            }
        }

        if (comma == -1) break;
        start = comma + 1;
    }

    if (m_snapshot.worldTimes.empty()) {
        m_snapshot.worldTimes.push_back({"NYC", -240, 0, 0});
        m_snapshot.worldTimes.push_back({"TYO", 540, 0, 0});
        m_snapshot.worldTimes.push_back({"LON", 60, 0, 0});
    }
}

void DashboardEngine::fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;

    String apiKey = m_weatherApiKey;
    String city = m_weatherCity.isEmpty() ? "Paris" : m_weatherCity;
    String lang = m_config.lang.length() > 0 ? m_config.lang : "en";
    lang.toLowerCase();

    // 1. Try OpenWeatherMap if key is provided
    if (m_weatherProvider && apiKey.length() > 5) {
        WeatherData fc[1];
        int numFc = 0;
        if (m_weatherProvider->fetchForecast(apiKey, city, lang, "metric", fc, 1, numFc)) {
            if (numFc > 0) {
                std::lock_guard<std::mutex> lock(m_snapshotMutex);
                m_snapshot.weather = fc[0];
                m_snapshot.weatherValid = true;
                LOGI("Dashboard", "Weather updated (OpenWeatherMap): %.1f°C (%s)", m_snapshot.weather.temp, m_snapshot.weather.description.c_str());
                return;
            }
        }
    }

    // 2. Free Open-Meteo fallback (No API key needed!)
    float lat = 48.8566f;
    float lon = 2.3522f;

    // Geocode city if not default Paris
    if (!city.equalsIgnoreCase("Paris")) {
        WiFiClientSecure geoClient;
        geoClient.setInsecure();
        HTTPClient geoHttp;
        geoHttp.setTimeout(3000);
        String encCity = city;
        encCity.trim();
        encCity.replace(" ", "%20");
        String geoUrl = "https://geocoding-api.open-meteo.com/v1/search?name=" + encCity + "&count=1&language=" + lang + "&format=json";
        if (geoHttp.begin(geoClient, geoUrl)) {
            int code = geoHttp.GET();
            if (code == 200) {
                DynamicJsonDocument geoDoc(2048);
                if (deserializeJson(geoDoc, geoHttp.getString()) == DeserializationError::Ok) {
                    if (geoDoc["results"].is<JsonArray>() && geoDoc["results"].size() > 0) {
                        lat = geoDoc["results"][0]["latitude"].as<float>();
                        lon = geoDoc["results"][0]["longitude"].as<float>();
                    }
                }
            }
            geoHttp.end();
            geoClient.stop();
        }
    }

    // Fetch current weather from Open-Meteo
    WiFiClientSecure metClient;
    metClient.setInsecure();
    HTTPClient metHttp;
    metHttp.setTimeout(3000);
    String metUrl = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) + "&current=temperature_2m,weather_code";
    if (metHttp.begin(metClient, metUrl)) {
        int code = metHttp.GET();
        if (code == 200) {
            DynamicJsonDocument metDoc(2048);
            if (deserializeJson(metDoc, metHttp.getString()) == DeserializationError::Ok) {
                float temp = metDoc["current"]["temperature_2m"].as<float>();
                int wCode = metDoc["current"]["weather_code"].as<int>();

                Lang l = I18n::parseLang(lang);

                WeatherData wd;
                wd.temp = temp;
                wd.iconCode = "01d";
                wd.description = "Clear";

                if (wCode >= 1 && wCode <= 3) { wd.iconCode = "02d"; wd.description = "Clouds"; }
                else if (wCode >= 45 && wCode <= 48) { wd.iconCode = "50d"; wd.description = "Fog"; }
                else if (wCode >= 51 && wCode <= 67) { wd.iconCode = "10d"; wd.description = "Rain"; }
                else if (wCode >= 71 && wCode <= 77) { wd.iconCode = "13d"; wd.description = "Snow"; }
                else if (wCode >= 80 && wCode <= 82) { wd.iconCode = "09d"; wd.description = "Drizzle"; }
                else if (wCode >= 95) { wd.iconCode = "11d"; wd.description = "Storm"; }

                wd.description = I18n::getWeatherCondition(wd.description, l);

                std::lock_guard<std::mutex> lock(m_snapshotMutex);
                m_snapshot.weather = wd;
                m_snapshot.weatherValid = true;
                LOGI("Dashboard", "Weather updated (Open-Meteo): %.1f°C (%s)", wd.temp, wd.description.c_str());
            }
        }
        metHttp.end();
        metClient.stop();
    }
}

void DashboardEngine::fetchMarkets() {
    if (WiFi.status() != WL_CONNECTED) return;

    std::vector<String> symbols;
    {
        String raw = m_cachedTrackedMarkets;
        int start = 0;
        while (start < (int)raw.length()) {
            int comma = raw.indexOf(',', start);
            String token = (comma == -1) ? raw.substring(start) : raw.substring(start, comma);
            token.trim();
            token.toUpperCase();
            if (token.length() > 0) symbols.push_back(token);
            if (comma == -1) break;
            start = comma + 1;
        }
    }

    if (symbols.empty()) return;

    std::vector<MarketItem> updatedItems;
    YahooFinanceProvider yahooProvider;

    for (const auto& sym : symbols) {
        if (!m_isActive) break;
        bool itemAdded = false;

        // 1. Check Binance (fast crypto API)
        WiFiClientSecure binanceClient;
        binanceClient.setInsecure();
        HTTPClient http;
        http.setTimeout(2500);
        String url = "https://api.binance.com/api/v3/ticker/24hr?symbol=" + sym + "USDT";
        
        if (http.begin(binanceClient, url)) {
            int code = http.GET();
            if (code == 200) {
                DynamicJsonDocument doc(1024);
                if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
                    float price = doc["lastPrice"].as<float>();
                    float chg = doc["priceChangePercent"].as<float>();
                    updatedItems.push_back(MarketItem(sym, price, chg, true));
                    itemAdded = true;
                }
            }
            http.end();
        }
        binanceClient.stop(); // Free TLS/SSL buffer immediately

        // 2. Check Yahoo Finance (Stocks & other Cryptos)
        if (!itemAdded && m_isActive) {
            float stockPrice = 0.0f;
            float stockChange = 0.0f;
            String imgUrl = "";
            if (yahooProvider.fetchQuote(sym, stockPrice, stockChange, imgUrl)) {
                updatedItems.push_back(MarketItem(sym, stockPrice, stockChange, true));
                itemAdded = true;
            } else if (yahooProvider.fetchQuote(sym + "-USD", stockPrice, stockChange, imgUrl)) {
                updatedItems.push_back(MarketItem(sym, stockPrice, stockChange, true));
                itemAdded = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    if (!updatedItems.empty()) {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshot.marketItems = updatedItems;
        LOGI("Dashboard", "Markets updated (%d tickers).", (int)updatedItems.size());
    }
}

void DashboardEngine::updateSnapshot() {
    uint32_t now = millis();

    // 1. Time & Synchronized Sub-Second Progress
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshot.time.hours = timeinfo.tm_hour;
        m_snapshot.time.minutes = timeinfo.tm_min;
        m_snapshot.time.seconds = timeinfo.tm_sec;
        m_snapshot.time.day = timeinfo.tm_mday;
        m_snapshot.time.month = timeinfo.tm_mon + 1;
        m_snapshot.time.year = timeinfo.tm_year + 1900;
        m_snapshot.time.dayOfWeek = timeinfo.tm_wday;

        if (timeinfo.tm_sec != m_lastSecondSeen) {
            m_lastSecondSeen = timeinfo.tm_sec;
            m_secondStartMillis = now;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        if (m_config.smoothSeconds) {
            uint32_t elapsed = now - m_secondStartMillis;
            m_snapshot.subSecondFraction = (elapsed < 1000) ? ((float)elapsed / 1000.0f) : 0.999f;
        } else {
            m_snapshot.subSecondFraction = 0.0f;
        }
    }

    // 2. Compute World Clocks Time
    time_t rawtime;
    time(&rawtime);
    struct tm* gm = gmtime(&rawtime);
    if (gm) {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        int utcMinTotal = gm->tm_hour * 60 + gm->tm_min;
        for (auto& item : m_snapshot.worldTimes) {
            int localMin = (utcMinTotal + item.offsetMinutes + 1440) % 1440;
            item.hours = localMin / 60;
            item.minutes = localMin % 60;
        }
    }

    // 3. Indoor Sensor Snapshot (every 2 seconds)
    if (m_config.showIndoorTemp && (m_lastSensorFetch == 0 || (now - m_lastSensorFetch >= 2000UL))) {
        m_lastSensorFetch = now;
        EnvironmentData env = hardwareHAL.readEnvironment(0.0f);
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshot.indoor.valid = env.available;
        m_snapshot.indoor.temperatureC = env.temperatureC;
        m_snapshot.indoor.temperatureF = env.temperatureF;
        m_snapshot.indoor.humidityPct = env.humidity;
    }

    // 4. System Metrics Snapshot (every 1 second)
    if (m_config.showSysInfo && (m_lastSystemFetch == 0 || (now - m_lastSystemFetch >= 1000UL))) {
        m_lastSystemFetch = now;
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        uint32_t heapSize = ESP.getHeapSize();
        m_snapshot.system.ramUsagePct = (heapSize > 0) ? ((1.0f - ((float)ESP.getFreeHeap() / (float)heapSize)) * 100.0f) : 0.0f;
        m_snapshot.system.wifiRssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
        m_snapshot.system.uptimeSec = now / 1000;
    }
}

void DashboardEngine::update(EngineContext* context) {
    updateSnapshot();
}

void DashboardEngine::render(EngineContext* context) {
    if (!matrix) return;

    if (m_layoutDirty) {
        m_cachedLayout = DashboardLayoutCalculator::calculate(m_geometry, m_config);
        m_layoutDirty = false;
    }

    DashboardTheme theme = getTheme(m_config.theme);

    DashboardSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        snap = m_snapshot;
    }

    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();

    bool useFahrenheit = m_config.tempUnit.equalsIgnoreCase("F") || m_config.tempUnit.equalsIgnoreCase("imperial") || 
                         ((m_config.tempUnit.isEmpty() || m_config.tempUnit.equalsIgnoreCase("system")) && 
                          (guard->system.unit.equalsIgnoreCase("F") || guard->system.unit.equalsIgnoreCase("imperial")));

    float tempOffset = (m_config.tempOffsetStr.isEmpty() || m_config.tempOffsetStr.equalsIgnoreCase("system")) ? 
                       guard->system.temp_offset : m_config.tempOffsetStr.toFloat();

    String lang = (m_config.lang.isEmpty() || m_config.lang.equalsIgnoreCase("system")) ? 
                  (guard->system.lang.length() > 0 ? guard->system.lang : "en") : m_config.lang;

    bool format24h = (m_config.format24hStr.isEmpty() || m_config.format24hStr.equalsIgnoreCase("system")) ? 
                     guard->system.format24h : 
                     (m_config.format24hStr.equalsIgnoreCase("24h") || m_config.format24hStr.equalsIgnoreCase("true") || m_config.format24hStr == "1");

    // 1. Clock Widget (Analog or Digital)
    if (m_cachedLayout.hasClock) {
        if (m_config.clockMode == ClockMode::MODE_ANALOG) {
            PixelClockWidget::renderAnalog(matrix, m_cachedLayout.clockRect, snap.time, snap.subSecondFraction, theme, m_config.showSeconds, m_config.showDate);
        } else {
            PixelClockWidget::renderDigital(matrix, m_cachedLayout.clockRect, snap.time, theme, m_config.showSeconds, m_config.showDate, m_config.city, format24h);
        }
    }

    // 2. World Clock Widget
    if (m_cachedLayout.hasWorldClock) {
        WorldClockWidget::render(matrix, m_cachedLayout.worldClockRect, snap.worldTimes, theme);
    }

    // 3. Climate Widget (Outdoor + Calibrated Indoor Sensor)
    if (m_cachedLayout.hasClimate) {
        ClimateWidget::render(matrix, m_cachedLayout.climateRect, snap.weather, snap.weatherValid, snap.indoor, tempOffset, theme, useFahrenheit, lang);
    }

    // 4. Market Widget (Crypto + Stock Quotes)
    if (m_cachedLayout.hasMarket) {
        MarketWidget::render(matrix, m_cachedLayout.marketRect, snap.marketItems, theme);
    }

    // 5. SysInfo Widget
    if (m_cachedLayout.hasSysInfo) {
        SysInfoWidget::render(matrix, m_cachedLayout.sysInfoRect, snap.system, theme);
    }
}

// ============================================================================
// Descriptor Handler Registration
// ============================================================================

EngineDescriptor DashboardEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"dashboard", "Dashboard Engine", "info", FIRMWARE_VERSION};
    desc.requirements.needsPsram = false;
    desc.requirements.needsAudio = false;
    desc.requirements.needsTempSensor = false;
    desc.requirements.needsGyroscope = false;

    desc.schema.fields = {
        ConfigField("clock_mode", ConfigType::ENUM, "Clock Style", "Display as Digital or Analog Hands", "1", false, "", "", "", "0:Digital Modern,1:Pixel-Art Watch Dial,2:Minimal", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("theme", ConfigType::ENUM, "Color Theme", "Color palette for dashboard widgets", "0", false, "", "", "", "0:Cyberpunk Neon,1:Arcade Amber HUD,2:Minimalist Luxury,3:Matrix Phosphor", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_clock", ConfigType::BOOLEAN, "Show Clock", "Display main time widget", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_world_clock", ConfigType::BOOLEAN, "Show World Clocks", "Display secondary timezones (NYC, TYO, LON...)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("world_clocks", ConfigType::STRING, "World Timezones", "Select timezones from the list or enter custom city/airport codes and offsets (e.g. NYC,TYO,LON,PAR,DXB,SIN,LAX,MIA,HKG,SYD,BER,ROM,MAD,AMS,YUL,UTC or REU:+4)", "NYC,TYO,LON", false, "", "", "", "NYC:New York (NYC),TYO:Tokyo (TYO),LON:London (LON),PAR:Paris (PAR),LAX:Los Angeles (LAX),SFO:San Francisco (SFO),CHI:Chicago (CHI),MIA:Miami (MIA),DXB:Dubai (DXB),SIN:Singapore (SIN),HKG:Hong Kong (HKG),SYD:Sydney (SYD),BER:Berlin (BER),ROM:Rome (ROM),MAD:Madrid (MAD),AMS:Amsterdam (AMS),YUL:Montreal (YUL),UTC:UTC (GMT)", "", true, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_weather", ConfigType::BOOLEAN, "Show Weather", "Display outdoor weather & temp", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("weather_city", ConfigType::STRING, "Weather City", "City name for weather forecasts (e.g. Paris, London, Tokyo, New York)", "Paris", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("weather_api_key", ConfigType::STRING, "OpenWeatherMap Key (Optional)", "OpenWeatherMap API Key (leave empty to use free Open-Meteo service without key)", "", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_indoor_temp", ConfigType::BOOLEAN, "Show Indoor Climate (SHTC3)", "Display room temperature & humidity", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("temp_unit", ConfigType::ENUM, "Temperature Unit", "Celsius (°C) or Fahrenheit (°F)", "system", false, "", "", "", "system:System (General),C:Celsius (°C),F:Fahrenheit (°F)", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("temp_offset", ConfigType::FLOAT, "Indoor Temp Offset", "Offset to compensate for CPU heat dissipation in the chosen temperature unit (leave empty to use General System setting)", "", false, "-30.0", "30.0", "0.5", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("refresh_interval", ConfigType::ENUM, "Refresh Interval", "Data refresh frequency for weather and markets", "10", false, "", "", "", "1:1 Minute,5:5 Minutes,10:10 Minutes (Recommended),15:15 Minutes,30:30 Minutes,60:60 Minutes", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("format_24h", ConfigType::ENUM, "Time Format", "24H or 12H time format", "system", false, "", "", "", "system:System (General),24h:24 Hours (23:59),12h:12 Hours (11:59 PM)", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Language", "Language for labels and dates", "system", false, "", "", "", "system:System (General),fr:Français,en:English,es:Español", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_markets", ConfigType::BOOLEAN, "Show Markets / Stocks", "Display live crypto and stock ticker badges", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("tracked_markets", ConfigType::STRING, "Tracked Markets (Crypto & Stocks)", "Select symbols from list or enter custom ticker symbols (e.g. PEPE, KAS, TAO, NVDA, AAPL, MSFT, BTC, ETH, SOL...)", "BTC,ETH,SOL,NVDA", false, "", "", "", "BTC:Bitcoin (BTC),ETH:Ethereum (ETH),SOL:Solana (SOL),BNB:Binance (BNB),XRP:Ripple (XRP),DOGE:Dogecoin (DOGE),ADA:Cardano (ADA),AVAX:Avalanche (AVAX),LINK:Chainlink (LINK),SUI:Sui (SUI),NEAR:Near (NEAR),PEPE:Pepe (PEPE),SHIB:Shiba (SHIB),TAO:Bittensor (TAO),APT:Aptos (APT),KAS:Kaspa (KAS),RENDER:Render (RENDER),FET:Fetch.ai (FET),INJ:Injective (INJ),BONK:Bonk (BONK),WIF:Dogwifhat (WIF),NVDA:Nvidia (NVDA),AAPL:Apple (AAPL),TSLA:Tesla (TSLA),MSFT:Microsoft (MSFT),GOOG:Alphabet (GOOG),AMZN:Amazon (AMZN),META:Meta (META),AMD:AMD (AMD),PLTR:Palantir (PLTR),MSTR:MicroStrategy (MSTR),COIN:Coinbase (COIN),SPY:S&P 500 (SPY),QQQ:Nasdaq (QQQ)", "", true, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_sysinfo", ConfigType::BOOLEAN, "Show System Vitals", "Display RAM, CPU & WiFi gauges", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_date", ConfigType::BOOLEAN, "Show Date", "Display day and date badge", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_seconds", ConfigType::BOOLEAN, "Show Seconds", "Display sweeping second hand or seconds digits", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("smooth_seconds", ConfigType::BOOLEAN, "Smooth Sweeping Seconds", "Continuous sweeping second hand vs crisp 1s ticks", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };

    desc.factory = []() { return std::unique_ptr<IEngine>(new DashboardEngine()); };
    return desc;
}
