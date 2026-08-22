#include "CyberpunkClock.h"
#include "../../core/ConfigLoader.h"
#include <stdlib.h>

#define NUM_DROPS 8

struct Drop {
    int x;
    int y;
    int speed;
    int length;
};

static Drop drops[NUM_DROPS];
static bool dropsInit = false;

CyberpunkClock::CyberpunkClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config), lineY(0), lastFrameTime(0) {}

void CyberpunkClock::draw(const TimeData& t) {
    storedTime = t;
}

void CyberpunkClock::drawTime() {
    matrix->setFont(NULL);
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    
    matrix->setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
    if (bw == 0 || bh == 0) { bw = 48; bh = 7; } // Fallback
    
    int maxScaleW = matrix->width() / bw;
    int maxScaleH = matrix->height() / bh;
    int sMax = min(maxScaleW, maxScaleH);
    if (sMax < 1) sMax = 1;
    
    int logicalSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 2;
    int gfxSize = 1;
    if (logicalSize >= 5) gfxSize = sMax + 1;
    else if (logicalSize == 4) gfxSize = sMax;
    else if (logicalSize == 3) gfxSize = max(1, (sMax * 3) / 4);
    else if (logicalSize == 2) gfxSize = max(1, (sMax * 2) / 4);
    else gfxSize = max(1, sMax / 4);
    
    matrix->setTextSize(gfxSize);
    matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
    
    int x = (matrix->width() - bw) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0) - bx;
    int y = (matrix->height() - bh) / 2 - by + (engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0);
    
    // Black outline so time remains readable over rain
    matrix->setTextColor(matrix->color565(0, 0, 0));
    matrix->setCursor(x - 1, y); matrix->print(timeStr);
    matrix->setCursor(x + 1, y); matrix->print(timeStr);
    matrix->setCursor(x, y - 1); matrix->print(timeStr);
    matrix->setCursor(x, y + 1); matrix->print(timeStr);

    // Dark Green Time (Vert foncé True Matrix)
    matrix->setTextColor(matrix->color565(0, 140, 0));
    matrix->setCursor(x, y);
    matrix->print(timeStr);
}

void CyberpunkClock::update() {
    if (!dropsInit) {
        for (int i=0; i<NUM_DROPS; i++) {
            drops[i].x = rand() % matrix->width();
            drops[i].y = (rand() % matrix->height()) - matrix->height();
            drops[i].speed = (rand() % 3) + 1;
            drops[i].length = (rand() % 10) + 5;
        }
        dropsInit = true;
    }

    if (true) {
        lastFrameTime = millis();
        // Update physics
        for (int i=0; i<NUM_DROPS; i++) {
            drops[i].y += drops[i].speed;
            if (drops[i].y - drops[i].length > matrix->height()) {
                drops[i].x = rand() % matrix->width();
                drops[i].y = (rand() % 10) * -1;
                drops[i].speed = (rand() % 3) + 1;
                drops[i].length = (rand() % 10) + 5;
            }
        }
    }

    // Draw drops
    for (int i=0; i<NUM_DROPS; i++) {
        for (int j=0; j<drops[i].length; j++) {
            int py = drops[i].y - j;
            if (py >= 0 && py < matrix->height()) {
                uint16_t color;
                if (j == 0) {
                    color = matrix->color565(255, 255, 255); // White head
                } else {
                    int green = 255 - (j * (255 / drops[i].length));
                    if (green < 0) green = 0;
                    color = matrix->color565(0, green, 0); // Fading tail
                }
                matrix->drawPixel(drops[i].x, py, color);
            }
        }
    }

    drawTime();
}
