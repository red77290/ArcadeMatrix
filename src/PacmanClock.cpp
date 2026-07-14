#include "PacmanClock.h"
#include "ConfigLoader.h"
#include <math.h>

extern ConfigLoader config;

PacmanClock::PacmanClock(MatrixPanel_I2S_DMA* display) : ClockFace(display) {
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
    } else if (lastMinute != storedTime.minutes && !transitioning) {
        transitioning = true;
        strcpy(newTimeStr, timeStr);
        pacX = -40.0f;
    }
    
    if (millis() - lastFrameTime > 50) {
        lastFrameTime = millis();
        animFrame++;
        
        matrix->fillScreen(0);
        
        int gfxSize = config.time.clock_size > 0 ? config.time.clock_size : 2;
        matrix->setTextSize(gfxSize);
        // Default GFX font
        matrix->setFont(NULL);
        
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(newTimeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 30; if (bh == 0) bh = 7 * gfxSize;
        
        int tx = (matrix->width() - bw) / 2 + config.time.clock_offset_x;
        int ty = (matrix->height() - bh) / 2 + config.time.clock_offset_y;
        
        uint16_t color1 = matrix->color565(255, 255, 255);
        if (config.time.clock_color_1[0] == '#') {
            long c1 = strtol(&config.time.clock_color_1[1], NULL, 16);
            color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
        }
        
        if (!transitioning) {
            matrix->setCursor(tx, ty);
            matrix->setTextColor(color1);
            matrix->print(newTimeStr);
            
            // Random pulsing dots
            uint16_t dotColor = matrix->color565(255, 183, 174);
            for (int i = 0; i < 5; i++) {
                float px = (sin(animFrame * 0.1f + i) * matrix->width() / 2) + matrix->width() / 2;
                float py = (cos(animFrame * 0.15f + i * 2) * matrix->height() / 2) + matrix->height() / 2;
                matrix->drawPixel((int)px, (int)py, dotColor);
            }
        } else {
            pacX += 1.5f;
            
            int mouthAngle = (int)(abs(sin(animFrame * 0.5f)) * 45);
            
            if (pacX < matrix->width() + 60) {
                // To achieve clipping, we draw the text and then cover it with black rectangles
                // Draw old time
                matrix->setCursor(tx, ty);
                matrix->setTextColor(matrix->color565(100, 100, 100));
                matrix->print(oldTimeStr);
                
                // Cover eaten part (left of pacman)
                matrix->fillRect(0, 0, (int)pacX, matrix->height(), 0);
                
                // Draw new time
                matrix->setCursor(tx, ty);
                matrix->setTextColor(color1);
                matrix->print(newTimeStr);
                
                // Cover uneaten part (right of pacman trailing tail)
                int revealX = (int)pacX - 50;
                if (revealX < matrix->width()) {
                    matrix->fillRect(revealX > 0 ? revealX : 0, 0, matrix->width(), matrix->height(), 0);
                }
                
                // Draw Pacman
                drawPacman((int)pacX, matrix->height() / 2, 6, mouthAngle, true);
                
                // Draw Ghosts
                for (int i = 0; i < 4; i++) {
                    float gx = pacX - 15 - (i * 12);
                    float gy = matrix->height() / 2 + sin(animFrame * 0.2f + i) * 2;
                    drawGhost((int)gx, (int)gy, 5, ghostColors[i], animFrame);
                }
            } else {
                transitioning = false;
                lastMinute = storedTime.minutes;
                strcpy(oldTimeStr, newTimeStr);
            }
        }
    }
}
