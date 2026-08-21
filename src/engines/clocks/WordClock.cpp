#include "WordClock.h"
#include "../../core/ConfigLoader.h"
#include <string.h>

WordClock::WordClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) {
    storedTime = {0, 0, 0};
}

void WordClock::draw(const TimeData& t) {
    storedTime = t;
}

void WordClock::update() {
    matrix->fillScreen(0);
    
    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 1;
    if (matrix->height() >= 64 && gfxSize == 1) gfxSize = 2; // Auto-scale for 64px if not specified
    
    String lang = (engineConfig ? engineConfig->getString("lang", "fr") : String("fr"));
    lang.toLowerCase();
    
    if (lang == "fr") {
        renderFR(storedTime.hours, storedTime.minutes, gfxSize);
    } else if (lang == "es") {
        renderES(storedTime.hours, storedTime.minutes, gfxSize);
    } else {
        renderEN(storedTime.hours, storedTime.minutes, gfxSize);
    }
}

void WordClock::drawLines(const std::vector<String>& lines, int gfxSize) {
    matrix->setTextSize(gfxSize);
    matrix->setFont(NULL);
    
    int lineSpacing = 4 * gfxSize;
    int totalH = 0;
    std::vector<int> lineHeights;
    
    for (const String& line : lines) {
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(line, 0, 0, &bx, &by, &bw, &bh);
        int finalLh = (bh == 0) ? (8 * gfxSize) : bh;
        lineHeights.push_back(finalLh);
        totalH += finalLh + lineSpacing;
    }
    if (totalH > 0) totalH -= lineSpacing;
    
    int y = (matrix->height() - totalH) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0);
    
    uint16_t color1 = matrix->color565(0, 220, 255);
    uint16_t color2 = matrix->color565(255, 120, 0);
    
    for (size_t i = 0; i < lines.size(); i++) {
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(lines[i], 0, 0, &bx, &by, &bw, &bh);
        int x = (matrix->width() - bw) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0);
        
        uint16_t color = (i % 2 == 0) ? color1 : color2;
        matrix->setTextColor(color);
        matrix->setCursor(x, y);
        matrix->print(lines[i]);
        
        y += lineHeights[i] + lineSpacing;
    }
}

void WordClock::renderFR(int hours, int minutes, int gfxSize) {
    int roundedM = (minutes / 5) * 5;
    bool pastHalf = minutes > 30;
    int displayH = (pastHalf && roundedM != 0) ? (hours + 1) % 24 : hours;
    int readH = displayH % 12;
    
    String strH;
    if (displayH == 0) strH = "MINUIT";
    else if (displayH == 12) strH = "MIDI";
    else {
        switch (readH) {
            case 1: strH = "UNE"; break;
            case 2: strH = "DEUX"; break;
            case 3: strH = "TROIS"; break;
            case 4: strH = "QUATRE"; break;
            case 5: strH = "CINQ"; break;
            case 6: strH = "SIX"; break;
            case 7: strH = "SEPT"; break;
            case 8: strH = "HUIT"; break;
            case 9: strH = "NEUF"; break;
            case 10: strH = "DIX"; break;
            case 11: strH = "ONZE"; break;
            default: strH = "?"; break;
        }
    }
    
    String strHSuffix = "";
    if (displayH != 0 && displayH != 12) {
        strHSuffix = (readH == 1) ? " HEURE" : " HEURES";
    }
    
    String strM;
    if (roundedM == 0 || roundedM == 60) strM = "PILE";
    else if (roundedM == 5 && !pastHalf) strM = "CINQ";
    else if (roundedM == 10 && !pastHalf) strM = "DIX";
    else if (roundedM == 15 && !pastHalf) strM = "ET QUART";
    else if (roundedM == 20 && !pastHalf) strM = "VINGT";
    else if (roundedM == 25 && !pastHalf) strM = "VINGT-CINQ";
    else if (roundedM == 30) strM = "ET DEMIE";
    else if (pastHalf) {
        int diff = 60 - roundedM;
        if (diff == 5) strM = "MOINS CINQ";
        else if (diff == 10) strM = "MOINS DIX";
        else if (diff == 15) strM = "MOINS LE QUART";
        else if (diff == 20) strM = "MOINS VINGT";
        else if (diff == 25) strM = "MOINS VINGT-CINQ";
        else strM = "MOINS " + String(diff);
    } else {
        strM = String(roundedM);
    }
    
    std::vector<String> lines = {
        "IL EST",
        strH + strHSuffix,
        strM
    };
    drawLines(lines, gfxSize);
}

