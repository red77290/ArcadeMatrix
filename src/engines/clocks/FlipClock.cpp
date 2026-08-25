#include "FlipClock.h"
#include "../../core/ConfigLoader.h"
#include <stdio.h>

static const uint8_t digit5x7[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x22, 0x41, 0x49, 0x49, 0x36}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}  // 9
};

FlipClock::FlipClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) {
    for (int i = 0; i < 6; i++) {
        prevDigits[i] = -1;
        oldDigits[i] = -1;
        flipFrame[i] = 0;
    }
    lastFrameTime = 0;
}

void FlipClock::draw(const TimeData& t) {
    storedTime = t;
    
    int curr[6] = {
        t.hours / 10, t.hours % 10,
        t.minutes / 10, t.minutes % 10,
        t.seconds / 10, t.seconds % 10
    };
    
    for (int i = 0; i < 6; i++) {
        if (prevDigits[i] == -1) {
            prevDigits[i] = curr[i];
            oldDigits[i] = curr[i];
        } else if (curr[i] != prevDigits[i] && flipFrame[i] == 0) {
            oldDigits[i] = prevDigits[i];
            prevDigits[i] = curr[i];
            flipFrame[i] = 1;
        }
    }
}

void FlipClock::update() {
    if (millis() - lastFrameTime >= 35) {
        lastFrameTime = millis();
        for (int i = 0; i < 6; i++) {
            if (flipFrame[i] > 0) {
                flipFrame[i]++;
                if (flipFrame[i] > 8) {
                    flipFrame[i] = 0;
                }
            }
        }
    }
    
    drawTime();
}

static void drawFlapRegion(MatrixPanel_I2S_DMA* matrix, int x, int y, int w, int h, int digit, uint16_t cardBgColor, uint16_t textColor, int flapTop, int flapBot, int cropTop, int cropBot, bool isMoving, bool isTopHalf) {
    if (flapTop > flapBot) return;

    // 3D Bevel color palette for rich tactile mechanical relief
    uint16_t highlightCol = matrix->color565(255, 255, 255); // Top/Left 3D highlight
    uint16_t borderCol    = matrix->color565(160, 160, 160); // Neutral card border
    uint16_t shadowCol    = matrix->color565(110, 110, 110); // Bottom/Right 3D shadow
    uint16_t flapEdgeCol  = matrix->color565(50, 50, 50);    // Dark 3D moving flap edge

    int flapH = flapBot - flapTop + 1;

    // 1. Fill card flap background with 3D gradient feel (top half slightly brighter than bottom)
    uint16_t currentBg = cardBgColor;
    if (!isTopHalf && !isMoving) {
        // Bottom half card is slightly shaded to create vertical depth
        currentBg = matrix->color565(215, 215, 215);
    }
    matrix->fillRect(x, y + flapTop, w, flapH, currentBg);

    // 2. Draw 3D bevel side borders
    matrix->drawFastVLine(x, y + flapTop, flapH, isTopHalf ? highlightCol : borderCol);
    matrix->drawFastVLine(x + w - 1, y + flapTop, flapH, shadowCol);

    // 3. Draw top/bottom border or 3D moving flap shadow edge
    if (isMoving) {
        int movingY = (cropTop == 0) ? (y + flapTop) : (y + flapBot);
        matrix->drawFastHLine(x, movingY, w, flapEdgeCol);
    } else {
        if (cropTop == 0) {
            // Card top edge: 3D highlight line
            matrix->drawFastHLine(x, y + flapTop, w, highlightCol);
        } else {
            // Card bottom edge: 3D shadow line
            matrix->drawFastHLine(x, y + flapBot, w, shadowCol);
        }
    }

    // 4. Pixel-perfect clipped digit rendering
    if (digit >= 0 && digit <= 9) {
        int scale = h / 10;
        if (scale < 1) scale = 1;
        int charW = 5 * scale;
        int charH = 7 * scale;
        int charX = x + (w - charW) / 2;
        int charY = y + (h - charH) / 2;

        for (int col = 0; col < 5; col++) {
            uint8_t bits = digit5x7[digit][col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row)) {
                    for (int sy = 0; sy < scale; sy++) {
                        int localY = (charY - y) + (row * scale) + sy;
                        if (localY >= cropTop && localY <= cropBot) {
                            int flapLocalY;
                            if (cropBot == cropTop) {
                                flapLocalY = flapTop;
                            } else {
                                flapLocalY = flapTop + (localY - cropTop) * (flapBot - flapTop) / (cropBot - cropTop);
                            }
                            int drawY = y + flapLocalY;
                            for (int sx = 0; sx < scale; sx++) {
                                int drawX = charX + (col * scale) + sx;
                                matrix->drawPixel(drawX, drawY, textColor);
                            }
                        }
                    }
                }
            }
        }
    }
}

