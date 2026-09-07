#include "SysInfoWidget.h"
#include "DashboardCommon.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

void SysInfoWidget::render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const SystemData& sys, const DashboardTheme& theme) {
    if (!matrix || rect.width < 14 || rect.height < 8) return;

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    int rssiBars = 1;
    if (sys.wifiRssi > -60) rssiBars = 4;
    else if (sys.wifiRssi > -70) rssiBars = 3;
    else if (sys.wifiRssi > -80) rssiBars = 2;

    if (rect.height <= 20) {
        // Compact slot layout (e.g. 128x32 upper row: ~31px wide, 15px high)
        // 1. Draw 4-bar WiFi signal meter in right corner
        int wx = rect.x + rect.width - 10;
        int wy = rect.y + rect.height - 3;
        for (int b = 0; b < 4; b++) {
            int bH = (b + 1) * 2;
            uint16_t c = (b < rssiBars) ? theme.accent : theme.border;
            for (int h = 0; h < bH; h++) {
                drawClippedPixel(matrix, wx + (b * 2), wy - h, minX, maxX, minY, maxY, c);
            }
        }

        int availW = rect.width - 12;
        uint32_t nowSec = millis() / 1000;
        int cycle = (nowSec / 3) % 2;

        const char* label = (cycle == 0) ? "CPU" : "RAM";
        float usageVal = (cycle == 0) ? 15.0f : sys.ramUsagePct;

        // Label on top
        drawClippedString(matrix, label, rect.x + 2, rect.y + 2, minX, rect.x + availW, minY, maxY, theme.primary);

        // Dynamic colored gauge bar on bottom
        int barX = rect.x + 2;
        int barW = max(4, availW - 3);
        int barY = rect.y + rect.height - 4;
        int barH = 2;
        float usageRatio = constrain(usageVal / 100.0f, 0.0f, 1.0f);

        for (int px = 0; px < barW; px++) {
            float colRatio = (float)(px + 0.5f) / (float)barW;
            uint16_t col;
            if (colRatio <= usageRatio) {
                if (colRatio < 0.20f) col = matrix->color565(0, 180, 255);
                else if (colRatio < 0.40f) col = matrix->color565(0, 230, 80);
                else if (colRatio < 0.60f) col = matrix->color565(255, 215, 0);
                else if (colRatio < 0.80f) col = matrix->color565(255, 130, 0);
                else col = matrix->color565(255, 45, 45);
            } else {
                col = matrix->color565(25, 30, 45);
            }

            for (int py = 0; py < barH; py++) {
                drawClippedPixel(matrix, barX + px, barY + py, minX, maxX, minY, maxY, col);
            }
        }
    } else {
        // Multi-page animated Carousel for taller slots (256x64)
        int numPages = 3;
        uint32_t period = 4000;
        uint32_t phase = millis() % period;
        int page = (millis() / period) % numPages;

        int slideY = 0;
        if (phase < 400) {
            float progress = (float)phase / 400.0f;
            slideY = (int)((1.0f - progress) * (rect.height - 4));
        }

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
    }

    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);
}
