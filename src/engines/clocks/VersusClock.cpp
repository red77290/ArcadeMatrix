#include "VersusClock.h"
#include "ConfigLoader.h"
#include <math.h>

extern ConfigLoader config;

VersusClock::VersusClock(MatrixPanel_I2S_DMA* display) : ClockFace(display) {
    storedTime = {0, 0, 0};
    lastMinute = -1;
    animating = false;
    currentP1HP = 100.0f;
    targetP1HP = 100.0f;
    currentP2HP = 100.0f;
    targetP2HP = 100.0f;
    lastFrameTime = 0;
    animFrame = 0;
}

void VersusClock::draw(const TimeData& t) {
    storedTime = t;
}

void VersusClock::drawHealthBar(int x, int y, int width, int height, float hpPercent, bool isPlayer1) {
    uint16_t border = matrix->color565(255, 255, 255);
    uint16_t bg = matrix->color565(100, 0, 0);
    uint16_t fill = matrix->color565(255, 255, 0);
    uint16_t dmg = matrix->color565(255, 0, 0);
    
    if (hpPercent > 0.5f) {
        fill = matrix->color565(0, 255, 0);
    } else if (hpPercent > 0.2f) {
        fill = matrix->color565(255, 255, 0);
    } else {
        fill = matrix->color565(255, 0, 0);
    }
    
    // Border
    matrix->drawRect(x, y, width, height, border);
    // Background
    matrix->fillRect(x + 1, y + 1, width - 2, height - 2, bg);
    
    int fillWidth = (int)((width - 2) * hpPercent);
    if (fillWidth > 0) {
        if (isPlayer1) {
            matrix->fillRect(x + 1, y + 1, fillWidth, height - 2, fill);
        } else {
            matrix->fillRect(x + width - 1 - fillWidth, y + 1, fillWidth, height - 2, fill);
        }
    }
}

void VersusClock::update() {
    if (lastMinute == -1) {
        lastMinute = storedTime.minutes;
        targetP1HP = (storedTime.hours / 24.0f);
        targetP2HP = (storedTime.minutes / 60.0f);
        currentP1HP = targetP1HP;
        currentP2HP = targetP2HP;
    } else if (lastMinute != storedTime.minutes && !animating) {
        animating = true;
        // Strike animation: drop P2 HP
        targetP2HP = (storedTime.minutes / 60.0f);
        targetP1HP = (storedTime.hours / 24.0f);
        if (storedTime.minutes == 0) {
            currentP2HP = 1.0f; // Reset bar visually before draining to 0
        }
    }
    
    if (millis() - lastFrameTime > 30) {
        lastFrameTime = millis();
        animFrame++;
        
        matrix->fillScreen(0);
        
        // Animate HP
        if (animating) {
            if (currentP1HP > targetP1HP) currentP1HP -= 0.02f;
            if (currentP1HP < targetP1HP) currentP1HP += 0.02f;
            if (abs(currentP1HP - targetP1HP) < 0.02f) currentP1HP = targetP1HP;
            
            if (currentP2HP > targetP2HP) currentP2HP -= 0.02f;
            if (currentP2HP < targetP2HP) currentP2HP += 0.02f;
            if (abs(currentP2HP - targetP2HP) < 0.02f) currentP2HP = targetP2HP;
            
            if (currentP1HP == targetP1HP && currentP2HP == targetP2HP) {
                animating = false;
                lastMinute = storedTime.minutes;
            }
        }
        
        // Draw Health Bars
        int barWidth = matrix->width() / 2 - 4;
        drawHealthBar(2, 2, barWidth, 6, currentP1HP, true);
        drawHealthBar(matrix->width() / 2 + 2, 2, barWidth, 6, currentP2HP, false);
        
        // Draw KO Text in middle
        uint16_t red = matrix->color565(255, 0, 0);
        uint16_t yellow = matrix->color565(255, 255, 0);
        
        matrix->setTextSize(1);
        matrix->setFont(NULL);
        matrix->setCursor(matrix->width() / 2 - 5, 2);
        matrix->setTextColor(animFrame % 20 < 10 ? red : yellow);
        matrix->print("V");
        
        // Draw Time below bars
        char timeStr[12];
        sprintf(timeStr, "%02d:%02d", storedTime.hours, storedTime.minutes);
        
        int gfxSize = config.time.clock_size > 0 ? config.time.clock_size : 2;
        matrix->setTextSize(gfxSize);
        
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 30; if (bh == 0) bh = 7 * gfxSize;
        
        int tx = (matrix->width() - bw) / 2 + config.time.clock_offset_x;
        int ty = (matrix->height() - bh) / 2 + 10 + config.time.clock_offset_y; // Push down below bars
        
        uint16_t color1 = matrix->color565(255, 255, 255);
        if (config.time.clock_color_1[0] == '#') {
            long c1 = strtol(&config.time.clock_color_1[1], NULL, 16);
            color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
        }
        
        // Shake effect when changing
        if (animating) {
            tx += (rand() % 3) - 1;
            ty += (rand() % 3) - 1;
        }
        
        matrix->setCursor(tx, ty);
        matrix->setTextColor(color1);
        matrix->print(timeStr);
        
        // Optional pulsing hit sparks if animating
        if (animating && (rand() % 100 > 70)) {
            int sparkX = tx + (rand() % bw);
            int sparkY = ty + (rand() % bh);
            matrix->fillCircle(sparkX, sparkY, 1 + (rand() % 2), matrix->color565(255, 255, 255));
        }
    }
}
