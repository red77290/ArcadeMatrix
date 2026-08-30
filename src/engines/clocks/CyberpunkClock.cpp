#include "CyberpunkClock.h"
#include "../../core/ConfigLoader.h"
#include <stdlib.h>

#define MAX_DROPS 64

struct Drop {
    int x;
    int y;
    int speed;
    int length;
};

static Drop drops[MAX_DROPS];
static bool dropsInit = false;
static int activeDropCount = 0;

CyberpunkClock::CyberpunkClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config), lineY(0), lastFrameTime(0) {}

void CyberpunkClock::draw(const TimeData& t) {
    storedTime = t;
}

void CyberpunkClock::drawTime() {
    matrix->setFont(NULL);
    int w = matrix->width();
    int h = matrix->height();
    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;
    uint16_t green = matrix->color565(0, 220, 100); // Vivid Cyberpunk Neon Green

    if (w < 48 || h > (w * 3) / 2) {
        // Stacked Portrait Layout
        char hStr[8], mStr[8], sStr[8];
        sprintf(hStr, "%02d", storedTime.hours);
        sprintf(mStr, "%02d", storedTime.minutes);
        sprintf(sStr, "%02d", storedTime.seconds);

        int scale = (w >= 64) ? 2 : 1;
        matrix->setTextSize(scale);

        int tx = (w - (12 * scale)) / 2 + offX;
        if (h >= 96) {
            int yH = (h / 6) - (4 * scale) + offY;
            int yM = (h / 2) - (4 * scale) + offY;
            int yS = (5 * h / 6) - (4 * scale) + offY;

            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, yH); matrix->print(hStr);
            matrix->setCursor(tx + 1, yH); matrix->print(hStr);
            matrix->setCursor(tx, yH); matrix->setTextColor(green); matrix->print(hStr);

            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, yM); matrix->print(mStr);
            matrix->setCursor(tx + 1, yM); matrix->print(mStr);
            matrix->setCursor(tx, yM); matrix->setTextColor(green); matrix->print(mStr);

            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, yS); matrix->print(sStr);
            matrix->setCursor(tx + 1, yS); matrix->print(sStr);
            matrix->setCursor(tx, yS); matrix->setTextColor(matrix->color565(0, 140, 60)); matrix->print(sStr);
        } else {
            int yH = (h / 4) - (4 * scale) + offY + 2;
            int yM = (3 * h / 4) - (4 * scale) + offY - 2;

            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, yH); matrix->print(hStr);
            matrix->setCursor(tx + 1, yH); matrix->print(hStr);
            matrix->setCursor(tx, yH); matrix->setTextColor(green); matrix->print(hStr);

            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, yM); matrix->print(mStr);
            matrix->setCursor(tx + 1, yM); matrix->print(mStr);
            matrix->setCursor(tx, yM); matrix->setTextColor(green); matrix->print(mStr);
        }
    } else {
        char timeStr[12];
        sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
        
        matrix->setTextSize(1);
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0 || bh == 0) { bw = 48; bh = 7; }
        
        int maxScaleW = w / bw;
        int maxScaleH = h / bh;
        int sMax = min(maxScaleW, maxScaleH);
        if (sMax < 1) sMax = 1;
        
        int logicalSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
        if (logicalSize < 1) logicalSize = 1;
        int gfxSize = min(logicalSize, sMax);
        
        matrix->setTextSize(gfxSize);
        matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
        
        int x = (w - bw) / 2 + offX - bx;
        int y = (h - bh) / 2 - by + offY;
        
        // Black outline so time remains readable over rain
        matrix->setTextColor(matrix->color565(0, 0, 0));
        matrix->setCursor(x - 1, y); matrix->print(timeStr);
        matrix->setCursor(x + 1, y); matrix->print(timeStr);
        matrix->setCursor(x, y - 1); matrix->print(timeStr);
        matrix->setCursor(x, y + 1); matrix->print(timeStr);

        // Neon Green Time
        matrix->setTextColor(green);
        matrix->setCursor(x, y);
        matrix->print(timeStr);
    }
}

void CyberpunkClock::update() {
    int w = matrix->width();
    int h = matrix->height();
    int targetDrops = min(MAX_DROPS, max(12, w / 5));
    int baseLen = max(6, h / 3);

    if (!dropsInit || activeDropCount != targetDrops) {
        activeDropCount = targetDrops;
        for (int i=0; i<activeDropCount; i++) {
            drops[i].x = rand() % w;
            drops[i].y = (rand() % (h + 20)) - (h + 20);
            drops[i].speed = (rand() % 3) + 2;
            drops[i].length = (rand() % baseLen) + (baseLen / 2);
        }
        dropsInit = true;
    }

    lastFrameTime = millis();
    // Update physics
    for (int i=0; i<activeDropCount; i++) {
        drops[i].y += drops[i].speed;
        if (drops[i].y - drops[i].length > h) {
            drops[i].x = rand() % w;
            drops[i].y = (rand() % 15) * -1;
            drops[i].speed = (rand() % 3) + 2;
            drops[i].length = (rand() % baseLen) + (baseLen / 2);
        }
    }

    // Draw rain drops
    for (int i=0; i<activeDropCount; i++) {
        int dropLen = drops[i].length;
        for (int j=0; j<dropLen; j++) {
            int py = drops[i].y - j;
            if (py >= 0 && py < h) {
                uint16_t color;
                if (j == 0) {
                    color = matrix->color565(255, 255, 255); // Crisp White head
                } else if (j == 1) {
                    color = matrix->color565(120, 255, 200); // Bright Neon Cyan/Green
                } else {
                    int greenVal = 240 - (j * (220 / dropLen));
                    if (greenVal < 20) greenVal = 20;
                    color = matrix->color565(0, greenVal, (j < dropLen / 2) ? 40 : 0);
                }
                matrix->drawPixel(drops[i].x, py, color);
            }
        }
    }

    drawTime();
}
