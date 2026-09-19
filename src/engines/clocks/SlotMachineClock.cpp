#include "SlotMachineClock.h"
#include "../../core/ConfigLoader.h"
#include <string.h>

SlotMachineClock::SlotMachineClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) { faceFont.load(config);
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
    } else if ((lastMinute != storedTime.minutes || strcmp(currentTime, timeStr) != 0) && !spinning) {
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
    
    int w = matrix->width();
    int h = matrix->height();
    bool isTate = (w < 48 || h > (w * 3) / 2);

    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 2;
    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;

    uint16_t color1 = matrix->color565(255, 255, 255);
    if ((engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[0] == '#') {
        long c1 = strtol(&(engineConfig ? engineConfig->getString("clock_color_1", "") : String(""))[1], NULL, 16);
        color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
    }
    if (color1 == 0) color1 = matrix->color565(255, 255, 255);

    uint16_t frameColor = spinning ? matrix->color565(200, 150, 0) : matrix->color565(80, 80, 80);
    if (!spinning && (animFrame / 20) % 2 == 0) {
        frameColor = matrix->color565(255, 220, 0); // Winning golden border blink
    }

    uint16_t red = matrix->color565(255, 0, 0);
    uint16_t green = matrix->color565(0, 255, 0);
    uint16_t leftCol = ((animFrame / 5) % 2 == 0) ? red : green;
    uint16_t rightCol = ((animFrame / 5) % 2 == 0) ? green : red;

    matrix->setFont(NULL);

    if (isTate) {
        // Stacked Portrait Layout (HH on top, MM on bottom)
        char hStr[4], mStr[4];
        hStr[0] = currentTime[0]; hStr[1] = currentTime[1]; hStr[2] = '\0';
        mStr[0] = currentTime[3]; mStr[1] = currentTime[4]; mStr[2] = '\0';

        int scale = (w >= 64) ? 3 : 2;
        if (gfxSize >= 1 && gfxSize <= 4) scale = min(scale, gfxSize);
        faceFont.apply(*matrix, scale, "88", w, h / 2);

        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds("88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 11 * scale; if (bh == 0) bh = 7 * scale;

        int tx = (w - bw) / 2 + offX;            // text box (used for the reel frames and clipping)
        int tyH = (h / 4) - (bh / 2) + offY;
        int tyM = (3 * h / 4) - (bh / 2) + offY;
        int cx = tx - bx, cyH = tyH - by, cyM = tyM - by;   // print cursor: custom fonts use the baseline

        // Slot Machine Reels Boxes
        matrix->drawRect(tx - 3, tyH - 2, bw + 6, bh + 4, frameColor);
        matrix->drawRect(tx - 3, tyM - 2, bw + 6, bh + 4, frameColor);

        if (spinning) {
            int th2 = bh * 2;
            int blurY = (int)yOffset % th2;

            matrix->setTextColor(matrix->color565(80, 80, 80));
            matrix->setCursor(cx, cyH + blurY - th2); matrix->print("88");
            matrix->setCursor(cx, cyM + blurY - th2); matrix->print("88");

            matrix->setTextColor(matrix->color565(40, 40, 40));
            matrix->setCursor(cx, cyH + blurY); matrix->print("00");
            matrix->setCursor(cx, cyM + blurY); matrix->print("00");

            // Clip overflow
            matrix->fillRect(0, 0, w, tyH - 2, 0);
            matrix->fillRect(0, tyH + bh + 2, w, (tyM - 2) - (tyH + bh + 2), 0);
            matrix->fillRect(0, tyM + bh + 2, w, h - (tyM + bh + 2), 0);
        } else {
            // Outline & Text
            matrix->setTextColor(0);
            matrix->setCursor(cx - 1, cyH); matrix->print(hStr);
            matrix->setCursor(cx + 1, cyH); matrix->print(hStr);
            matrix->setCursor(cx, cyH); matrix->setTextColor(color1); matrix->print(hStr);

            matrix->setTextColor(0);
            matrix->setCursor(cx - 1, cyM); matrix->print(mStr);
            matrix->setCursor(cx + 1, cyM); matrix->print(mStr);
            matrix->setCursor(cx, cyM); matrix->setTextColor(color1); matrix->print(mStr);
        }

        // Blinking LEDs
        matrix->drawPixel(tx - 6, tyH + bh / 2, leftCol);
        matrix->drawPixel(tx + bw + 5, tyH + bh / 2, rightCol);
        matrix->drawPixel(tx - 6, tyM + bh / 2, rightCol);
        matrix->drawPixel(tx + bw + 5, tyM + bh / 2, leftCol);
    } else {
        // Landscape / Widescreen Layout
        faceFont.apply(*matrix, gfxSize, "88:88", w, h);
        int16_t bx, by;
        uint16_t bw, bh;
        // Use fixed reference "88:88" to avoid shifting when '1' appears
        matrix->getTextBounds("88:88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 29 * gfxSize; if (bh == 0) bh = 7 * gfxSize;
        
        int tx = (w - bw) / 2 + offX;            // text box (frame + clipping)
        int ty = (h - bh) / 2 + offY;
        int cx = tx - bx, cy = ty - by;          // print cursor (baseline for custom fonts)

        matrix->drawRect(tx - 4, ty - 2, bw + 8, bh + 4, frameColor);
        
        if (spinning) {
            int th2 = bh * 2;
            int blurY = ty + ((int)yOffset % th2);
            
            matrix->setTextColor(matrix->color565(80, 80, 80));
            matrix->setCursor(cx, blurY - th2 - by);
            matrix->print("88:88");
            
            matrix->setTextColor(matrix->color565(40, 40, 40));
            matrix->setCursor(cx, blurY - by);
            matrix->print("00:00");
            
            // Clip overflow
            matrix->fillRect(0, 0, w, ty - 2, 0);
            matrix->fillRect(0, ty + bh + 2, w, h - (ty + bh + 2), 0);
        } else {
            // Black outline
            matrix->setTextColor(0);
            matrix->setCursor(cx - 1, cy); matrix->print(currentTime);
            matrix->setCursor(cx + 1, cy); matrix->print(currentTime);
            matrix->setCursor(cx, cy - 1); matrix->print(currentTime);
            matrix->setCursor(cx, cy + 1); matrix->print(currentTime);

            matrix->setTextColor(color1);
            matrix->setCursor(cx, cy);
            matrix->print(currentTime);
        }
        
        // Decorative blinking LED dots on both sides
        int ledY = ty + bh / 2;
        matrix->drawPixel(tx - 7, ledY - 1, leftCol);
        matrix->drawPixel(tx - 7, ledY, leftCol);
        matrix->drawPixel(tx + bw + 6, ledY - 1, rightCol);
        matrix->drawPixel(tx + bw + 6, ledY, rightCol);
    }
}
