#include "WordClock.h"
#include "../../core/ConfigLoader.h"
#include "../../core/I18n.h"
#include "../fonts/ArcadeFonts.h"
#include <string.h>

WordClock::WordClock(IDrawingSurface* display, const EngineConfig* config) : ClockFace(display, config) {
    storedTime = {0, 0, 0};
    String fontSetting = engineConfig ? engineConfig->getString("clock_font", "") : "";
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("font", "");
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("clock_font_path", "");
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("font_path", "");
    if (fontSetting.length() > 0 && !fontSetting.equalsIgnoreCase("Default")) {
        if (fontSetting.endsWith(".amf") || fontSetting.endsWith(".AMF") || fontSetting.startsWith("/")) {
            if (!customFont.loadFromSD(fontSetting.c_str())) {
                String altPath = fontSetting.startsWith("/") ? fontSetting : ("/fonts/" + fontSetting);
                customFont.loadFromSD(altPath.c_str());
            }
        }
    }
}

void WordClock::draw(const TimeData& t) {
    if (storedTime.hours != t.hours || storedTime.minutes != t.minutes) {
        storedTime = t;
        _dirty = true;
    }
}

void WordClock::update() {
    matrix->fillScreen(0);
    
    if (_dirty || storedTime.hours != _lastHours || storedTime.minutes != _lastMinutes) {
        _lastHours = storedTime.hours;
        _lastMinutes = storedTime.minutes;
        _dirty = false;
        recomputeLines();
    }

    matrix->setFont(_cachedFont);
    matrix->setTextSize(_cachedGfxSize);
    for (uint8_t i = 0; i < _cachedLineCount; i++) {
        matrix->setTextColor(_cachedLines[i].color);
        matrix->setCursor(_cachedLines[i].x, _cachedLines[i].y);
        matrix->print(_cachedLines[i].text);
    }
}

void WordClock::recomputeLines() {
    _cachedLineCount = 0;
    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (gfxSize < 1) gfxSize = 1;
    _cachedGfxSize = gfxSize;

    std::vector<String> rawLines = I18n::getWordClockLines(storedTime.hours, storedTime.minutes);

    const GFXfont* chosenFont = nullptr;
    String fontSetting = engineConfig ? engineConfig->getString("clock_font", "") : "";
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("font", "");
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("clock_font_path", "");
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("font_path", "");

    if (fontSetting.equalsIgnoreCase("PressStart2P") || fontSetting.equalsIgnoreCase("PressStart2P.ttf")) {
        chosenFont = &PressStart2P9pt7b;
    } else if (fontSetting.equalsIgnoreCase("namco") || fontSetting.equalsIgnoreCase("namco.ttf")) {
        chosenFont = &namco__9pt7b;
    } else if (fontSetting.equalsIgnoreCase("FreeSansBold") || fontSetting.equalsIgnoreCase("FreeSansBold.ttf")) {
        chosenFont = &FreeSansBold9pt7b;
    } else if (fontSetting.equalsIgnoreCase("FreeMonoBold") || fontSetting.equalsIgnoreCase("FreeMonoBold.ttf")) {
        chosenFont = &FreeMonoBold9pt7b;
    } else if (fontSetting.equalsIgnoreCase("RetroGaming") || fontSetting.equalsIgnoreCase("Retro_Gaming") || fontSetting.equalsIgnoreCase("RetroGaming.ttf")) {
        chosenFont = &Retro_Gaming9pt7b;
    } else if (customFont.getFont()) {
        chosenFont = customFont.getFont();
    }

    // If the chosen font is too tall for a 3-line word clock (e.g. 27px tall font on 32px/64px matrix),
    // fallback to clean 5x7 so all 3 lines can be displayed and scaled by the size slider
    if (chosenFont != nullptr) {
        matrix->setFont(chosenFont);
        matrix->setTextSize(1);
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        matrix->getTextBounds("TEST", 0, 0, &tbx, &tby, &tbw, &tbh);
        if (tbh > (matrix->height() / 2)) {
            chosenFont = nullptr;
        }
    }
    _cachedFont = chosenFont;

    matrix->setFont(chosenFont);
    matrix->setTextSize(gfxSize);

    // Break down any long lines that exceed screen width into wrapped lines
    std::vector<String> lines;
    int maxCharsPerLine = max(1, matrix->width() / (chosenFont ? 12 : (6 * gfxSize)));

    for (const String& rawLine : rawLines) {
        if ((int)rawLine.length() <= maxCharsPerLine) {
            lines.push_back(rawLine);
        } else {
            int start = 0;
            while (start < (int)rawLine.length()) {
                int nextSpace = rawLine.indexOf(' ', start);
                if (nextSpace == -1) {
                    lines.push_back(rawLine.substring(start));
                    break;
                }
                int afterSpace = rawLine.indexOf(' ', nextSpace + 1);
                if (afterSpace != -1 && (afterSpace - start) <= maxCharsPerLine) {
                    lines.push_back(rawLine.substring(start, afterSpace));
                    start = afterSpace + 1;
                } else {
                    lines.push_back(rawLine.substring(start, nextSpace));
                    start = nextSpace + 1;
                }
            }
        }
    }
    
    int lineSpacing = (matrix->height() >= 64 ? 3 : 1) * gfxSize;
    int totalH = 0;
    int lineHeights[8] = {0};
    size_t numLines = min(lines.size(), (size_t)8);
    
    for (size_t i = 0; i < numLines; i++) {
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(lines[i], 0, 0, &bx, &by, &bw, &bh);
        int finalLh = (bh == 0) ? ((chosenFont ? 10 : 8) * gfxSize) : bh;
        lineHeights[i] = finalLh;
        totalH += finalLh + lineSpacing;
    }
    if (totalH > 0) totalH -= lineSpacing;

    if (totalH > matrix->height() && lineSpacing > 1) {
        lineSpacing = 1;
        totalH = 0;
        for (size_t i = 0; i < numLines; i++) totalH += lineHeights[i] + lineSpacing;
        if (totalH > 0) totalH -= lineSpacing;
    }
    
    int y = (matrix->height() - totalH) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0);
    
    uint16_t color1 = matrix->color565(0, 220, 255);
    uint16_t color2 = matrix->color565(255, 120, 0);
    
    for (size_t i = 0; i < numLines; i++) {
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(lines[i], 0, 0, &bx, &by, &bw, &bh);
        int x = (matrix->width() - bw) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0) - (chosenFont ? bx : 0);
        int curY = y - (chosenFont ? by : 0);
        
        WordClockLine& l = _cachedLines[_cachedLineCount++];
        strncpy(l.text, lines[i].c_str(), sizeof(l.text) - 1);
        l.text[sizeof(l.text) - 1] = '\0';
        l.x = x;
        l.y = curY;
        l.color = (i % 2 == 0) ? color1 : color2;
        
        y += lineHeights[i] + lineSpacing;
    }
}
