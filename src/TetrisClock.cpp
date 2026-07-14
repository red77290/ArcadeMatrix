#include "TetrisClock.h"
#include "ConfigLoader.h"
#include <Adafruit_GFX.h>
#include <stdlib.h>

extern ConfigLoader config;

const uint16_t tetrisColors[7] = {
    0xF800, // Red
    0x07E0, // Green
    0x001F, // Blue
    0xFFE0, // Yellow
    0xFD20, // Orange
    0x07FF, // Cyan
    0xF81F  // Magenta
};

TetrisClock::TetrisClock(MatrixPanel_I2S_DMA* display) : ClockFace(display), lastFrameTime(0) {
    storedTime = {0, 0, 0};
    strcpy(lastTimeStr, "");
    blockSize = 2;
}

void TetrisClock::draw(const TimeData& t) {
    storedTime = t;
}

void TetrisClock::buildTargets(const char* timeStr) {
    GFXcanvas1 canvas(matrix->width(), matrix->height());
    canvas.fillScreen(0);
    canvas.setTextSize(1);
    canvas.setFont(NULL);
    
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
    if (bw == 0 || bh == 0) { bw = 48; bh = 7; }
    
    int maxScaleW = matrix->width() / bw;
    int maxScaleH = matrix->height() / bh;
    int sMax = min(maxScaleW, maxScaleH);
    if (sMax < 1) sMax = 1;
    
    int logicalSize = config.time.clock_size > 0 ? config.time.clock_size : 2;
    int gfxSize = 1;
    if (logicalSize == 3) gfxSize = sMax;
    else if (logicalSize == 2) gfxSize = max(1, (sMax * 2) / 3);
    else gfxSize = max(1, sMax / 3);
    
    canvas.setTextSize(gfxSize);
    canvas.getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
    
    int x = (matrix->width() - bw) / 2 + config.time.clock_offset_x - bx;
    int y = (matrix->height() - bh) / 2 - by + config.time.clock_offset_y;
    
    canvas.setCursor(x, y);
    canvas.setTextColor(1);
    canvas.print(timeStr);
    
    for (int py = 0; py < matrix->height(); py += blockSize) {
        for (int px = 0; px < matrix->width(); px += blockSize) {
            if (canvas.getPixel(px, py)) {
                TetrisBlock b;
                b.tx = px;
                b.ty = py;
                b.x = px;
                b.y = py - matrix->height() - (rand() % 40);
                b.dy = ((float)rand() / RAND_MAX) * 2.0f + 1.0f; // 1.0 to 3.0
                b.color = tetrisColors[rand() % 7];
                b.state = 0; // IN
                blocks.push_back(b);
            }
        }
    }
}

void TetrisClock::update() {
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    
    if (strcmp(timeStr, lastTimeStr) != 0) {
        // Drop out old blocks
        for (auto& b : blocks) {
            b.state = 2; // OUT
            b.dy = ((float)rand() / RAND_MAX) * 1.5f + 0.5f;
        }
        
        // Build new
        buildTargets(timeStr);
        strcpy(lastTimeStr, timeStr);
    }
    
    if (millis() - lastFrameTime > 20) { // ~50 FPS
        lastFrameTime = millis();
        
        for (auto it = blocks.begin(); it != blocks.end(); ) {
            if (it->state == 0) { // IN
                it->y += it->dy;
                if (it->y >= it->ty) {
                    it->y = it->ty;
                    it->state = 1; // FIXED
                }
                ++it;
            } else if (it->state == 2) { // OUT
                it->y += it->dy;
                it->dy += 0.2f; // Gravity
                if (it->y > matrix->height()) {
                    it = blocks.erase(it);
                } else {
                    ++it;
                }
            } else {
                ++it; // FIXED
            }
        }
    }
    
    // Draw
    for (const auto& b : blocks) {
        matrix->fillRect((int)b.x, (int)b.y, blockSize, blockSize, b.color);
    }
}
