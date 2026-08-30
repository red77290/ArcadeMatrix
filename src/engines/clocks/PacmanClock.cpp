#include "PacmanClock.h"
#include "../../core/ConfigLoader.h"
#include <math.h>

PacmanClock::PacmanClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) {
    storedTime = {0, 0, 0};
    strcpy(oldTimeStr, "");
    strcpy(newTimeStr, "");
    lastMinute = -1;
    transitioning = false;
    pacX = -30.0f;
    animFrame = 0;
    lastFrameTime = 0;
    
    ghostColors[0] = matrix->color565(255, 0, 0);       // Blinky
    ghostColors[1] = matrix->color565(255, 184, 255);   // Pinky
    ghostColors[2] = matrix->color565(0, 255, 255);     // Inky
    ghostColors[3] = matrix->color565(255, 184, 82);    // Clyde
}

void PacmanClock::draw(const TimeData& t) {
    storedTime = t;
}

void PacmanClock::drawPacman(int px, int py, int radius, int mouthAngle, bool facingRight) {
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
    // Body top (semi-circle)
    matrix->fillCircle(px, py, radius, color);
    // Body bottom
    matrix->fillRect(px - radius, py, radius * 2 + 1, radius + 1, color);
    
    uint16_t black = 0;
    // Tentacles cutout
    int tentacleOffset = (tickCount / 2) % 2;
    for (int i = 0; i < 3; i++) {
        int tx = px - radius + i * (radius * 2 / 3);
        int ty = py + radius + 1;
        if ((i + tentacleOffset) % 2 == 0) {
            matrix->fillRect(tx, ty - 2, (radius * 2 / 3) + 1, 2, black);
        }
    }
    
    // Eyes
    uint16_t white = matrix->color565(255, 255, 255);
    uint16_t blue = matrix->color565(0, 0, 255);
    
    matrix->fillRect(px - radius / 2 - 1, py - 2, 3, 4, white);
    matrix->fillRect(px + radius / 2, py - 2, 3, 4, white);
    
    // Pupils
    matrix->drawPixel(px - radius / 2 + 1, py, blue);
    matrix->drawPixel(px + radius / 2 + 2, py, blue);
}

