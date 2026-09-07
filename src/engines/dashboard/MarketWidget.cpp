#include "MarketWidget.h"
#include "DashboardCommon.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <math.h>

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

static void renderMarketIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, int minX, int maxX, int minY, int maxY, const MarketItem& item) {
    if (item.hasIcon) {
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                uint16_t color = item.iconPixels[r * 8 + c];
                if (color != 0) {
                    drawClippedPixel(matrix, x + c, y + r, minX, maxX, minY, maxY, color);
                }
            }
        }
    } else {
        drawClippedMarketIcon8x8(matrix, x, y, minX, maxX, minY, maxY, item.symbol);
    }
}

void MarketWidget::render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const std::vector<MarketItem>& items, const DashboardTheme& theme) {
    if (!matrix || rect.width < 10 || rect.height < 8) return;

    matrix->fillRect(rect.x, rect.y, rect.width, rect.height, theme.panelBg);
    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);

    size_t count = items.size();
    if (count == 0) return;

    int minX = rect.x + 1;
    int maxX = rect.x + rect.width - 1;
    int minY = rect.y + 1;
    int maxY = rect.y + rect.height - 1;

    if (rect.height <= 20) {
        // Single-Line Continuous Smooth Horizontal Ticker (128x32, 128x64, 256x64 bottom bar)
        // Format: [8x8 Icon] SYM $PRICE +X.X% (all in one line, vertically centered)
        struct ItemMetrics {
            int symW;
            int priceW;
            int chgW;
            int itemW;
            char priceBuf[16];
            char chgBuf[10];
            uint16_t trendCol;
        };

        size_t safeCount = min(count, (size_t)16);
        ItemMetrics metrics[16];
        int totalW = 0;
        int gapIconSym = 3;
        int gapSymPrice = 5;
        int gapPriceChg = 5;
        int marginRight = 14;

        for (size_t i = 0; i < safeCount; i++) {
            metrics[i].symW = measureText(items[i].symbol.length());
            formatMarketPrice(metrics[i].priceBuf, sizeof(metrics[i].priceBuf), items[i].price);
            metrics[i].priceW = measureText(strlen(metrics[i].priceBuf));
            metrics[i].trendCol = (items[i].change24h >= 0) ? theme.green : theme.red;
            snprintf(metrics[i].chgBuf, sizeof(metrics[i].chgBuf), "%c%.1f%%", items[i].change24h >= 0 ? '+' : '-', fabsf(items[i].change24h));
            metrics[i].chgW = measureText(strlen(metrics[i].chgBuf));

            metrics[i].itemW = 8 + gapIconSym + metrics[i].symW + gapSymPrice + metrics[i].priceW + gapPriceChg + metrics[i].chgW + marginRight;
            totalW += metrics[i].itemW;
        }

        if (totalW < 1) totalW = 1;
        uint32_t now = millis();
        int speedPxPerSec = 16;
        int scrollOffset = (int)((now * speedPxPerSec) / 1000) % totalW;

        int iconY = rect.y + (rect.height - 8) / 2;
        int textY = rect.y + (rect.height - 7) / 2;

        for (int k = -1; k < 3; k++) {
            int curX = rect.x + 2 + (k * totalW) - scrollOffset;
            for (size_t i = 0; i < safeCount; i++) {
                int itemX = curX;
                int itemW = metrics[i].itemW;
                curX += itemW;

                if (itemX + itemW < minX || itemX >= maxX) continue;

                int iconX = itemX;
                int symX = iconX + 8 + gapIconSym;
                int priceX = symX + metrics[i].symW + gapSymPrice;
                int chgX = priceX + metrics[i].priceW + gapPriceChg;

                renderMarketIcon(matrix, iconX, iconY, minX, maxX, minY, maxY, items[i]);
                drawClippedString(matrix, items[i].symbol, symX, textY, minX, maxX, minY, maxY, theme.text);
                drawClippedString(matrix, metrics[i].priceBuf, priceX, textY, minX, maxX, minY, maxY, theme.primary);
                drawClippedString(matrix, metrics[i].chgBuf, chgX, textY, minX, maxX, minY, maxY, metrics[i].trendCol);
            }
        }
    } else if (rect.width >= 100 && rect.height <= 36) {
        // Multi-line Horizontal Ticker for taller slots (e.g. 256x64 when market has 30px+ height)
        int itemW = 56;
        int totalW = (int)count * itemW;
        uint32_t now = millis();
        int scrollOffset = (int)((now * 12) / 1000) % max(1, totalW);

        for (size_t i = 0; i < count; i++) {
            int slotBaseX = (int)(i * itemW) - scrollOffset;

            while (slotBaseX < -itemW) slotBaseX += totalW;
            while (slotBaseX > rect.width) slotBaseX -= totalW;

            for (int k = 0; k < 2; k++) {
                int posX = rect.x + 2 + slotBaseX + (k * totalW);
                if (posX + itemW < minX || posX >= maxX) continue;

                renderMarketIcon(matrix, posX, rect.y + 3, minX, maxX, minY, maxY, items[i]);
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
        // Vertical rolling list for narrow portrait layouts
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

            renderMarketIcon(matrix, rect.x + 3, posY + 2, minX, maxX, minY, maxY, items[i]);
            drawClippedString(matrix, items[i].symbol, rect.x + 13, posY, minX, maxX, minY, maxY, theme.text);

            char priceBuf[16];
            formatMarketPrice(priceBuf, sizeof(priceBuf), items[i].price);
            drawClippedString(matrix, priceBuf, rect.x + 13, posY + 7, minX, maxX, minY, maxY, theme.primary);
        }
    }

    matrix->drawRect(rect.x, rect.y, rect.width, rect.height, theme.border);
}
