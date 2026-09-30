#include "WordsClock.h"
#include "ClockFaceFont.h"
#include "WordsHourFont.h"
#include "WordsMinuteFont.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "../../core/I18n.h"

namespace {
constexpr uint16_t ACCENT = 0x2589;   // the muted blue the original uses for the hour and the date
}  // namespace

WordsClockFace::WordsClockFace(MatrixPanel_I2S_DMA* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 0, 0, 0 };
    glow = ClockFaceFont::resolveGlow(config);
}

void WordsClockFace::draw(const TimeData& t) {
    storedTime = t;
}

// Wording and date order come from the shared localisation layer, so the face follows the
// language the sign is set to rather than being English-only.
void WordsClockFace::timeInWords(int h, int m, char* hw, size_t hn, char* mw, size_t mn) const {
    I18n::getSpokenTime(h, m, I18n::getLang(), hw, hn, mw, mn);
}

void WordsClockFace::drawCentred(const char* text, int centreY, const GFXfont* font, uint16_t color,
                                 int panelW) {
    if (!text || !*text) return;
    matrix->setFont(font);
    matrix->setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    ClockFaceFont::print(*matrix, glow, (panelW - (int)bw) / 2 - bx,
                         centreY - (int)bh / 2 - by, text, color, false);
    matrix->setFont(nullptr);
}

void WordsClockFace::update() {
    if (!matrix) return;
    const int w = matrix->width();
    const int h = matrix->height();

    // This face is laid out for a wide panel; on a small one it would overlap itself, so it says so
    // rather than drawing a mess. See the theme name, which carries the same warning.
    if (w < 192 || h < 64) {
        if (m_dirty == 0) { m_hasFrame = false; return; }
        m_dirty--;
        m_hasFrame = true;
        matrix->fillRect(0, 0, w, h, 0x0000);
        matrix->setFont(nullptr);
        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(255, 160, 0));
        matrix->setCursor(2, h / 2 - 7);
        matrix->print("NEEDS");
        matrix->setCursor(2, h / 2 + 1);
        matrix->print("256x64");
        return;
    }

    if (lastMinute != storedTime.minutes) {
        lastMinute = storedTime.minutes;
        m_dirty = 2;
    }
    if (m_dirty == 0) { m_hasFrame = false; return; }
    m_dirty--;
    m_hasFrame = true;

    matrix->fillRect(0, 0, w, h, 0x0000);

    char hourWords[24], minuteWords[24];
    timeInWords(storedTime.hours, storedTime.minutes, hourWords, sizeof(hourWords),
                minuteWords, sizeof(minuteWords));

    // Same stack as the original: hour, minutes, a rule, then the date.
    const bool tall = (h >= 48);
    int hourY = tall ? (h * 22 / 100) : (h / 3);
    int minuteY = tall ? (h * 45 / 100) : (h * 2 / 3);
    int ruleY = tall ? (h * 63 / 100) : (h - 8);
    int dateY = tall ? (h * 80 / 100) : (h - 4);

    drawCentred(hourWords, hourY, &hour8pt7b, ACCENT, w);
    drawCentred(minuteWords, minuteY, &minute7pt7b, 0xFFFF, w);

    if (tall) {
        int ruleW = (w * 3) / 4;
        matrix->drawFastHLine((w - ruleW) / 2, ruleY, ruleW, 0xFFFF);

        char dateLine[24] = "";
        time_t nowT = time(nullptr);
        if (nowT > 1600000000) {
            struct tm lt;
            localtime_r(&nowT, &lt);
            // Month before day in English, day before month in French and Spanish.
            I18n::getDateLine(lt.tm_wday, lt.tm_mon, lt.tm_mday, I18n::getLang(),
                              dateLine, sizeof(dateLine));
        }
        drawCentred(dateLine, dateY, &minute7pt7b, ACCENT, w);
    }
}

void WordsClockFace::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    lastMinute = -1;
}
