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

const uint16_t gameboyColors[4] = {
    0x0AD1, // Darkest green
    0x3306,
    0x8D61,
    0x9DE1  // Lightest green
};

TetrisClock::TetrisClock(MatrixPanel_I2S_DMA* display, bool gameboyMode) : ClockFace(display), isGameboy(gameboyMode), lastFrameTime(0) {
    storedTime = {0, 0, 0};
    strcpy(lastTimeStr, "");
    blockSize = max(1, (int)(matrix->height() / 16));
}

void TetrisClock::draw(const TimeData& t) {
    storedTime = t;
}

void TetrisClock::buildTargets(const char* timeStr, const std::vector<int>& targetIndices) {
    int logicalSize = config.time.clock_size > 0 ? config.time.clock_size : 2;
    int gfxSize = 1;
    
    // Setup temporary canvas to get bounds
    GFXcanvas1 tempCanvas(matrix->width(), matrix->height());
    tempCanvas.setTextSize(1);
    tempCanvas.setFont(NULL);
    
    int16_t bx, by;
    uint16_t bw, bh;
    tempCanvas.getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
    if (bw == 0 || bh == 0) { bw = 48; bh = 7; }
    
    int sMax = min((int)(matrix->width() / bw), (int)(matrix->height() / bh));
    if (sMax < 1) sMax = 1;
    
    if (logicalSize == 3) gfxSize = sMax;
    else if (logicalSize == 2) gfxSize = max(1, (sMax * 2) / 3);
    else gfxSize = max(1, sMax / 3);
    
    tempCanvas.setTextSize(gfxSize);
    tempCanvas.getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
    
    int x = (matrix->width() - bw) / 2 + config.time.clock_offset_x - bx;
    int y = (matrix->height() - bh) / 2 - by + config.time.clock_offset_y;
    
    GFXcanvas1 fullCanvas(matrix->width(), matrix->height());
    fullCanvas.fillScreen(0);
    fullCanvas.setTextSize(gfxSize);
    fullCanvas.setCursor(x, y);
    fullCanvas.setTextColor(1);
    fullCanvas.print(timeStr);
    
    for (int charIdx : targetIndices) {
        char maskStr[12];
        strcpy(maskStr, timeStr);
        maskStr[charIdx] = ' '; // Hide this character
        
        GFXcanvas1 maskCanvas(matrix->width(), matrix->height());
        maskCanvas.fillScreen(0);
        maskCanvas.setTextSize(gfxSize);
        maskCanvas.setCursor(x, y);
        maskCanvas.setTextColor(1);
        maskCanvas.print(maskStr);
        
        for (int py = 0; py < matrix->height(); py += blockSize) {
            for (int px = 0; px < matrix->width(); px += blockSize) {
                if (fullCanvas.getPixel(px, py) && !maskCanvas.getPixel(px, py)) {
                    TetrisBlock b;
                    b.charIndex = charIdx;
                    b.tx = px;
                    b.ty = py;
                    b.x = px;
                    b.y = py - matrix->height() - (rand() % 40);
                    b.dy = (((float)rand() / RAND_MAX) * 2.0f + 1.0f) * (matrix->height() / 32.0f); // 1.0 to 3.0 scaled
                    if (isGameboy) {
                        b.color = gameboyColors[rand() % 4];
                    } else {
                        b.color = tetrisColors[rand() % 7];
                    }
                    b.state = 0; // IN
                    blocks.push_back(b);
                }
            }
        }
    }
}

void TetrisClock::update() {
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    
    if (strcmp(timeStr, lastTimeStr) != 0) {
        if (strlen(timeStr) != strlen(lastTimeStr) || blocks.empty()) {
            for (auto& b : blocks) {
                b.state = 2; // OUT
                b.dy = (((float)rand() / RAND_MAX) * 1.5f + 0.5f) * (matrix->height() / 32.0f);
            }
            std::vector<int> allIndices;
            for(int i=0; i<strlen(timeStr); i++) allIndices.push_back(i);
            buildTargets(timeStr, allIndices);
        } else {
            std::vector<int> changedIndices;
            for(int i=0; i<strlen(timeStr); i++) {
                if(timeStr[i] != lastTimeStr[i]) {
                    changedIndices.push_back(i);
                }
            }
            if (!changedIndices.empty()) {
                for (auto& b : blocks) {
                    if (b.state != 2) {
                        for(int idx : changedIndices) {
                            if(b.charIndex == idx) {
                                b.state = 2; // OUT
                                b.dy = (((float)rand() / RAND_MAX) * 1.5f + 0.5f) * (matrix->height() / 32.0f);
                                break;
                            }
                        }
                    }
                }
                buildTargets(timeStr, changedIndices);
            }
        }
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
                it->dy += 0.2f * (matrix->height() / 32.0f); // Gravity
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