void WordClock::renderEN(int hours, int minutes, int gfxSize) {
    int roundedM = (minutes / 5) * 5;
    bool pastHalf = minutes > 30;
    int displayH = (pastHalf && roundedM != 0) ? (hours + 1) % 24 : hours;
    int readH = displayH % 12;
    
    String strH;
    if (displayH == 0) strH = "MIDNIGHT";
    else if (displayH == 12) strH = "NOON";
    else {
        switch (readH) {
            case 1: strH = "ONE"; break;
            case 2: strH = "TWO"; break;
            case 3: strH = "THREE"; break;
            case 4: strH = "FOUR"; break;
            case 5: strH = "FIVE"; break;
            case 6: strH = "SIX"; break;
            case 7: strH = "SEVEN"; break;
            case 8: strH = "EIGHT"; break;
            case 9: strH = "NINE"; break;
            case 10: strH = "TEN"; break;
            case 11: strH = "ELEVEN"; break;
            default: strH = "?"; break;
        }
    }
    
    String strM;
    if (roundedM == 0 || roundedM == 60) strM = "O'CLOCK";
    else if (roundedM == 15) strM = "A QUARTER";
    else if (roundedM == 30) strM = "HALF";
    else if (pastHalf) {
        int diff = 60 - roundedM;
        if (diff == 15) strM = "A QUARTER";
        else strM = String(diff);
    } else {
        strM = String(roundedM);
    }
    
    String strConn = "";
    if (roundedM != 0 && roundedM != 60) {
        strConn = pastHalf ? "TO" : "PAST";
    }
    
    std::vector<String> lines;
    lines.push_back("IT IS");
    if (strConn == "") {
        if (displayH == 0 || displayH == 12) {
            lines.push_back(strH);
        } else {
            lines.push_back(strH);
            lines.push_back(strM);
        }
    } else {
        lines.push_back(strM);
        lines.push_back(strConn);
        lines.push_back(strH);
    }
    drawLines(lines, gfxSize);
}

void WordClock::renderES(int hours, int minutes, int gfxSize) {
    int roundedM = (minutes / 5) * 5;
    bool pastHalf = minutes > 30;
    int displayH = (pastHalf && roundedM != 0) ? (hours + 1) % 24 : hours;
    int readH = displayH % 12;
    
    String strH;
    if (displayH == 0) strH = "MEDIANOCHE";
    else if (displayH == 12) strH = "MEDIODIA";
    else {
        switch (readH) {
            case 1: strH = "LA UNA"; break;
            case 2: strH = "LAS DOS"; break;
            case 3: strH = "LAS TRES"; break;
            case 4: strH = "LAS CUATRO"; break;
            case 5: strH = "LAS CINCO"; break;
            case 6: strH = "LAS SEIS"; break;
            case 7: strH = "LAS SIETE"; break;
            case 8: strH = "LAS OCHO"; break;
            case 9: strH = "LAS NUEVE"; break;
            case 10: strH = "LAS DIEZ"; break;
            case 11: strH = "LAS ONCE"; break;
            default: strH = "?"; break;
        }
    }
    
    String strM;
    if (roundedM == 0 || roundedM == 60) strM = "EN PUNTO";
    else if (roundedM == 15 && !pastHalf) strM = "Y CUARTO";
    else if (roundedM == 30) strM = "Y MEDIA";
    else if (pastHalf) {
        int diff = 60 - roundedM;
        if (diff == 15) strM = "MENOS CUARTO";
        else strM = "MENOS " + String(diff);
    } else {
        strM = "Y " + String(roundedM);
    }
    
    std::vector<String> lines;
    if (displayH == 0 || displayH == 12) {
        if (roundedM == 0 || roundedM == 60) {
            lines.push_back("ES LA");
            lines.push_back(strH);
        } else {
            lines.push_back("ES LA");
            lines.push_back(strH);
            lines.push_back(strM);
        }
    } else {
        String prefix = (readH == 1 && displayH != 0 && displayH != 12) ? "ES LA" : "SON LAS";
        lines.push_back(prefix);
        lines.push_back(strH);
        lines.push_back(strM);
    }
    drawLines(lines, gfxSize);
}
