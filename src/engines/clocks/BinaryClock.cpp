#include "BinaryClock.h"

BinaryClock::BinaryClock(MatrixPanel_I2S_DMA* display) : ClockFace(display) {
    storedTime = {0, 0, 0};
}

void BinaryClock::draw(const TimeData& t) {
    storedTime = t;
}

void BinaryClock::update() {
    int digits[6];
    digits[0] = storedTime.hours / 10;
    digits[1] = storedTime.hours % 10;
    digits[2] = storedTime.minutes / 10;
    digits[3] = storedTime.minutes % 10;
    digits[4] = storedTime.seconds / 10;
    digits[5] = storedTime.seconds % 10;
    
    int maxBits[6] = {2, 4, 3, 4, 3, 4};
    
    int dotRadius = matrix->width() / 20;
    if (dotRadius > matrix->height() / 12) dotRadius = matrix->height() / 12;
    if (dotRadius < 2) dotRadius = 2;
    
    int spacingX = matrix->width() / 8;
    int spacingY = matrix->height() / 6;
    
    int startX = (matrix->width() - (5 * spacingX)) / 2;
    int startY = matrix->height() - (matrix->height() / 6);
    
    uint16_t colorH = matrix->color565(0, 220, 255);
    uint16_t colorM = matrix->color565(255, 0, 180);
    uint16_t colorS = matrix->color565(200, 200, 200);
    uint16_t colorDim = matrix->color565(25, 25, 25);
    
    for (int col = 0; col < 6; col++) {
        int x = startX + col * spacingX;
        if (col >= 2) x += spacingX / 2;
        if (col >= 4) x += spacingX / 2;
        
        for (int bit = 0; bit < maxBits[col]; bit++) {
            int y = startY - bit * spacingY;
            bool isOn = (digits[col] >> bit) & 1;
            
            if (isOn) {
                uint16_t c = (col < 2) ? colorH : ((col < 4) ? colorM : colorS);
                matrix->fillCircle(x, y, dotRadius, c);
            } else {
                matrix->drawCircle(x, y, dotRadius, colorDim);
            }
        }
    }
}
