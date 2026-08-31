#include "PacmanClock.h"
#include "../../core/ConfigLoader.h"
#include <math.h>

PacmanClock::PacmanClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) {
    storedTime = {0, 0, 0};
    strcpy(oldTimeStr, "");
    strcpy(newTimeStr, "");
    lastMinute = -1;
    transitioning = false;
    pacX = -40.0f;
    animFrame = 0;
    lastFrameTime = 0;
    
    if (matrix) {
        ghostColors[0] = matrix->color565(255, 0, 0);       // Blinky
        ghostColors[1] = matrix->color565(255, 184, 255);   // Pinky
        ghostColors[2] = matrix->color565(0, 255, 255);     // Inky
        ghostColors[3] = matrix->color565(255, 184, 82);    // Clyde
    }
}

void PacmanClock::draw(const TimeData& t) {
    storedTime = t;
}

void PacmanClock::drawPacman(int px, int py, int radius, int mouthAngle, bool facingRight) {
    if (!matrix || radius < 2) return;
    int w = matrix->width();
    if (px + radius < 0 || px - radius >= w) return;
    
    uint16_t yellow = matrix->color565(255, 255, 0);
    uint16_t black = 0;
    
    matrix->fillCircle(px, py, radius, yellow);
    
    if (mouthAngle > 0) {
        float angleRad = mouthAngle * (M_PI / 180.0f);
        int dx = (int)(radius * 1.5f * cos(angleRad));
        int dy = (int)(radius * 1.5f * sin(angleRad));
        
        if (facingRight) {
            matrix->fillTriangle(px, py, px + dx, py - dy, px + dx, py + dy, black);
        } else {
            matrix->fillTriangle(px, py, px - dx, py - dy, px - dx, py + dy, black);
        }
    }
}

void PacmanClock::drawGhost(int px, int py, int radius, uint16_t color, int tickCount) {
    if (!matrix || radius < 2) return;
    int w = matrix->width();
    if (px + radius < 0 || px - radius >= w) return;
    
    // Body top (semi-circle)
    matrix->fillCircle(px, py, radius, color);
    // Body bottom
    matrix->fillRect(px - radius, py, radius * 2 + 1, radius + 1, color);
    
    uint16_t black = 0;
    // Tentacles cutout
    int tentacleOffset = (tickCount / 2) % 2;
    int tentacleW = max(1, (radius * 2) / 3);
    int tentacleH = max(2, radius / 3);
    for (int i = 0; i < 3; i++) {
        int tx = px - radius + i * tentacleW;
        int ty = py + radius + 1;
        if ((i + tentacleOffset) % 2 == 0) {
            matrix->fillRect(tx, ty - tentacleH, tentacleW + 1, tentacleH, black);
        }
    }
    
    // Eyes
    uint16_t white = matrix->color565(255, 255, 255);
    uint16_t blue = matrix->color565(0, 0, 255);
    
    int eyeW = max(2, radius * 2 / 5);
    int eyeH = max(3, radius / 2);
    int eyeOffsetY = max(1, radius / 4);
    int eyeOffsetX = max(2, radius / 2);
    
    // Left eye & Right eye
    matrix->fillRect(px - eyeOffsetX - eyeW / 2, py - eyeOffsetY, eyeW, eyeH, white);
    matrix->fillRect(px + eyeOffsetX - eyeW / 2, py - eyeOffsetY, eyeW, eyeH, white);
    
    // Pupils
    int pupilSize = (radius >= 10) ? 2 : 1;
    matrix->fillRect(px - eyeOffsetX - eyeW / 2 + 1, py - eyeOffsetY + 1, pupilSize, pupilSize, blue);
    matrix->fillRect(px + eyeOffsetX - eyeW / 2 + 1, py - eyeOffsetY + 1, pupilSize, pupilSize, blue);
}

