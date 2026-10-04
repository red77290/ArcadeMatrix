#include "WeatherLayout.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "WeatherIcon.h"

namespace weather_layout {

namespace {

// Adafruit GFX draws the built-in font's degree glyph (CP437 0xF8) for 0xF7 while CP437 mode is off.
constexpr char kDegree = (char)0xF7;

int textW(const char* t, int size) {
    size_t n = strlen(t);
    return n ? (int)n * 6 * size - size : 0;
}

/// Long text when it fits `w`, else the short one, else the short one cut ("Partl." style).
void fitText(const char* longText, const char* shortText, int w, int size, char* out, size_t cap) {
    if (longText[0] && textW(longText, size) <= w) { strlcpy(out, longText, cap); return; }
    strlcpy(out, shortText[0] ? shortText : longText, cap);
    const int maxChars = (w + size) / (6 * size) > 1 ? (w + size) / (6 * size) : 1;
    if ((int)strlen(out) > maxChars && (size_t)maxChars < cap) {
        if (maxChars > 3) out[maxChars - 1] = '.';
        out[maxChars] = '\0';
    }
}

void printShadowed(MatrixPanel_I2S_DMA* m, int x, int y, const char* t, uint16_t c, uint16_t shadow) {
    m->setTextColor(shadow);
    m->setCursor(x + 1, y + 1);
    m->print(t);
    m->setTextColor(c);
    m->setCursor(x, y);
    m->print(t);
}

void cut(char* t, int maxChars) {
    if ((int)strlen(t) > maxChars) {
        if (maxChars > 3) t[maxChars - 1] = '.';
        t[maxChars] = '\0';
    }
}

}  // namespace

void setRange(Page& p, float low, float high, bool fahrenheit) {
    const char unit = fahrenheit ? 'F' : 'C';
    p.fahrenheit = fahrenheit;
    p.topIsHigh = fahrenheit;
    snprintf(p.top, sizeof(p.top), "%ld%c%c", lroundf(fahrenheit ? high : low), kDegree, unit);
    snprintf(p.bottom, sizeof(p.bottom), "%ld%c%c", lroundf(fahrenheit ? low : high), kDegree, unit);
}

void draw(MatrixPanel_I2S_DMA* m, const Page& p, int ox, int oy, uint16_t shadow) {
    if (!m) return;
    m->setFont(nullptr);
    m->cp437(false);   // the degree byte relies on it (another engine may have left it on)
    m->setTextWrap(false);

    const int mw = m->width(), mh = m->height();
    const uint16_t white = m->color565(255, 255, 255);
    const uint16_t cyan = m->color565(120, 200, 255);       // the low
    const uint16_t orange = m->color565(255, 150, 50);      // the high
    const uint16_t colorLabel = m->color565(180, 180, 255);
    const uint16_t colorDesc = m->color565(210, 210, 210);
    const uint16_t topColor = p.isNow ? white : (p.topIsHigh ? orange : cyan);
    const uint16_t bottomColor = p.isNow ? colorDesc : (p.topIsHigh ? cyan : orange);
    const char* icon = p.icon ? p.icon : "";

    const bool wide = mw >= 256 && mh >= 64;
    const bool mid = !wide && mw >= 128 && mh <= 32;
    if (wide || mid) {
        const int s = wide ? 2 : 1;
        const int margin = wide ? 8 : 4;
        const int iconX = margin + ox;
        WeatherIcon::draw(m, icon, iconX, (mh - 24 * s) / 2 + oy, s);
        const int row1 = (wide ? 10 : 4) + oy;
        const int row2 = (wide ? 38 : 18) + oy;
        const int rightEdge = mw - margin + ox;
        const int midX = iconX + 24 * s + margin;

        // A now page's second line must start at or after the label column, else its short form.
        const char* bottom = p.bottom;
        if (p.isNow && p.bottomShort[0] && rightEdge - textW(p.bottom, s) < midX) bottom = p.bottomShort;

        m->setTextSize(s);
        printShadowed(m, rightEdge - textW(p.top, s), row1, p.top, topColor, shadow);
        printShadowed(m, rightEdge - textW(bottom, s), row2, bottom, bottomColor, shadow);

        // The widest temperature sets the middle column (on a now page only the reading does).
        const int tempW = p.isNow ? textW(p.top, s)
                                  : (textW(p.top, s) > textW(bottom, s) ? textW(p.top, s) : textW(bottom, s));
        const int midW = rightEdge - tempW - margin - midX;
        char text[40];
        if (wide) {
            strlcpy(text, (p.labelLong[0] && textW(p.labelLong, 2) <= midW) ? p.labelLong : p.label, sizeof(text));
            m->setTextSize(2);
            m->setTextColor(colorLabel);
            m->setCursor(midX, row1);
            m->print(text);
            if (!p.isNow && (p.descLong[0] || p.desc[0])) {
                const char* desc = p.descLong[0] ? p.descLong : p.desc;
                int size = textW(desc, 2) <= midW ? 2 : 1;
                if (size == 1 && textW(desc, 1) > midW) desc = p.desc;   // last resort: short form
                m->setTextSize(size);
                m->setTextColor(colorDesc);
                m->setCursor(midX, size == 2 ? row2 : row2 + 4);
                m->print(desc);
            }
        } else {
            fitText(p.labelLong, p.label, midW, 1, text, sizeof(text));
            m->setTextColor(colorLabel);
            m->setCursor(midX, row1);
            m->print(text);
            if (!p.isNow && (p.descLong[0] || p.desc[0])) {
                fitText(p.descLong, p.desc, midW, 1, text, sizeof(text));
                m->setTextColor(colorDesc);
                m->setCursor(midX, row2);
                m->print(text);
            }
        }
        return;
    }

    m->setTextSize(1);
    if (mw <= 64 && mh <= 32) {
        // 64x32: label, both temperatures on one line, condition.
        const int iconX = 2 + ox;
        WeatherIcon::draw(m, icon, iconX, (mh - 24) / 2 + oy, 1);
        const int rightX = iconX + 26;
        const int maxChars = (mw - rightX - 1) / 6 > 1 ? (mw - rightX - 1) / 6 : 1;
        m->setTextColor(colorLabel);
        m->setCursor(rightX, 2 + oy);
        m->print(p.label);
        m->setTextColor(topColor);
        m->setCursor(rightX, 11 + oy);
        m->print(p.top);
        if (!p.isNow) {
            m->setTextColor(bottomColor);
            m->setCursor(rightX + (int)strlen(p.top) * 6 + 2, 11 + oy);
            m->print(p.bottom);
        }
        char text[40];
        strlcpy(text, p.isNow ? (p.bottomShort[0] ? p.bottomShort : p.bottom) : p.desc, sizeof(text));
        cut(text, maxChars);
        m->setTextColor(colorDesc);
        m->setCursor(rightX, 21 + oy);
        m->print(text);
        return;
    }

    // Square / vertical panels: everything centred in a column.
    WeatherIcon::draw(m, icon, (mw - 24) / 2 + ox, 14 + oy, 1);
    m->setTextColor(colorLabel);
    m->setCursor((mw - (int)strlen(p.label) * 6) / 2 + ox, 3 + oy);
    m->print(p.label);
    const char* line = p.isNow ? (p.bottomShort[0] ? p.bottomShort : p.bottom) : p.desc;
    m->setTextColor(colorDesc);
    m->setCursor((mw - (int)strlen(line) * 6) / 2 + ox, 40 + oy);
    m->print(line);
    m->setTextColor(topColor);
    m->setCursor((mw - (int)strlen(p.top) * 6) / 2 + ox, 49 + oy);
    m->print(p.top);
    if (!p.isNow) {
        m->setTextColor(bottomColor);
        m->setCursor((mw - (int)strlen(p.bottom) * 6) / 2 + ox, 57 + oy);
        m->print(p.bottom);
    }
}

}  // namespace weather_layout
