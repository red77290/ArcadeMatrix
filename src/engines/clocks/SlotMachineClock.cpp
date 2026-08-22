#include "SlotMachineClock.h"
#include "../../core/ConfigLoader.h"
#include <string.h>

SlotMachineClock::SlotMachineClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) {
    storedTime = {0, 0, 0};
    lastMinute = -1;
    animFrame = 0;
    spinning = false;
    spinSpeed = 0.0f;
    yOffset = 0.0f;
    strcpy(currentTime, "00:00");
    strcpy(targetTime, "00:00");
    lastFrameTime = 0;
}

void SlotMachineClock::draw(const TimeData& t) {
    storedTime = t;
}

void SlotMachineClock::update() {
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d", storedTime.hours, storedTime.minutes);
    
    if (lastMinute == -1) {
        lastMinute = storedTime.minutes;
        strcpy(currentTime, timeStr);
        strcpy(targetTime, timeStr);
    } else if (lastMinute != storedTime.minutes && !spinning) {
        spinning = true;
        spinSpeed = 15.0f;
        strcpy(targetTime, timeStr);
    } else if (!spinning) {
        strcpy(currentTime, timeStr);
    }

    if (true) {
        lastFrameTime = millis();
        animFrame++;
        
        if (spinning) {
            yOffset += spinSpeed;
            spinSpeed *= 0.95f;

            if (spinSpeed < 0.5f) {
                spinning = false;
                strcpy(currentTime, targetTime);
                lastMinute = storedTime.minutes;
                yOffset = 0.0f;
            }
        }
    }

    matrix->fillScreen(0);
    
    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 2;
    matrix->setTextSize(gfxSize);
    matrix->setFont(NULL);
    
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds(currentTime, 0, 0, &bx, &by, &bw, &bh);
    if (bw == 0) bw = 30; if (bh == 0) bh = 7 * gfxSize;
    
    int tx = (matrix->width() - bw) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0);
    int ty = (matrix->height() - bh) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0);

    // Draw slot machine border
    uint16_t frameColor = spinning ? matrix->color565(200, 150, 0) : matrix->color565(80, 80, 80);
    
    if (!spinning && (animFrame / 20) % 2 == 0) {
        frameColor = matrix->color565(255, 220, 0); // Winning golden border blink
    }

    matrix->drawRect(tx - 4, ty - 2, bw + 8, bh + 4, frameColor);
    // Draw an extra inner outline if we want thickness, but Rust does a 1px frame + corners. We'll stick to a simple drawRect.
    
    if (spinning) {
        // Draw blurred spinning text
        int th2 = bh * 2;
        int blurY = ty + ((int)yOffset % th2);
        
        matrix->setTextColor(matrix->color565(80, 80, 80));
        matrix->setCursor(tx, blurY - th2);
        matrix->print("88:88");
        
        matrix->setTextColor(matrix->color565(40, 40, 40));
        matrix->setCursor(tx, blurY);
        matrix->print("00:00");
        
        // Clip overflow (cover above and below frame)
        matrix->fillRect(0, 0, matrix->width(), ty - 1, 0);
        matrix->fillRect(0, ty + bh + 1, matrix->width(), matrix->height() - (ty + bh + 1), 0);
    } else {
        uint16_t color1 = matrix->color565(255, 255, 255);
        if ((engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[0] == '#') {
            long c1 = strtol(&(engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[1], NULL, 16);
            color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
        }
        if (color1 == 0) color1 = matrix->color565(255, 255, 255); // Fallback to white if black
        
        // Black outline
        matrix->setTextColor(0);
        matrix->setCursor(tx - 1, ty); matrix->print(currentTime);
        matrix->setCursor(tx + 1, ty); matrix->print(currentTime);
        matrix->setCursor(tx, ty - 1); matrix->print(currentTime);
        matrix->setCursor(tx, ty + 1); matrix->print(currentTime);

        matrix->setTextColor(color1);
        matrix->setCursor(tx, ty);
        matrix->print(currentTime);
    }
    
    // Decorative blinking LED dots on both sides
    int ledY = ty + bh / 2;
    uint16_t red = matrix->color565(255, 0, 0);
    uint16_t green = matrix->color565(0, 255, 0);
    uint16_t leftCol = ((animFrame / 5) % 2 == 0) ? red : green;
    uint16_t rightCol = ((animFrame / 5) % 2 == 0) ? green : red;
    
    matrix->drawPixel(tx - 8, ledY - 1, leftCol);
    matrix->drawPixel(tx - 8, ledY, leftCol);
    matrix->drawPixel(tx + bw + 8, ledY - 1, rightCol);
    matrix->drawPixel(tx + bw + 8, ledY, rightCol);
}