void PacmanClock::update() {
    if (!matrix) return;
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d", storedTime.hours, storedTime.minutes);
    
    if (lastMinute == -1) {
        lastMinute = storedTime.minutes;
        strcpy(oldTimeStr, timeStr);
        strcpy(newTimeStr, timeStr);
    } else if ((lastMinute != storedTime.minutes || strcmp(oldTimeStr, timeStr) != 0) && !transitioning) {
        transitioning = true;
        strcpy(newTimeStr, timeStr);
        pacX = -40.0f;
    }

    int w = matrix->width();
    int h = matrix->height();
    bool isTate = (w < 48 || h > (w * 3) / 2);

    matrix->setFont(NULL);
    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (gfxSize < 1) gfxSize = 1;

    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;
    
    uint16_t color1 = matrix->color565(255, 255, 255);
    const char* colStr = engineConfig ? engineConfig->getString("clock_color_1", "").c_str() : "";
    if (colStr[0] == '#') {
        long c1 = strtol(&colStr[1], NULL, 16);
        color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
    }
    if (color1 == 0) color1 = matrix->color565(255, 255, 255);

    int scale = 1;
    uint16_t bw = 0, bh = 0;
    int16_t bx = 0, by = 0;

    if (isTate) {
        scale = (w >= 64) ? 3 : 2;
        if (gfxSize >= 1 && gfxSize <= 4) scale = min(scale, gfxSize);
        matrix->setTextSize(scale);
        matrix->getTextBounds("88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 11 * scale;
        if (bh == 0) bh = 7 * scale;
    } else {
        scale = gfxSize;
        matrix->setTextSize(scale);
        matrix->getTextBounds(newTimeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 30 * scale;
        if (bh == 0) bh = 7 * scale;
    }

    int maxPacRadius = isTate ? min((w / 2) - 2, (h / 4) - 2) : ((h / 2) - 1);
    int targetPacRadius = (int)(bh * 0.70f) + 1;
    int pacRadius = max(4, min(targetPacRadius, maxPacRadius));
    int ghostRadius = max(3, (int)(pacRadius * 0.85f));
    int ghostSpacing = (int)(pacRadius * 2.2f);
    
    uint32_t now = millis();
    float dt = (lastFrameTime > 0) ? constrain((now - lastFrameTime) / 1000.0f, 0.005f, 0.050f) : 0.016f;
    lastFrameTime = now;
    animFrame++;
    
    if (transitioning) {
        float legLen = w + pacRadius * 4.0f + 4.0f * ghostSpacing;
        float maxPath = isTate ? (3.0f * legLen) : legLen;
        float transitionDuration = isTate ? 4.5f : 2.5f; // Duration in seconds
        float pacSpeed = (maxPath / transitionDuration) * dt;
        pacX += pacSpeed;
        
        if (pacX >= maxPath) {
            transitioning = false;
            lastMinute = storedTime.minutes;
            strcpy(oldTimeStr, newTimeStr);
        }
    }
    
    matrix->fillScreen(0);

    if (isTate) {
        // Stacked Portrait Layout (HH on top, MM on bottom)
        char hNew[4], mNew[4], hOld[4], mOld[4];
        hNew[0] = newTimeStr[0]; hNew[1] = newTimeStr[1]; hNew[2] = '\0';
        mNew[0] = newTimeStr[3]; mNew[1] = newTimeStr[4]; mNew[2] = '\0';
        hOld[0] = oldTimeStr[0]; hOld[1] = oldTimeStr[1]; hOld[2] = '\0';
        mOld[0] = oldTimeStr[3]; mOld[1] = oldTimeStr[4]; mOld[2] = '\0';

        int tx = (w - bw) / 2 + offX;
        int tyH = (h / 4) - (bh / 2) + offY;
        int tyM = (3 * h / 4) - (bh / 2) + offY;
        int dotY = (h / 2) + offY;
        uint16_t dotColor = matrix->color565(255, 183, 174);
        int dotX[3] = {
            (w / 4) + offX,
            (w / 2) + offX,
            (3 * w / 4) + offX
        };

        if (!transitioning) {
            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, tyH); matrix->print(hNew);
            matrix->setCursor(tx + 1, tyH); matrix->print(hNew);
            matrix->setCursor(tx, tyH - 1); matrix->print(hNew);
            matrix->setCursor(tx, tyH + 1); matrix->print(hNew);
            matrix->setCursor(tx, tyH); matrix->setTextColor(color1); matrix->print(hNew);

            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, tyM); matrix->print(mNew);
            matrix->setCursor(tx + 1, tyM); matrix->print(mNew);
            matrix->setCursor(tx, tyM - 1); matrix->print(mNew);
            matrix->setCursor(tx, tyM + 1); matrix->print(mNew);
            matrix->setCursor(tx, tyM); matrix->setTextColor(color1); matrix->print(mNew);

            for (int i = 0; i < 3; i++) {
                matrix->fillRect(dotX[i] - 1, dotY - 1, 2, 2, dotColor);
            }
        } else {
            int mouthAngle = (int)(abs(sin(animFrame * 0.25f)) * 45);
            float legLen = w + pacRadius * 4.0f + 4 * ghostSpacing;

            if (pacX < legLen) {
                // Tier 1: Hours line (Left -> Right)
                float currentPacX = -pacRadius * 2.0f + pacX;

                matrix->setTextColor(matrix->color565(100, 100, 100));
                matrix->setCursor(tx, tyH); matrix->print(hOld);

                if (currentPacX > 0) {
                    matrix->fillRect(0, tyH - 2, min(w, (int)currentPacX), bh + 4, 0);
                }
                int revealX = (int)currentPacX - (pacRadius * 3 + 4 * ghostSpacing);
                if (revealX > 0) {
                    matrix->setTextColor(color1);
                    matrix->setCursor(tx, tyH); matrix->print(hNew);
                    if (revealX < w) {
                        matrix->fillRect(revealX, tyH - 2, w - revealX, bh + 4, 0);
                    }
                }

                for (int i = 0; i < 3; i++) {
                    matrix->fillRect(dotX[i] - 1, dotY - 1, 2, 2, dotColor);
                }

                matrix->setTextColor(matrix->color565(100, 100, 100));
                matrix->setCursor(tx, tyM); matrix->print(mOld);

                drawPacman((int)currentPacX, tyH + bh / 2, pacRadius, mouthAngle, true);
                for (int i = 0; i < 4; i++) {
                    float gx = currentPacX - (pacRadius * 2.5f) - (i * ghostSpacing);
                    float gy = tyH + bh / 2 + sin(animFrame * 0.4f + i) * (pacRadius / 3.0f);
                    drawGhost((int)gx, (int)gy, ghostRadius, ghostColors[i], animFrame);
                }
            } else if (pacX < 2.0f * legLen) {
                // Tier 2: Middle dots (Right -> Left)
                float progress = pacX - legLen;
                float currentPacX = (w + pacRadius * 2.0f) - progress;

                matrix->setTextColor(color1);
                matrix->setCursor(tx, tyH); matrix->print(hNew);

                for (int i = 0; i < 3; i++) {
                    int px = dotX[i];
                    if (px < (currentPacX - pacRadius)) {
                        matrix->fillRect(px - 1, dotY - 1, 2, 2, dotColor);
                    } else if (px > (currentPacX + pacRadius * 3 + 4 * ghostSpacing)) {
                        matrix->fillRect(px - 1, dotY - 1, 2, 2, dotColor);
                    }
                }

                matrix->setTextColor(matrix->color565(100, 100, 100));
                matrix->setCursor(tx, tyM); matrix->print(mOld);

                drawPacman((int)currentPacX, dotY, pacRadius, mouthAngle, false);
                for (int i = 0; i < 4; i++) {
                    float gx = currentPacX + (pacRadius * 2.5f) + (i * ghostSpacing);
                    float gy = dotY + sin(animFrame * 0.4f + i) * (pacRadius / 3.0f);
                    drawGhost((int)gx, (int)gy, ghostRadius, ghostColors[i], animFrame);
                }
            } else {
                // Tier 3: Minutes line (Left -> Right)
                float progress = pacX - 2.0f * legLen;
                float currentPacX = -pacRadius * 2.0f + progress;

                matrix->setTextColor(color1);
                matrix->setCursor(tx, tyH); matrix->print(hNew);

                for (int i = 0; i < 3; i++) {
                    matrix->fillRect(dotX[i] - 1, dotY - 1, 2, 2, dotColor);
                }

                matrix->setTextColor(matrix->color565(100, 100, 100));
                matrix->setCursor(tx, tyM); matrix->print(mOld);

                if (currentPacX > 0) {
                    matrix->fillRect(0, tyM - 2, min(w, (int)currentPacX), bh + 4, 0);
                }
                int revealX = (int)currentPacX - (pacRadius * 3 + 4 * ghostSpacing);
                if (revealX > 0) {
                    matrix->setTextColor(color1);
                    matrix->setCursor(tx, tyM); matrix->print(mNew);
                    if (revealX < w) {
                        matrix->fillRect(revealX, tyM - 2, w - revealX, bh + 4, 0);
                    }
                }

                drawPacman((int)currentPacX, tyM + bh / 2, pacRadius, mouthAngle, true);
                for (int i = 0; i < 4; i++) {
                    float gx = currentPacX - (pacRadius * 2.5f) - (i * ghostSpacing);
                    float gy = tyM + bh / 2 + sin(animFrame * 0.4f + i) * (pacRadius / 3.0f);
                    drawGhost((int)gx, (int)gy, ghostRadius, ghostColors[i], animFrame);
                }
            }
        }
    } else {
        // Landscape / Widescreen Layout
        int tx = (w - bw) / 2 + offX;
        int ty = (h - bh) / 2 + offY;
        
        if (!transitioning) {
            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, ty); matrix->print(newTimeStr);
            matrix->setCursor(tx + 1, ty); matrix->print(newTimeStr);
            matrix->setCursor(tx, ty - 1); matrix->print(newTimeStr);
            matrix->setCursor(tx, ty + 1); matrix->print(newTimeStr);

            matrix->setTextColor(color1);
            matrix->setCursor(tx, ty);
            matrix->print(newTimeStr);
            
            uint16_t dotColor = matrix->color565(255, 183, 174);
            for (int i = 0; i < 5; i++) {
                float px = (sin(animFrame * 0.1f + i) * w / 2) + w / 2;
                float py = (cos(animFrame * 0.15f + i * 2) * h / 2) + h / 2;
                matrix->drawPixel((int)px, (int)py, dotColor);
            }
        } else {
            int mouthAngle = (int)(abs(sin(animFrame * 0.25f)) * 45);
            
            // Draw old time being eaten
            matrix->setCursor(tx, ty);
            matrix->setTextColor(matrix->color565(100, 100, 100));
            matrix->print(oldTimeStr);
            
            // Cover eaten part (left of pacman)
            if (pacX > 0) {
                matrix->fillRect(0, 0, min(w, (int)pacX), h, 0);
            }
            
            // Draw new time revealed behind pacman
            int revealX = (int)(pacX - (pacRadius * 2.5f + 4 * ghostSpacing));
            if (revealX > 0) {
                matrix->setCursor(tx, ty);
                matrix->setTextColor(color1);
                matrix->print(newTimeStr);
                
                // Cover uneaten part (right of reveal wave)
                if (revealX < w) {
                    matrix->fillRect(revealX, 0, w - revealX, h, 0);
                }
            }
            
            // Draw Pacman
            drawPacman((int)pacX, h / 2, pacRadius, mouthAngle, true);
            
            // Draw Ghosts
            for (int i = 0; i < 4; i++) {
                float gx = pacX - (pacRadius * 2.5f) - (i * ghostSpacing);
                float gy = (h / 2) + sin(animFrame * 0.4f + i) * (pacRadius / 3.0f);
                drawGhost((int)gx, (int)gy, ghostRadius, ghostColors[i], animFrame);
            }
        }
    }
}
