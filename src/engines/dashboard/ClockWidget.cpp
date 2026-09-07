#include "ClockWidget.h"
#include "DashboardCommon.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <math.h>

void PixelClockWidget::renderAnalog(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const DashboardTimeData& time, float subSecond, const DashboardTheme& theme, bool showSeconds, bool showDate) {
    if (!matrix || rect.width < 14 || rect.height < 14) return;

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);

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

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    if (rect.width < 50) {
        // Compact clock mode (e.g. 32x32 on 128x32 display)
        int hDisp = time.hours;
        if (!format24h) {
            hDisp = (time.hours % 12 == 0) ? 12 : (time.hours % 12);
        }
        char hmBuf[8];
        snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", hDisp, time.minutes);
        int hmW = measureText(strlen(hmBuf));
        int hmX = rect.x + (rect.width - hmW) / 2;

        if (rect.height >= 24 && showDate) {
            // Stacked: HH:MM on line 1, DD/MM on line 2 (100% RPi parity)
            drawClippedString(matrix, hmBuf, hmX, rect.y + 4, minX, maxX, minY, maxY, theme.primary);

            char dBuf[8];
            snprintf(dBuf, sizeof(dBuf), "%02d/%02d", time.day, time.month);
            int dW = measureText(strlen(dBuf));
            int dX = rect.x + (rect.width - dW) / 2;
            drawClippedString(matrix, dBuf, dX, rect.y + 16, minX, maxX, minY, maxY, theme.text);
        } else {
            // Vertically centered single line HH:MM
            int cy = rect.y + (rect.height - 7) / 2;
            drawClippedString(matrix, hmBuf, hmX, cy, minX, maxX, minY, maxY, theme.primary);
        }
        return;
    }

    int textY = rect.y + 3;
    if (rect.height >= 32 && !city.isEmpty()) {
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
        int h12 = (time.hours % 12 == 0) ? 12 : (time.hours % 12);
        if (showSeconds) {
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", h12, time.minutes, time.seconds);
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h12, time.minutes);
        }
    }

    int timeW = strlen(timeBuf) * 6;
    int timeX = rect.x + (rect.width - timeW) / 2;
    int cy = (rect.height >= 32) ? textY : (rect.y + (rect.height - 7) / 2);

    matrix->setFont(nullptr);
    matrix->setTextSize(1);
    matrix->setTextColor(theme.primary);
    matrix->setCursor(timeX, cy);
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
