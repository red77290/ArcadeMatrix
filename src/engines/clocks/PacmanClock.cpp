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
    } else if (lastMinute != storedTime.minutes && !transitioning) {
        transitioning = true;
        strcpy(newTimeStr, timeStr);
        pacX = -40.0f;
    }
    
    int pacRadius = max(4, (int)(6.0f * matrix->height() / 32.0f));
    int ghostRadius = pacRadius - 1;
    int ghostSpacing = pacRadius * 2;
    int tailLength = pacRadius * 4;
    
    // Advance animation state at most every 50ms, but always redraw below on every call. The
    // outer main loop clears the DMA back buffer and flips it every ~33ms unconditionally
    // (fixed ~30 FPS), so skipping the draw here (as this used to do) left the cleared buffer
    // flipped to screen as a black flash on every iteration where this throttle hadn't elapsed
    // yet - causing severe flicker. State (pacX, animFrame) only advances on the throttle, but
    // rendering below always redraws the current (possibly unchanged) state every call.
    if (true) {
        lastFrameTime = millis();
        animFrame++;
        
        if (transitioning) {
            float pacSpeed = max(1.5f, 3.0f * matrix->width() / 64.0f);
            pacX += pacSpeed;
            
            if (pacX >= matrix->width() + pacRadius * 3.0f) {
                transitioning = false;
                lastMinute = storedTime.minutes;
                strcpy(oldTimeStr, newTimeStr);
            }
        }
    }
    
    matrix->fillScreen(0);
    
    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 2;
    matrix->setTextSize(gfxSize);
    // Default GFX font
    matrix->setFont(NULL);
    
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds(newTimeStr, 0, 0, &bx, &by, &bw, &bh);
    if (bw == 0) bw = 30; if (bh == 0) bh = 7 * gfxSize;
    
    int tx = (matrix->width() - bw) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0);
    int ty = (matrix->height() - bh) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0);
    
    uint16_t color1 = matrix->color565(255, 255, 255);
    if ((engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[0] == '#') {
        long c1 = strtol(&(engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[1], NULL, 16);
        color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
    }
    if (color1 == 0) color1 = matrix->color565(255, 255, 255); // Fallback to white if black
    
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
            float px = (sin(animFrame * 0.1f + i) * matrix->width() / 2) + matrix->width() / 2;
            float py = (cos(animFrame * 0.15f + i * 2) * matrix->height() / 2) + matrix->height() / 2;
            matrix->drawPixel((int)px, (int)py, dotColor);
        }
    } else {
        int mouthAngle = (int)(abs(sin(animFrame * 0.5f)) * 45);
        
        // Draw old time
        matrix->setCursor(tx, ty);
        matrix->setTextColor(matrix->color565(100, 100, 100));
        matrix->print(oldTimeStr);
        
        // Cover eaten part (left of pacman)
        if (pacX > 0) {
            matrix->fillRect(0, 0, (int)pacX, matrix->height(), 0);
        }
        
        // Draw new time
        matrix->setCursor(tx, ty);
        matrix->setTextColor(color1);
        matrix->print(newTimeStr);
        
        // Cover uneaten part (right of reveal wave)
        int revealX = (int)pacX - pacRadius * 4;
        if (revealX > 0 && revealX < matrix->width()) {
            matrix->fillRect(revealX, 0, matrix->width() - revealX, matrix->height(), 0);
        }
        
        // Draw Pacman
        drawPacman((int)pacX, matrix->height() / 2, pacRadius, mouthAngle, true);
        
        // Draw Ghosts
        for (int i = 0; i < 4; i++) {
            float gx = pacX - (pacRadius * 3.0f) - (i * ghostSpacing);
            float gy = matrix->height() / 2 + sin(animFrame * 0.2f + i) * (pacRadius / 3.0f);
            drawGhost((int)gx, (int)gy, ghostRadius, ghostColors[i], animFrame);
        }
    }
}
