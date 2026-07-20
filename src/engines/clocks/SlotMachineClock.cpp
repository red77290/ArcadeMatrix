#include "SlotMachineClock.h"
#include "../../core/ConfigLoader.h"

extern ConfigLoader config;

SlotMachineClock::SlotMachineClock(MatrixPanel_I2S_DMA* display) : ClockFace(display) {
    storedTime = {0, 0, 0};
    strcpy(lastTimeStr, "");
    numDigits = 0;
    lastFrameTime = 0;
    
    for(int i=0; i<12; i++) {
        digits[i].targetChar = ' ';
        digits[i].currentChar = ' ';
        digits[i].yOffset = 0;
        digits[i].speed = 0;
        digits[i].spinning = false;
    }
}

void SlotMachineClock::draw(const TimeData& t) {
    storedTime = t;
}

void SlotMachineClock::update() {
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    
    if (strcmp(timeStr, lastTimeStr) != 0) {
        if (numDigits != strlen(timeStr)) {
            // Re-init
            numDigits = strlen(timeStr);
            for(int i=0; i<numDigits; i++) {
                digits[i].targetChar = timeStr[i];
                digits[i].currentChar = timeStr[i];
                digits[i].yOffset = 0;
                digits[i].spinning = false;
            }
        } else {
            // Trigger spinning for changed chars
            for(int i=0; i<numDigits; i++) {
                if (timeStr[i] != digits[i].targetChar) {
                    digits[i].targetChar = timeStr[i];
                    digits[i].spinning = true;
                    digits[i].speed = 3.0f + ((float)(rand() % 30) / 10.0f); // 3.0 to 6.0
                }
            }
        }
        strcpy(lastTimeStr, timeStr);
    }
    
    if (millis() - lastFrameTime > 30) {
        lastFrameTime = millis();
        
        matrix->fillScreen(0);
        
        int gfxSize = config.time.clock_size > 0 ? config.time.clock_size : 2;
        matrix->setTextSize(gfxSize);
        matrix->setFont(NULL);
        
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0) bw = 48; if (bh == 0) bh = 7 * gfxSize;
        
        int startX = (matrix->width() - bw) / 2 + config.time.clock_offset_x;
        int y = (matrix->height() - bh) / 2 + config.time.clock_offset_y;
        
        uint16_t color1 = matrix->color565(255, 255, 255);
        if (config.time.clock_color_1[0] == '#') {
            long c1 = strtol(&config.time.clock_color_1[1], NULL, 16);
            color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
        }
        
        int currentX = startX;
        for (int i = 0; i < numDigits; i++) {
            char cStr[2] = { digits[i].currentChar, '\0' };
            
            int16_t cbx, cby;
            uint16_t cbw, cbh;
            matrix->getTextBounds(cStr, 0, 0, &cbx, &cby, &cbw, &cbh);
            int advance = (config.time.clock_font == -1) ? (6 * gfxSize) : (cbw + 1);
            
            if (digits[i].spinning) {
                digits[i].yOffset += digits[i].speed;
                if (digits[i].yOffset >= bh + 4) {
                    digits[i].yOffset = 0;
                    // Random char while spinning
                    if (digits[i].targetChar >= '0' && digits[i].targetChar <= '9') {
                        digits[i].currentChar = '0' + (rand() % 10);
                        
                        // 10% chance to stop if near target? 
                        // Just stop after a random duration. We can tie speed decay to stop.
                    } else {
                        digits[i].currentChar = digits[i].targetChar;
                    }
                    digits[i].speed -= 0.1f;
                    
                    if (digits[i].speed <= 1.0f) {
                        digits[i].spinning = false;
                        digits[i].yOffset = 0;
                        digits[i].currentChar = digits[i].targetChar;
                    }
                }
                
                // Draw current scrolling down
                char drawStr1[2] = { digits[i].currentChar, '\0' };
                matrix->setCursor(currentX, y + (int)digits[i].yOffset);
                matrix->setTextColor(color1);
                matrix->print(drawStr1);
                
                // Draw next above
                char nextC = digits[i].currentChar + 1;
                if (nextC > '9') nextC = '0';
                char drawStr2[2] = { nextC, '\0' };
                matrix->setCursor(currentX, y + (int)digits[i].yOffset - (bh + 4));
                matrix->setTextColor(color1);
                matrix->print(drawStr2);
                
            } else {
                char drawStr[2] = { digits[i].targetChar, '\0' };
                matrix->setCursor(currentX, y);
                matrix->setTextColor(color1);
                matrix->print(drawStr);
            }
            
            currentX += advance;
        }
        
        // Draw frame top/bottom to hide scroll overflow
        matrix->fillRect(0, 0, matrix->width(), y, 0);
        matrix->fillRect(0, y + bh + 1, matrix->width(), matrix->height() - (y + bh + 1), 0);
    }
}
