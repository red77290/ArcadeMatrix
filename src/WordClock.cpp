#include "WordClock.h"
#include <string.h>
#include <stdio.h>

WordClock::WordClock(MatrixPanel_I2S_DMA* display) : ClockFace(display) {
    storedTime = {0, 0, 0};
}

void WordClock::draw(const TimeData& t) {
    storedTime = t;
}

const char* WordClock::numberToFrench(int n) {
    const char* nums[] = {"MINUIT", "UNE", "DEUX", "TROIS", "QUATRE", "CINQ", "SIX", "SEPT", "HUIT", "NEUF", "DIX", "ONZE", "MIDI"};
    if (n >= 0 && n <= 12) {
        return nums[n];
    }
    static char buf[4];
    sprintf(buf, "%d", n);
    return buf;
}

void WordClock::update() {
    matrix->setFont(NULL);
    matrix->setTextSize(1);
    
    int h = storedTime.hours;
    int m = storedTime.minutes;
    
    bool isPastHalf = m > 32;
    int displayH = h % 24;
    
    if (isPastHalf) {
        displayH = (displayH + 1) % 24;
    }
    
    int readH = displayH % 12;
    char strH[32];
    if (displayH == 0) {
        strcpy(strH, "MINUIT");
    } else if (displayH == 12) {
        strcpy(strH, "MIDI");
    } else {
        sprintf(strH, "%s %s", numberToFrench(readH), (readH == 1) ? "HEURE" : "HEURES");
    }
    
    char strM[32];
    int roundedM = 5 * ((m + 2) / 5);
    
    if (roundedM == 0 || roundedM == 60) {
        strcpy(strM, "PILE");
    } else if (isPastHalf) {
        int diff = 60 - roundedM;
        if (diff == 15) strcpy(strM, "MOINS LE QUART");
        else sprintf(strM, "MOINS %d", diff);
    } else {
        if (roundedM == 15) strcpy(strM, "ET QUART");
        else if (roundedM == 30) strcpy(strM, "ET DEMIE");
        else sprintf(strM, "%d", roundedM);
    }
    
    const char* lines[3] = {"IL EST", strH, strM};
    uint16_t c1 = matrix->color565(255, 255, 255);
    uint16_t c2 = matrix->color565(150, 150, 150);
    
    int totalH = 3 * 8 + 4; // 3 lines of 8px height + 2 gaps
    int y = (matrix->height() - totalH) / 2;
    
    for (int i = 0; i < 3; i++) {
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(lines[i], 0, 0, &bx, &by, &bw, &bh);
        int x = (matrix->width() - bw) / 2;
        
        matrix->setTextColor((i % 2 == 0) ? c1 : c2);
        matrix->setCursor(x, y);
        matrix->print(lines[i]);
        y += 10;
    }
}