void PacmanClock::update() {
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d", storedTime.hours, storedTime.minutes); // Pacman usually shows HH:MM to avoid constant transitions
    // Wait, the Python implementation showed seconds too if configured. Let's just use hours and minutes.
    // If the Python version used `time_str.split(':')` to get the minute...
    
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

    int pacRadius = max(3, min(w, h) / 10);
    int ghostRadius = max(2, pacRadius - 1);
    int ghostSpacing = pacRadius * 2;
    
    if (true) {
        lastFrameTime = millis();
        animFrame++;
        
        if (transitioning) {
            float pacSpeed = max(1.2f, 1.6f * w / 64.0f);
            pacX += pacSpeed;
            
            float maxPath = isTate ? (2.0f * (w + pacRadius * 4.0f)) : (w + pacRadius * 4.0f);
            if (pacX >= maxPath) {
                transitioning = false;
                lastMinute = storedTime.minutes;
                strcpy(oldTimeStr, newTimeStr);
            }
        }
    }
    
    matrix->fillScreen(0);
    
    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (gfxSize < 1) gfxSize = 1;
    matrix->setFont(NULL);

    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;
    
    uint16_t color1 = matrix->color565(255, 255, 255);
    if ((engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[0] == '#') {
        long c1 = strtol(&(engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[1], NULL, 16);
        color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
    }
    if (color1 == 0) color1 = matrix->color565(255, 255, 255);

    if (isTate) {
        // Stacked Portrait Layout (HH on top, MM on bottom)
        char hNew[4], mNew[4], hOld[4], mOld[4];
        hNew[0] = newTimeStr[0]; hNew[1] = newTimeStr[1]; hNew[2] = '\0';
        mNew[0] = newTimeStr[3]; mNew[1] = newTimeStr[4]; mNew[2] = '\0';
        hOld[0] = oldTimeStr[0]; hOld[1] = oldTimeStr[1]; hOld[2] = '\0';
        mOld[0] = oldTimeStr[3]; mOld[1] = oldTimeStr[4]; mOld[2] = '\0';

        int scale = (w >= 64) ? 3 : 2;
        if (gfxSize >= 1 && gfxSize <= 4) scale = min(scale, gfxSize);
        matrix->setTextSize(scale);

        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds("88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 11 * scale; if (bh == 0) bh = 7 * scale;

        int tx = (w - bw) / 2 + offX;
        int tyH = (h / 4) - (bh / 2) + offY;
        int tyM = (3 * h / 4) - (bh / 2) + offY;

        if (!transitioning) {
            // Outline & Text
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

            // Pulsing pellets
            uint16_t dotColor = matrix->color565(255, 183, 174);
            int dotY = (h / 2) + offY;
            for (int i = 0; i < 3; i++) {
                int px = (w / 4) + (i * (w / 4)) + offX;
                matrix->fillRect(px - 1, dotY - 1, 2, 2, dotColor);
            }
        } else {
            int mouthAngle = (int)(abs(sin(animFrame * 1.0f)) * 45);
            float leg1Len = w + pacRadius * 4.0f;

            if (pacX < leg1Len) {
                // Tier 1: Hours line transition (Left to Right)
                // Draw old hours
                matrix->setTextColor(matrix->color565(100, 100, 100));
                matrix->setCursor(tx, tyH); matrix->print(hOld);

                // Reveal new hours
                if (pacX > 0) {
                    matrix->fillRect(0, tyH - 2, (int)pacX, bh + 4, 0);
                }
                int revealX = (int)pacX - pacRadius * 3;
                if (revealX > 0) {
                    matrix->setTextColor(color1);
                    matrix->setCursor(tx, tyH); matrix->print(hNew);
                    if (revealX < w) {
                        matrix->fillRect(revealX, tyH - 2, w - revealX, bh + 4, 0);
                    }
                }

                // Tier 2 still shows old minutes
                matrix->setTextColor(matrix->color565(100, 100, 100));
                matrix->setCursor(tx, tyM); matrix->print(mOld);

                // Draw Pacman & Ghosts on Tier 1
                drawPacman((int)pacX, tyH + bh / 2, pacRadius, mouthAngle, true);
                for (int i = 0; i < 4; i++) {
                    float gx = pacX - (pacRadius * 3.0f) - (i * ghostSpacing);
                    float gy = tyH + bh / 2 + sin(animFrame * 0.4f + i) * (pacRadius / 3.0f);
                    drawGhost((int)gx, (int)gy, ghostRadius, ghostColors[i], animFrame);
                }
            } else {
                // Tier 2: Minutes line transition (Right to Left)
                // Hours are fully revealed
                matrix->setTextColor(color1);
                matrix->setCursor(tx, tyH); matrix->print(hNew);

                float leg2X = pacX - leg1Len;
                float currentPacX = w - leg2X;

                matrix->setTextColor(matrix->color565(100, 100, 100));
                matrix->setCursor(tx, tyM); matrix->print(mOld);

                if (currentPacX < w) {
                    matrix->fillRect((int)currentPacX, tyM - 2, w - (int)currentPacX, bh + 4, 0);
                }
                int revealX = (int)currentPacX + pacRadius * 3;
                if (revealX < w) {
                    matrix->setTextColor(color1);
                    matrix->setCursor(tx, tyM); matrix->print(mNew);
                    if (revealX > 0) {
                        matrix->fillRect(0, tyM - 2, revealX, bh + 4, 0);
                    }
                }

                // Draw Pacman facing left & Ghosts
                drawPacman((int)currentPacX, tyM + bh / 2, pacRadius, mouthAngle, false);
                for (int i = 0; i < 4; i++) {
                    float gx = currentPacX + (pacRadius * 3.0f) + (i * ghostSpacing);
                    float gy = tyM + bh / 2 + sin(animFrame * 0.4f + i) * (pacRadius / 3.0f);
                    drawGhost((int)gx, (int)gy, ghostRadius, ghostColors[i], animFrame);
                }
            }
        }
    } else {
        // Landscape / Widescreen Layout
        matrix->setTextSize(gfxSize);
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(newTimeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 30; if (bh == 0) bh = 7 * gfxSize;
        
        int tx = (w - bw) / 2 + offX;
        int ty = (h - bh) / 2 + offY;
        
        if (!transitioning) {
            // Black outline for crisp visibility
            matrix->setTextColor(0);
            matrix->setCursor(tx - 1, ty); matrix->print(newTimeStr);
            matrix->setCursor(tx + 1, ty); matrix->print(newTimeStr);
            matrix->setCursor(tx, ty - 1); matrix->print(newTimeStr);
            matrix->setCursor(tx, ty + 1); matrix->print(newTimeStr);

            matrix->setTextColor(color1);
            matrix->setCursor(tx, ty);
            matrix->print(newTimeStr);
            
            // Random pulsing dots
            uint16_t dotColor = matrix->color565(255, 183, 174);
            for (int i = 0; i < 5; i++) {
                float px = (sin(animFrame * 0.1f + i) * w / 2) + w / 2;
                float py = (cos(animFrame * 0.15f + i * 2) * h / 2) + h / 2;
                matrix->drawPixel((int)px, (int)py, dotColor);
            }
        } else {
            int mouthAngle = (int)(abs(sin(animFrame * 1.0f)) * 45);
            
            // Draw old time
            matrix->setCursor(tx, ty);
            matrix->setTextColor(matrix->color565(100, 100, 100));
            matrix->print(oldTimeStr);
            
            // Cover eaten part (left of pacman)
            if (pacX > 0) {
                matrix->fillRect(0, 0, (int)pacX, h, 0);
            }
            
            // Draw new time
            matrix->setCursor(tx, ty);
            matrix->setTextColor(color1);
            matrix->print(newTimeStr);
            
            // Cover uneaten part (right of reveal wave)
            int revealX = (int)pacX - pacRadius * 4;
            if (revealX > 0 && revealX < w) {
                matrix->fillRect(revealX, 0, w - revealX, h, 0);
            }
            
            // Draw Pacman
            drawPacman((int)pacX, h / 2, pacRadius, mouthAngle, true);
            
            // Draw Ghosts
            for (int i = 0; i < 4; i++) {
                float gx = pacX - (pacRadius * 3.0f) - (i * ghostSpacing);
                float gy = h / 2 + sin(animFrame * 0.4f + i) * (pacRadius / 3.0f);
                drawGhost((int)gx, (int)gy, ghostRadius, ghostColors[i], animFrame);
            }
        }
    }
}
