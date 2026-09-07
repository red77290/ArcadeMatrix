#include "WorldClockWidget.h"
#include "DashboardCommon.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

void WorldClockWidget::render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const std::vector<WorldTimeItem>& worldTimes, const DashboardTheme& theme) {
    if (!matrix || rect.width < 14 || rect.height < 8) return;

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    size_t count = worldTimes.size();
    if (count == 0) return;

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    uint32_t now = millis();

    if (rect.height >= 22) {
        // ====================================================================
        // CASE 1: 2-Line Stacked Display with Animated Vertical Slide Carousel
        // (For 64x256 Tower, 32x128, and any slot with height >= 22)
        // Eliminates overflow: [CITY] on top line, HH:MM on bottom line!
        // ====================================================================
        uint32_t period = 3500;
        uint32_t phase = now % period;
        int page = (now / period) % count;

        int slideY = 0;
        if (phase < 400 && count > 1) {
            float progress = (float)phase / 400.0f;
            slideY = (int)((1.0f - progress) * (rect.height - 4));
        }

        auto renderSlide = [&](int idx, int offsetY) {
            const auto& wt = worldTimes[idx];
            char label[16];
            snprintf(label, sizeof(label), "[%s]", wt.code.c_str());
            char timeBuf[8];
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", wt.hours, wt.minutes);

            int labelW = measureText(strlen(label));
            int timeW = measureText(strlen(timeBuf));

            int xCode = rect.x + max(1, (rect.width - labelW) / 2);
            int xTime = rect.x + max(1, (rect.width - timeW) / 2);

            int yCode = rect.y + (rect.height / 2) - 8 + offsetY;
            int yTime = rect.y + (rect.height / 2) + 1 + offsetY;

            // Extra details if slot is tall (e.g. 64x256 Tower where height is 44)
            if (rect.height >= 40) {
                yCode = rect.y + 4 + offsetY;
                yTime = rect.y + 14 + offsetY;

                // Offset line on row 3
                char offBuf[12];
                int offH = wt.offsetMinutes / 60;
                int offM = abs(wt.offsetMinutes % 60);
                if (offM > 0) {
                    snprintf(offBuf, sizeof(offBuf), "UTC%+d:%02d", offH, offM);
                } else {
                    snprintf(offBuf, sizeof(offBuf), "UTC%+d", offH);
                }
                int offW = measureText(strlen(offBuf));
                int xOff = rect.x + max(1, (rect.width - offW) / 2);
                int yOff = rect.y + 24 + offsetY;
                drawClippedString(matrix, offBuf, xOff, yOff, minX, maxX, minY, maxY, theme.textDim);
            }

            drawClippedString(matrix, label, xCode, yCode, minX, maxX, minY, maxY, theme.secondary);
            drawClippedString(matrix, timeBuf, xTime, yTime, minX, maxX, minY, maxY, theme.primary);
        };

        if (slideY > 0 && count > 1) {
            int prevPage = (page - 1 + count) % count;
            renderSlide(prevPage, -slideY);
            renderSlide(page, (rect.height - 4) - slideY);
        } else {
            renderSlide(page, 0);
        }
    } else {
        // ====================================================================
        // CASE 2: Compact Slot (height < 22, e.g. 128x32 upper row: ~31px wide)
        // Smooth Infinite Rolling Ticker OR Centered Alternating Display
        // ====================================================================
        if (rect.width <= 36) {
            // Very narrow compact slot (e.g. 31x15):
            // Cycle city every 3s, alternating city code and time every 1.5s
            size_t idx = (now / 3000) % count;
            const auto& wt = worldTimes[idx];

            char timeBuf[8];
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", wt.hours, wt.minutes);

            int labelW = measureText(wt.code.length());
            int timeW = measureText(strlen(timeBuf));
            int textY = rect.y + (rect.height - 7) / 2;

            bool showCode = ((now / 1500) % 2 == 0);
            if (showCode) {
                int xCen = rect.x + (rect.width - labelW) / 2;
                drawClippedString(matrix, wt.code.c_str(), xCen, textY, minX, maxX, minY, maxY, theme.secondary);
            } else {
                int xCen = rect.x + (rect.width - timeW) / 2;
                drawClippedString(matrix, timeBuf, xCen, textY, minX, maxX, minY, maxY, theme.primary);
            }
        } else {
            // Wide compact slot (e.g. 62x15 on 256x64):
            // Smooth Continuous Rolling Ticker: [NYC] 08:30   [TYO] 21:30   [LON] 13:30 ...
            struct ItemMetrics {
                char label[8];
                char timeBuf[8];
                int labelW;
                int timeW;
                int itemW;
            };

            size_t safeCount = min(count, (size_t)12);
            ItemMetrics metrics[12];
            int totalW = 0;
            int gapCodeTime = 3;
            int marginItem = 10;

            for (size_t i = 0; i < safeCount; i++) {
                snprintf(metrics[i].label, sizeof(metrics[i].label), "[%s]", worldTimes[i].code.c_str());
                snprintf(metrics[i].timeBuf, sizeof(metrics[i].timeBuf), "%02d:%02d", worldTimes[i].hours, worldTimes[i].minutes);
                metrics[i].labelW = measureText(strlen(metrics[i].label));
                metrics[i].timeW = measureText(strlen(metrics[i].timeBuf));
                metrics[i].itemW = metrics[i].labelW + gapCodeTime + metrics[i].timeW + marginItem;
                totalW += metrics[i].itemW;
            }

            if (totalW < 1) totalW = 1;
            int speedPxPerSec = 16;
            int scrollOffset = (int)((now * speedPxPerSec) / 1000) % totalW;
            int textY = rect.y + (rect.height - 7) / 2;

            for (int k = -1; k < 3; k++) {
                int curX = rect.x + 2 + (k * totalW) - scrollOffset;
                for (size_t i = 0; i < safeCount; i++) {
                    int itemX = curX;
                    int itemW = metrics[i].itemW;
                    curX += itemW;

                    if (itemX + itemW < minX || itemX >= maxX) continue;

                    int codeX = itemX;
                    int timeX = codeX + metrics[i].labelW + gapCodeTime;

                    drawClippedString(matrix, metrics[i].label, codeX, textY, minX, maxX, minY, maxY, theme.secondary);
                    drawClippedString(matrix, metrics[i].timeBuf, timeX, textY, minX, maxX, minY, maxY, theme.primary);
                }
            }
        }
    }

    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);
}