void FlipClock::drawPanel(int x, int y, int w, int h, const char* curText, const char* oldText, uint16_t bgColor, uint16_t textColor, int frame) {
    uint16_t slotUpperShadow = matrix->color565(100, 100, 100);
    uint16_t splitCol        = matrix->color565(0, 0, 0);       // Pure black split slot
    uint16_t slotLowerBevel  = matrix->color565(240, 240, 240);

    int curDigit = curText[0] - '0';
    int oldDigit = oldText[0] - '0';

    int topEnd = (h / 2) - 1;
    int botStart = h / 2;
    int botEnd = h - 1;

    if (frame <= 0 || frame > 8) {
        // Static 3D Card Display
        drawFlapRegion(matrix, x, y, w, h, curDigit, bgColor, textColor, 0, topEnd, 0, topEnd, false, true);
        drawFlapRegion(matrix, x, y, w, h, curDigit, bgColor, textColor, botStart, botEnd, botStart, botEnd, false, false);
        
        // 3D Mechanical Split Slot (Fente mécanique en relief)
        if (botStart > 0) matrix->drawFastHLine(x + 1, y + botStart - 1, w - 2, slotUpperShadow);
        matrix->drawFastHLine(x, y + botStart, w, splitCol);
        if (botStart + 1 < h) matrix->drawFastHLine(x + 1, y + botStart + 1, w - 2, slotLowerBevel);
    } else {
        // Animated 3D Flip Mechanism (Rust-identical 2-phase flap)
        // 1. Static TOP half: NEW digit top half
        drawFlapRegion(matrix, x, y, w, h, curDigit, bgColor, textColor, 0, topEnd, 0, topEnd, false, true);

        // 2. Static BOT half: OLD digit bot half
        drawFlapRegion(matrix, x, y, w, h, oldDigit, bgColor, textColor, botStart, botEnd, botStart, botEnd, false, false);

        // 3. Moving 3D flap
        if (frame <= 4) {
            // Phase 1: Top half of OLD digit falling down
            int shrinkPx = (frame * (h / 2)) / 4;
            int flapTop = shrinkPx;
            int flapBot = topEnd;
            drawFlapRegion(matrix, x, y, w, h, oldDigit, bgColor, textColor, flapTop, flapBot, 0, topEnd, true, true);
        } else {
            // Phase 2: Bottom half of NEW digit expanding down
            int expandPx = ((frame - 4) * (h / 2)) / 4;
            int flapTop = botStart;
            int flapBot = botStart + expandPx - 1;
            drawFlapRegion(matrix, x, y, w, h, curDigit, bgColor, textColor, flapTop, flapBot, botStart, botEnd, true, false);
        }

        // 4. 3D Mechanical Split Slot
        if (botStart > 0) matrix->drawFastHLine(x + 1, y + botStart - 1, w - 2, slotUpperShadow);
        matrix->drawFastHLine(x, y + botStart, w, splitCol);
        if (botStart + 1 < h) matrix->drawFastHLine(x + 1, y + botStart + 1, w - 2, slotLowerBevel);
    }
}

void FlipClock::drawTime() {
    uint16_t bgColor  = matrix->color565(245, 245, 245);
    uint16_t textColor = matrix->color565(15, 15, 15);
    uint16_t dotCol    = matrix->color565(210, 210, 210);
    
    int logicalSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 2;
    int spacing = (matrix->width() <= 64) ? 1 : 2;
    int dotWidth = (matrix->width() <= 64) ? 2 : 3;
    
    int maxW = (matrix->width() - (7 * spacing) - (2 * dotWidth)) / 6;
    int maxH = matrix->height() - 4;
    if (maxW * 1.4 < maxH) maxH = maxW * 1.4;
    if (maxH / 1.4 < maxW) maxW = maxH / 1.4;
    
    int panelW = 10;
    int panelH = 14;
    if (logicalSize >= 5) { panelW = maxW * 1.5; panelH = maxH * 1.5; }
    else if (logicalSize == 4) { panelW = maxW; panelH = maxH; }
    else if (logicalSize == 3) { panelW = maxW * 3 / 4; panelH = maxH * 3 / 4; }
    else if (logicalSize == 2) { panelW = maxW * 2 / 4; panelH = maxH * 2 / 4; }
    else { panelW = maxW / 4; panelH = maxH / 4; }
    
    if (panelW < 4) panelW = 4;
    if (panelH < 8) panelH = 8;
    
    int totalW = (panelW * 6) + (spacing * 6) + (2 * (dotWidth + spacing * 2));
    int startX = (matrix->width() - totalW) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0);
    int y = (matrix->height() - panelH) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0);
    
    int curr[6] = {
        storedTime.hours / 10, storedTime.hours % 10,
        storedTime.minutes / 10, storedTime.minutes % 10,
        storedTime.seconds / 10, storedTime.seconds % 10
    };

    char curStr[6][8];
    char oldStr[6][8];
    for (int i = 0; i < 6; i++) {
        int cDigit = (curr[i] >= 0) ? (curr[i] % 10) : 0;
        int oDigit = (oldDigits[i] >= 0) ? (oldDigits[i] % 10) : cDigit;
        snprintf(curStr[i], sizeof(curStr[i]), "%d", cDigit);
        snprintf(oldStr[i], sizeof(oldStr[i]), "%d", oDigit);
    }
    
    int cx = startX;
    for (int i = 0; i < 6; i++) {
        drawPanel(cx, y, panelW, panelH, curStr[i], oldStr[i], bgColor, textColor, flipFrame[i]);
        cx += panelW + spacing;
        
        if (i == 1 || i == 3) {
            // Perfectly centered 3D colon dots
            cx += spacing;
            int dotSize = (panelH >= 16) ? 2 : 1;
            int dotY1 = y + (panelH / 3) - (dotSize / 2);
            int dotY2 = y + (2 * panelH / 3) - (dotSize / 2);
            int dotX  = cx + (dotWidth - dotSize) / 2;
            
            matrix->fillRect(dotX, dotY1, dotSize, dotSize, dotCol);
            matrix->fillRect(dotX, dotY2, dotSize, dotSize, dotCol);
            
            cx += dotWidth + spacing;
        }
    }
}
