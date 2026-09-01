#include "VersusClock.h"
#include "../../core/ConfigLoader.h"
#include <math.h>

VersusClock::VersusClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) {
    storedTime = {0, 0, 0};
    lastMinute = -1;
    animating = false; // Kept for compatibility but unused
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
    uint16_t border = matrix->color565(180, 180, 180);
    uint16_t bg = matrix->color565(50, 0, 0);
    uint16_t fill = matrix->color565(255, 220, 0);
    
    if (hpPercent <= 0.3f) {
        fill = matrix->color565(255, 40, 40);
    }
    
    // Background
    matrix->fillRect(x, y, width + 1, height + 1, bg);
    
    // Border corners
    matrix->drawPixel(x, y, border);
    matrix->drawPixel(x + width, y, border);
    matrix->drawPixel(x, y + height, border);
    matrix->drawPixel(x + width, y + height, border);
    
    int fillWidth = (int)(width * hpPercent);
    if (fillWidth > 0) {
        if (isPlayer1) {
            // Fill right-aligned, drains from left
            int xStart = x + width - fillWidth + 1;
            matrix->fillRect(xStart, y + 1, fillWidth, height - 1, fill);
        } else {
            // Fill left-aligned, drains from right
            matrix->fillRect(x, y + 1, fillWidth, height - 1, fill);
        }
    }
}

void VersusClock::drawKO(int x, int y) {
    uint16_t red = matrix->color565(255, 0, 0);
    
    // K
    uint8_t k[5][6] = {
        {1, 0, 0, 0, 1, 0},
        {1, 0, 0, 1, 0, 0},
        {1, 1, 0, 0, 0, 0},
        {1, 0, 0, 1, 0, 0},
        {1, 0, 0, 0, 1, 0}
    };
    for(int row=0; row<5; row++) {
        for(int col=0; col<6; col++) {
            if (k[row][col]) matrix->drawPixel(x + col, y + row, red);
        }
    }
    
    // O
    uint8_t o[5][6] = {
        {0, 1, 1, 1, 0, 0},
        {1, 0, 0, 0, 1, 0},
        {1, 0, 0, 0, 1, 0},
        {1, 0, 0, 0, 1, 0},
        {0, 1, 1, 1, 0, 0}
    };
    for(int row=0; row<5; row++) {
        for(int col=0; col<6; col++) {
            if (o[row][col]) matrix->drawPixel(x + 7 + col, y + row, red);
        }
    }
}

void VersusClock::update() {
    if (true) {
        lastFrameTime = millis();
        animFrame++;
    }
    
    matrix->fillScreen(0);
    
    int w = matrix->width();
    int h = matrix->height();
    
    float p1HP = 1.0f - min(1.0f, storedTime.hours / 23.0f);
    float p2HP = 1.0f - min(1.0f, storedTime.minutes / 59.0f);

    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;

    uint16_t color1 = matrix->color565(255, 255, 255);
    if ((engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[0] == '#') {
        long c1 = strtol(&(engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[1], NULL, 16);
        color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
    }
    if (color1 == 0) color1 = matrix->color565(255, 255, 255);

    int bounce1 = (int)(sin(animFrame * 0.2f) * 2.0f);
    int bounce2 = (int)(cos(animFrame * 0.2f) * 2.0f);
    uint16_t blue = matrix->color565(0, 200, 255);
    uint16_t orange = matrix->color565(255, 100, 0);

    if (w < 48 || h > (w * 3) / 2) {
        // Portrait / Tate Layout
        if ((animFrame / 10) % 2 == 0) {
            drawKO((w - 13) / 2 + offX, 1 + offY);
        }
        drawHealthBar(2 + offX, 7 + offY, w - 4, 3, p1HP, true);
        drawHealthBar(2 + offX, 11 + offY, w - 4, 3, p2HP, false);

        int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 2;
        int scale = (w >= 64) ? 3 : 2;
        if (gfxSize >= 1 && gfxSize <= 4) scale = min(scale, gfxSize);
        matrix->setFont(NULL);
        matrix->setTextSize(scale);

        char hStr[8], mStr[8];
        sprintf(hStr, "%02d", storedTime.hours);
        sprintf(mStr, "%02d", storedTime.minutes);

        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds("88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 11 * scale; if (bh == 0) bh = 7 * scale;

        int tx = (w - bw) / 2 + offX;
        int tyH = (h / 2) - bh - 1 + offY;
        int tyM = (h / 2) + 3 + offY;

        matrix->setTextColor(0);
        matrix->setCursor(tx - 1, tyH); matrix->print(hStr);
        matrix->setCursor(tx + 1, tyH); matrix->print(hStr);
        matrix->setCursor(tx, tyH); matrix->setTextColor(color1); matrix->print(hStr);

        matrix->setTextColor(0);
        matrix->setCursor(tx - 1, tyM); matrix->print(mStr);
        matrix->setCursor(tx + 1, tyM); matrix->print(mStr);
        matrix->setCursor(tx, tyM); matrix->setTextColor(color1); matrix->print(mStr);

        matrix->fillRect(2, h - 8 + bounce1, 5, 5, blue);
        matrix->fillRect(w - 7, h - 8 + bounce2, 5, 5, orange);
    } else {
        // Landscape / Widescreen Layout
        int barW = w / 2 - 10;
        drawHealthBar(5 + offX, 2 + offY, barW, 4, p1HP, true);
        drawHealthBar(w - 5 - barW + offX, 2 + offY, barW, 4, p2HP, false);
        
        if ((animFrame / 10) % 2 == 0) {
            drawKO(w / 2 - 7 + offX, 0 + offY);
        }
        
        char timeStr[12];
        sprintf(timeStr, "%02d:%02d", storedTime.hours, storedTime.minutes);
        
        int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 2;
        matrix->setTextSize(gfxSize);
        matrix->setFont(NULL);
        
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds("88:88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 30; if (bh == 0) bh = 7 * gfxSize;
        
        int tx = (w - bw) / 2 + offX;
        int ty = (h - bh) / 2 + 4 + offY;
        
        matrix->setTextColor(0);
        matrix->setCursor(tx - 1, ty); matrix->print(timeStr);
        matrix->setCursor(tx + 1, ty); matrix->print(timeStr);
        matrix->setCursor(tx, ty - 1); matrix->print(timeStr);
        matrix->setCursor(tx, ty + 1); matrix->print(timeStr);

        matrix->setCursor(tx, ty);
        matrix->setTextColor(color1);
        matrix->print(timeStr);
        
        matrix->fillRect(10, h - 8 + bounce1, 6, 6, blue);
        matrix->fillRect(w - 16, h - 8 + bounce2, 6, 6, orange);
    }
}
