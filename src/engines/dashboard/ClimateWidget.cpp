#include "ClimateWidget.h"
#include "DashboardCommon.h"
#include "../../core/I18n.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

void ClimateWidget::render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const WeatherData& weather, bool weatherValid, const IndoorData& indoor, float tempOffset, const DashboardTheme& theme, bool useFahrenheit, const String& lang) {
    if (!matrix || rect.width < 14 || rect.height < 8) return;

    Lang l = I18n::parseLang(lang);

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    // CASE 1: Full-height Tall Box (e.g. 64x256 Tower, rect.height >= 44)
    // Display BOTH Outdoor AND Indoor simultaneously with ZERO line overlap!
    if (rect.height >= 44 && weatherValid && indoor.valid) {
        // --- TOP HALF: OUTDOOR WEATHER ---
        drawMiniWeatherIcon(matrix, rect.x + 3, rect.y + 3, minX, maxX, minY, maxY, weather.iconCode);

        char outBuf[16];
        float outT = useFahrenheit ? (weather.temp * 1.8f + 32.0f) : weather.temp;
        snprintf(outBuf, sizeof(outBuf), "%.1f\xF8%s", outT, useFahrenheit ? "F" : "C");
        drawClippedString(matrix, outBuf, rect.x + 14, rect.y + 3, minX, maxX, minY, maxY, theme.primary);

        String desc = I18n::getWeatherCondition(weather.description, l);
        if (desc.isEmpty()) desc = I18n::getOutdoorLabel(l);
        desc.toUpperCase();
        drawClippedString(matrix, desc.substring(0, 8), rect.x + 3, rect.y + 13, minX, maxX, minY, maxY, theme.textDim);

        // Subtle divider
        int divY = rect.y + 23;
        matrix->drawFastHLine(rect.x + 3, divY, rect.width - 6, theme.border);

        // --- BOTTOM HALF: INDOOR SENSOR (SHTC3) ---
        drawMiniIndoorIcon(matrix, rect.x + 3, rect.y + 26, minX, maxX, minY, maxY, theme.accent);

        char inBuf[16];
        float inT = useFahrenheit ? (indoor.temperatureF + tempOffset) : (indoor.temperatureC + tempOffset);
        snprintf(inBuf, sizeof(inBuf), "%.1f\xF8%s", inT, useFahrenheit ? "F" : "C");
        drawClippedString(matrix, inBuf, rect.x + 14, rect.y + 26, minX, maxX, minY, maxY, theme.accent);

        char humBuf[12];
        snprintf(humBuf, sizeof(humBuf), "%.0f%%RH", indoor.humidityPct);
        drawClippedString(matrix, humBuf, rect.x + 3, rect.y + 36, minX, maxX, minY, maxY, theme.textDim);

        int barW = rect.width - 8;
        int fillW = constrain((int)(barW * (indoor.humidityPct / 100.0f)), 0, barW);
        int barY = rect.y + 46;
        if (barY < maxY - 1 && barY >= minY) {
            matrix->drawRect(rect.x + 3, barY, barW, 3, theme.border);
            matrix->fillRect(rect.x + 4, barY + 1, fillW, 1, theme.accent);
        }
        return;
    }

    // CASE 2: Medium or Compact Box -> Clean Carousel without line overlap
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
        if (rect.height < 22) {
            // Ultra-compact single line (e.g. 128x32 top row, rect.height = 15)
            int textY = rect.y + (rect.height - 7) / 2 + offsetY;
            int iconY = rect.y + (rect.height - 8) / 2 + offsetY;

            if (p == 0 && weatherValid) {
                drawMiniWeatherIcon(matrix, rect.x + 2, iconY, minX, maxX, minY, maxY, weather.iconCode);
                char outBuf[12];
                float outT = useFahrenheit ? (weather.temp * 1.8f + 32.0f) : weather.temp;
                if (rect.width < 34) {
                    snprintf(outBuf, sizeof(outBuf), "%.0f\xF8", outT);
                } else {
                    snprintf(outBuf, sizeof(outBuf), "%.0f\xF8%s", outT, useFahrenheit ? "F" : "C");
                }
                drawClippedString(matrix, outBuf, rect.x + 11, textY, minX, maxX, minY, maxY, theme.primary);
            } else if (indoor.valid) {
                drawMiniIndoorIcon(matrix, rect.x + 2, iconY, minX, maxX, minY, maxY, theme.accent);
                float inT = useFahrenheit ? (indoor.temperatureF + tempOffset) : (indoor.temperatureC + tempOffset);
                char inBuf[12];
                if (rect.width < 34) {
                    snprintf(inBuf, sizeof(inBuf), "%.0f\xF8", inT);
                } else {
                    snprintf(inBuf, sizeof(inBuf), "%.0f\xF8%s", inT, useFahrenheit ? "F" : "C");
                }
                drawClippedString(matrix, inBuf, rect.x + 11, textY, minX, maxX, minY, maxY, theme.accent);
            } else {
                drawClippedString(matrix, I18n::getClimateLabel(l), rect.x + 3, textY, minX, maxX, minY, maxY, theme.textDim);
            }
        } else {
            // Multi-line slide layout (rect.height >= 22, e.g. 24..43)
            int textY = rect.y + 3 + offsetY;
            int iconY = rect.y + 3 + offsetY;

            if (p == 0 && weatherValid) {
                drawMiniWeatherIcon(matrix, rect.x + 2, iconY, minX, maxX, minY, maxY, weather.iconCode);
                char outBuf[16];
                float outT = useFahrenheit ? (weather.temp * 1.8f + 32.0f) : weather.temp;
                if (rect.width < 36) {
                    snprintf(outBuf, sizeof(outBuf), "%.0f\xF8", outT);
                } else {
                    snprintf(outBuf, sizeof(outBuf), "%.1f\xF8%s", outT, useFahrenheit ? "F" : "C");
                }
                drawClippedString(matrix, outBuf, rect.x + 12, textY, minX, maxX, minY, maxY, theme.primary);

                int descY = rect.y + 13 + offsetY;
                if (descY < maxY - 6 && descY >= minY) {
                    String desc = I18n::getWeatherCondition(weather.description, l);
                    if (desc.isEmpty()) desc = I18n::getOutdoorLabel(l);
                    desc.toUpperCase();
                    drawClippedString(matrix, desc.substring(0, 8), rect.x + 3, descY, minX, maxX, minY, maxY, theme.textDim);
                }
            } else if (indoor.valid) {
                drawMiniIndoorIcon(matrix, rect.x + 2, iconY, minX, maxX, minY, maxY, theme.accent);
                float inT = useFahrenheit ? (indoor.temperatureF + tempOffset) : (indoor.temperatureC + tempOffset);
                char inBuf[16];
                if (rect.width < 36) {
                    snprintf(inBuf, sizeof(inBuf), "%.0f\xF8", inT);
                } else {
                    snprintf(inBuf, sizeof(inBuf), "%.1f\xF8%s", inT, useFahrenheit ? "F" : "C");
                }
                drawClippedString(matrix, inBuf, rect.x + 12, textY, minX, maxX, minY, maxY, theme.accent);

                int humY = rect.y + 13 + offsetY;
                if (humY < maxY - 6 && humY >= minY) {
                    char humBuf[12];
                    snprintf(humBuf, sizeof(humBuf), "%.0f%%RH", indoor.humidityPct);
                    drawClippedString(matrix, humBuf, rect.x + 3, humY, minX, maxX, minY, maxY, theme.textDim);
                }

                int barY = rect.y + 23 + offsetY;
                if (rect.height >= 30 && barY < maxY - 1 && barY >= minY) {
                    int barW = rect.width - 8;
                    int fillW = constrain((int)(barW * (indoor.humidityPct / 100.0f)), 0, barW);
                    matrix->drawRect(rect.x + 3, barY, barW, 3, theme.border);
                    matrix->fillRect(rect.x + 4, barY + 1, fillW, 1, theme.accent);
                }
            } else {
                drawClippedString(matrix, I18n::getClimateLabel(l), rect.x + 3, textY, minX, maxX, minY, maxY, theme.textDim);
            }
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
