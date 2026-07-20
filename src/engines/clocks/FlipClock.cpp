#include "FlipClock.h"
#include "../../core/ConfigLoader.h"

extern ConfigLoader config;

FlipClock::FlipClock(MatrixPanel_I2S_DMA* display) : ClockFace(display) {
    for (int i=0; i<6; i++) {
        prevDigits[i] = -1;
        flippingPanels[i] = false;
    }
    isFlipping = false;
    flipFrame = 0;
    lastFrameTime = 0;
}

void FlipClock::draw(const TimeData& t) {
    storedTime = t;
    
    int curr[6];
    curr[0] = t.hours / 10;
    curr[1] = t.hours % 10;
    curr[2] = t.minutes / 10;
    curr[3] = t.minutes % 10;
    curr[4] = t.seconds / 10;
    curr[5] = t.seconds % 10;
    
    bool changed = false;
    for (int i=0; i<6; i++) {
        if (prevDigits[i] != -1 && curr[i] != prevDigits[i]) {
            changed = true;
            flippingPanels[i] = true;
        } else {
            flippingPanels[i] = false;
        }
        prevDigits[i] = curr[i];
    }
    
    if (changed) {
        isFlipping = true;
        flipFrame = 0;
        lastFrameTime = millis();
    }
}

void FlipClock::update() {
    if (!isFlipping) {
        drawStaticTime();
        return;
    }

    // Faster animation (20ms per frame)
    if (millis() - lastFrameTime > 20) {
        lastFrameTime = millis();
        flipFrame++;
    }
    
    drawFlappingTime();

    if (flipFrame > 8) { // 8 frames of flip
        isFlipping = false;
        for(int i=0; i<6; i++) flippingPanels[i] = false;
    }
}

void FlipClock::drawPanel(int x, int y, int w, int h, const char* text, uint16_t bgColor, uint16_t textColor, bool isFlippingPanel, int flipOffset) {
    if (isFlippingPanel) {
        // Shrink height towards middle
        int shrink = flipOffset;
        if (shrink > h/2) shrink = h - flipOffset; // expand back
        if (shrink < 0) shrink = 0;
        
        matrix->fillRect(x, y + shrink, w, h - (shrink*2), bgColor);
        
        // Horizontal separation line
        matrix->drawFastHLine(x, y + (h/2), w, matrix->color565(0, 0, 0));
        
    } else {
        matrix->fillRect(x, y, w, h, bgColor);
        
        int gfxSize = h / 10;
        if (gfxSize < 1) gfxSize = 1;
        
        matrix->setTextSize(gfxSize);
        matrix->setFont(nullptr);
        matrix->setTextColor(textColor);
        
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
        matrix->setCursor(x + (w - bw)/2 + 1, y + (h - bh)/2 + 1);
        matrix->print(text);
        
        // Horizontal separation line
        matrix->drawFastHLine(x, y + (h/2), w, matrix->color565(0, 0, 0));
    }
}

void FlipClock::drawStaticTime() {
    uint16_t bgColor = matrix->color565(255, 255, 255);
    uint16_t textColor = matrix->color565(0, 0, 0);
    
    int logicalSize = config.time.clock_size > 0 ? config.time.clock_size : 2;
    int spacing = (matrix->width() <= 64) ? 1 : 2;
    
    // Max responsive calculations
    // Total width = (panelW * 6) + (spacing * 7) + 4
    int maxW = (matrix->width() - (7 * spacing) - 4) / 6;
    int maxH = matrix->height() - 2;
    if (maxW * 1.5 < maxH) maxH = maxW * 1.5;
    if (maxH / 1.5 < maxW) maxW = maxH / 1.5;
    
    int panelW = 10;
    int panelH = 14;
    if (logicalSize == 3) { panelW = maxW; panelH = maxH; }
    else if (logicalSize == 2) { panelW = maxW * 2 / 3; panelH = maxH * 2 / 3; }
    else { panelW = maxW / 3; panelH = maxH / 3; }
    
    // Lower the floor for 64x32 compatibility
    if (panelW < 3) panelW = 3;
    if (panelH < 5) panelH = 5;
    
    int totalW = (panelW * 6) + (spacing * 7) + 4;
    int startX = (matrix->width() - totalW) / 2 + config.time.clock_offset_x;
    int y = (matrix->height() - panelH) / 2 + config.time.clock_offset_y;
    
    char d[6][2];
    sprintf(d[0], "%d", storedTime.hours / 10);
    sprintf(d[1], "%d", storedTime.hours % 10);
    sprintf(d[2], "%d", storedTime.minutes / 10);
    sprintf(d[3], "%d", storedTime.minutes % 10);
    sprintf(d[4], "%d", storedTime.seconds / 10);
    sprintf(d[5], "%d", storedTime.seconds % 10);
    
    int cx = startX;
    for(int i=0; i<6; i++) {
        drawPanel(cx, y, panelW, panelH, d[i], bgColor, textColor, false, 0);
        cx += panelW + spacing;
        if (i == 1 || i == 3) {
            matrix->fillRect(cx, y + 3, 2, 2, bgColor);
            matrix->fillRect(cx, y + 9, 2, 2, bgColor);
            cx += 2 + spacing;
        }
    }
}

void FlipClock::drawFlappingTime() {
    uint16_t bgColor = matrix->color565(255, 255, 255);
    uint16_t textColor = matrix->color565(0, 0, 0);
    
    int logicalSize = config.time.clock_size > 0 ? config.time.clock_size : 2;
    int spacing = (matrix->width() <= 64) ? 1 : 2;
    
    // Max responsive calculations
    // Total width = (panelW * 6) + (spacing * 7) + 4
    int maxW = (matrix->width() - (7 * spacing) - 4) / 6;
    int maxH = matrix->height() - 2;
    if (maxW * 1.5 < maxH) maxH = maxW * 1.5;
    if (maxH / 1.5 < maxW) maxW = maxH / 1.5;
    
    int panelW = 10;
    int panelH = 14;
    if (logicalSize == 3) { panelW = maxW; panelH = maxH; }
    else if (logicalSize == 2) { panelW = maxW * 2 / 3; panelH = maxH * 2 / 3; }
    else { panelW = maxW / 3; panelH = maxH / 3; }
    
    // Lower the floor for 64x32 compatibility
    if (panelW < 3) panelW = 3;
    if (panelH < 5) panelH = 5;
    
    int totalW = (panelW * 6) + (spacing * 7) + 4;
    int startX = (matrix->width() - totalW) / 2 + config.time.clock_offset_x;
    int y = (matrix->height() - panelH) / 2 + config.time.clock_offset_y;
    
    char d[6][2];
    sprintf(d[0], "%d", storedTime.hours / 10);
    sprintf(d[1], "%d", storedTime.hours % 10);
    sprintf(d[2], "%d", storedTime.minutes / 10);
    sprintf(d[3], "%d", storedTime.minutes % 10);
    sprintf(d[4], "%d", storedTime.seconds / 10);
    sprintf(d[5], "%d", storedTime.seconds % 10);
    
    int cx = startX;
    for(int i=0; i<6; i++) {
        drawPanel(cx, y, panelW, panelH, d[i], bgColor, textColor, flippingPanels[i], flipFrame);
        cx += panelW + spacing;
        if (i == 1 || i == 3) {
            matrix->fillRect(cx, y + 3, 2, 2, bgColor);
            matrix->fillRect(cx, y + 9, 2, 2, bgColor);
            cx += 2 + spacing;
        }
    }
}
