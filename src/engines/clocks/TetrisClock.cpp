#include "TetrisClock.h"
#include "../../core/ConfigLoader.h"
#include <Adafruit_GFX.h>
#include <stdlib.h>

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

TetrisClock::TetrisClock(MatrixPanel_I2S_DMA* display, bool gameboyMode, const EngineConfig* config) : ClockFace(display, config), isGameboy(gameboyMode), lastFrameTime(0) {
    storedTime = {0, 0, 0};
    strcpy(lastTimeStr, "");
    blockSize = max(1, (int)(matrix->height() / 16));
}

void TetrisClock::draw(const TimeData& t) {
    storedTime = t;
}

void TetrisClock::buildTargets(const char* timeStr, const std::vector<int>& targetIndices) {
    int w = matrix->width();
    int h = matrix->height();
    bool isTate = (w < 48 || h > (w * 3) / 2);

    int logicalSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (logicalSize < 1) logicalSize = 1;

    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;

    if (isTate) {
        // Stacked Portrait Layout: 3 Tiers (HH, MM, SS)
        // Each tier has 2 characters (e.g. "16", "02", "52")
        char tierStrs[3][4];
        tierStrs[0][0] = timeStr[0]; tierStrs[0][1] = timeStr[1]; tierStrs[0][2] = '\0';
        tierStrs[1][0] = timeStr[3]; tierStrs[1][1] = timeStr[4]; tierStrs[1][2] = '\0';
        tierStrs[2][0] = timeStr[6]; tierStrs[2][1] = timeStr[7]; tierStrs[2][2] = '\0';

        int16_t bx, by;
        uint16_t bw, bh;
        GFXcanvas1 tempCanvas(32, 16);
        tempCanvas.setTextSize(1);
        tempCanvas.setFont(NULL);
        tempCanvas.getTextBounds("88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0 || bh == 0) { bw = 11; bh = 7; }

        int sMaxW = w / bw;
        int sMaxH = (h / 3) / bh;
        int sMax = min(sMaxW, sMaxH);
        if (sMax < 1) sMax = 1;
        blockSize = min(logicalSize, sMax);
        if (blockSize < 1) blockSize = 1;

        int scaledW = bw * blockSize;
        int scaledH = bh * blockSize;
        int tierStartX = (w - scaledW) / 2 + offX - (bx * blockSize);

        int tierY[3] = {
            (h / 6) - (scaledH / 2) + offY - (by * blockSize),
            (h / 2) - (scaledH / 2) + offY - (by * blockSize),
            (5 * h / 6) - (scaledH / 2) + offY - (by * blockSize)
        };

        for (int charIdx : targetIndices) {
            if (charIdx == 2 || charIdx == 5) continue; // Skip colons in stacked mode
            int tier = (charIdx < 2) ? 0 : (charIdx < 5 ? 1 : 2);
            int charInTier = (charIdx < 2) ? charIdx : (charIdx < 5 ? charIdx - 3 : charIdx - 6);

            const char* currentTierStr = tierStrs[tier];
            GFXcanvas1 fullCanvas(bw + 4, bh + 4);
            if (!fullCanvas.getBuffer()) return;
            fullCanvas.fillScreen(0);
            fullCanvas.setTextSize(1);
            fullCanvas.setCursor(2 - bx, 2 - by);
            fullCanvas.setTextColor(1);
            fullCanvas.print(currentTierStr);

            GFXcanvas1 maskCanvas(bw + 4, bh + 4);
            if (!maskCanvas.getBuffer()) return;
            char maskStr[4];
            strcpy(maskStr, currentTierStr);
            maskStr[charInTier] = ' ';
            maskCanvas.fillScreen(0);
            maskCanvas.setTextSize(1);
            maskCanvas.setCursor(2 - bx, 2 - by);
            maskCanvas.setTextColor(1);
            maskCanvas.print(maskStr);

            int curTierY = tierY[tier];
            for (int py = 0; py < bh + 4; py++) {
                for (int px = 0; px < bw + 4; px++) {
                    if (fullCanvas.getPixel(px, py) && !maskCanvas.getPixel(px, py)) {
                        TetrisBlock b;
                        b.charIndex = charIdx;
                        b.tx = tierStartX + (px - 2) * blockSize;
                        b.ty = curTierY + (py - 2) * blockSize;
                        b.x = b.tx;
                        b.y = b.ty - (h / 3) - (rand() % (int)(h / 4 + 1));
                        float base_dy = max(0.35f, (float)h / 120.0f);
                        b.dy = base_dy + (((float)rand() / RAND_MAX) * base_dy * 0.5f);
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
    } else {
        // Landscape / Widescreen Layout (Single Row "HH:MM:SS")
        int16_t bx, by;
        uint16_t bw, bh;
        GFXcanvas1 tempCanvas(128, 32);
        tempCanvas.setTextSize(1);
        tempCanvas.setFont(NULL);
        tempCanvas.getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0 || bh == 0) { bw = 48; bh = 7; }

        int sMax = min((int)(w / bw), (int)(h / bh));
        if (sMax < 1) sMax = 1;
        blockSize = min(logicalSize, sMax);
        if (blockSize < 1) blockSize = 1;

        int scaledW = bw * blockSize;
        int scaledH = bh * blockSize;

        int startX = (w - scaledW) / 2 + offX - (bx * blockSize);
        int startY = (h - scaledH) / 2 + offY - (by * blockSize);

        GFXcanvas1 fullCanvas(bw + 4, bh + 4);
        if (!fullCanvas.getBuffer()) return;

        fullCanvas.fillScreen(0);
        fullCanvas.setTextSize(1);
        fullCanvas.setCursor(2 - bx, 2 - by);
        fullCanvas.setTextColor(1);
        fullCanvas.print(timeStr);

        GFXcanvas1 maskCanvas(bw + 4, bh + 4);
        if (!maskCanvas.getBuffer()) return;

        for (int charIdx : targetIndices) {
            char maskStr[12];
            strcpy(maskStr, timeStr);
            maskStr[charIdx] = ' ';

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
                        b.y = b.ty - h - (rand() % (int)(h / 2 + 1));
                        float base_dy = max(0.30f, (float)h / 140.0f);
                        b.dy = base_dy + (((float)rand() / RAND_MAX) * base_dy * 0.5f);
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
}
void TetrisClock::update() {
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    
    if (strcmp(timeStr, lastTimeStr) != 0) {
        if (strlen(timeStr) != strlen(lastTimeStr) || blocks.empty()) {
            for (auto& b : blocks) {
                b.state = 2; // OUT
                float base_dy = max(0.40f, matrix->height() / 70.0f);
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
                                float base_dy = max(0.40f, matrix->height() / 70.0f);
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
                it->dy += 0.08f * timeScale; // Smooth natural gravity
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

void TetrisClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    if (strlen(lastTimeStr) > 0 && !blocks.empty()) {
        std::vector<int> allIndices = {0, 1, 3, 4, 6, 7};
        buildTargets(lastTimeStr, allIndices);
    }
}
