#include "TetrisClock.h"
#include "../../core/ConfigLoader.h"
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
    
    int16_t bx, by;
    uint16_t bw, bh;
    
    // We always draw text at scale 1 to a small canvas, then scale it up using blockSize!
    // But wait, what if the user wants it to be smaller than the max size?
    // Let's determine blockSize based on matrix size.
    
    // First, find the unscaled text bounds.
    GFXcanvas1 tempCanvas(128, 32);
    tempCanvas.setTextSize(1);
    tempCanvas.setFont(NULL);
    tempCanvas.getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
    if (bw == 0 || bh == 0) { bw = 48; bh = 7; }
    
    int sMax = min((int)(matrix->width() / bw), (int)(matrix->height() / bh));
    if (sMax < 1) sMax = 1;
    
    if (logicalSize >= 5) gfxSize = sMax + 1;
    else if (logicalSize == 4) gfxSize = sMax;
    else if (logicalSize == 3) gfxSize = max(1, (sMax * 3) / 4);
    else if (logicalSize == 2) gfxSize = max(1, (sMax * 2) / 4);
    else gfxSize = max(1, sMax / 4);
    
    blockSize = gfxSize; // The grid block size is equal to the text scale!
    
    int scaledW = bw * blockSize;
    int scaledH = bh * blockSize;
    
    int startX = (matrix->width() - scaledW) / 2 + config.time.clock_offset_x - (bx * blockSize);
    int startY = (matrix->height() - scaledH) / 2 + config.time.clock_offset_y - (by * blockSize);
    
    // Draw full text at scale 1
    GFXcanvas1 fullCanvas(bw + 4, bh + 4);
    if (!fullCanvas.getBuffer()) return; // Prevent crash
    
    fullCanvas.fillScreen(0);
    fullCanvas.setTextSize(1);
    fullCanvas.setCursor(2 - bx, 2 - by); // Margin of 2
    fullCanvas.setTextColor(1);
    fullCanvas.print(timeStr);
    
    GFXcanvas1 maskCanvas(bw + 4, bh + 4);
    if (!maskCanvas.getBuffer()) return;
    
    for (int charIdx : targetIndices) {
        char maskStr[12];
        strcpy(maskStr, timeStr);
        maskStr[charIdx] = ' '; // Hide this character
        
        maskCanvas.fillScreen(0);
        maskCanvas.setTextSize(1);
        maskCanvas.setCursor(2 - bx, 2 - by);
        maskCanvas.setTextColor(1);
        maskCanvas.print(maskStr);
        
        for (int py = 0; py < bh + 4; py++) {
            for (int px = 0; px < bw + 4; px++) {
                if (fullCanvas.getPixel(px, py) && !maskCanvas.getPixel(px, py)) {
                    TetrisBlock b;
                    b.charIndex = charIdx;
                    
                    b.tx = startX + (px - 2) * blockSize;
                    b.ty = startY + (py - 2) * blockSize;
                    b.x = b.tx;
                    b.y = b.ty - matrix->height() - (rand() % (int)(matrix->height() / 2));
                    // Distance to fall is roughly height + some random offset (64-96 pixels)
                    // At 60fps (16ms per frame), we want it to take ~60 frames (1 second).
                    // So dy should be around 1.5 pixels per frame.
                    float base_dy = max(1.0f, matrix->height() / 40.0f);
                    b.dy = base_dy + (((float)rand() / RAND_MAX) * base_dy);
                    if (isGameboy) {
                        b.color = gameboyColors[charIdx % 4];
                    } else {
                        b.color = tetrisColors[charIdx % 7];
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
                float base_dy = max(1.0f, matrix->height() / 40.0f);
                b.dy = base_dy * 0.5f + (((float)rand() / RAND_MAX) * base_dy * 0.5f);
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
                                float base_dy = max(1.5f, matrix->height() / 15.0f);
                                b.dy = base_dy * 0.5f + (((float)rand() / RAND_MAX) * base_dy * 0.5f);
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
    
    // Frame-rate independent physics
    unsigned long currentMillis = millis();
    float dt = (currentMillis - lastFrameTime);
    if (dt > 100) dt = 100.0f; // Cap at 100ms to avoid huge jumps, but don't reset to 16ms!
    lastFrameTime = currentMillis;
    float timeScale = dt / 16.0f; // Scale relative to 60fps
        
    for (auto it = blocks.begin(); it != blocks.end(); ) {
            if (it->state == 0) { // IN
                it->y += it->dy * timeScale;
                if (it->y >= it->ty) {
                    it->y = it->ty;
                    it->state = 1; // FIXED
                }
                ++it;
            } else if (it->state == 2) { // OUT
                it->y += it->dy * timeScale;
                it->dy += 0.4f * timeScale; // Gravity matches Rust
                if (it->y > matrix->height()) {
                    it = blocks.erase(it);
                } else {
                    ++it;
                }
            } else {
                ++it; // FIXED
            }
        }

    
    // Draw
    for (const auto& b : blocks) {
        matrix->fillRect((int)b.x, (int)b.y, blockSize, blockSize, b.color);
    }
}
