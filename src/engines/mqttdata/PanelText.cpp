#include "PanelText.h"
#include "../../../include/core/I18n.h"

namespace panel_text {

namespace {

// T: 3x5 glyphs, five rows of three bits each (high bit = left pixel), row 0 in the top bits.
#define T5(a, b, c, d, e) (uint16_t)(((a) << 12) | ((b) << 9) | ((c) << 6) | ((d) << 3) | (e))
struct TinyGlyph { char ch; uint16_t bits; };
static const TinyGlyph kTiny[] = {
    {' ', T5(0, 0, 0, 0, 0)}, {'-', T5(0, 0, 7, 0, 0)}, {'.', T5(0, 0, 0, 0, 2)},
    {'%', T5(5, 1, 2, 4, 5)}, {'$', T5(3, 6, 2, 3, 6)}, {graph::DEGREE, T5(2, 5, 2, 0, 0)},
    {'/', T5(1, 1, 2, 4, 4)}, {':', T5(0, 2, 0, 2, 0)}, {'+', T5(0, 2, 7, 2, 0)},
    {'?', T5(7, 1, 2, 0, 2)},
    {'0', T5(7, 5, 5, 5, 7)}, {'1', T5(2, 6, 2, 2, 7)}, {'2', T5(7, 1, 7, 4, 7)},
    {'3', T5(7, 1, 7, 1, 7)}, {'4', T5(5, 5, 7, 1, 1)}, {'5', T5(7, 4, 7, 1, 7)},
    {'6', T5(7, 4, 7, 5, 7)}, {'7', T5(7, 1, 1, 2, 2)}, {'8', T5(7, 5, 7, 5, 7)},
    {'9', T5(7, 5, 7, 1, 7)},
    {'A', T5(2, 5, 7, 5, 5)}, {'B', T5(6, 5, 6, 5, 6)}, {'C', T5(3, 4, 4, 4, 3)},
    {'D', T5(6, 5, 5, 5, 6)}, {'E', T5(7, 4, 6, 4, 7)}, {'F', T5(7, 4, 6, 4, 4)},
    {'G', T5(3, 4, 5, 5, 3)}, {'H', T5(5, 5, 7, 5, 5)}, {'I', T5(7, 2, 2, 2, 7)},
    {'J', T5(1, 1, 1, 5, 2)}, {'K', T5(5, 5, 6, 5, 5)}, {'L', T5(4, 4, 4, 4, 7)},
    {'M', T5(5, 7, 7, 5, 5)}, {'N', T5(6, 5, 5, 5, 5)}, {'O', T5(2, 5, 5, 5, 2)},
    {'P', T5(6, 5, 6, 4, 4)}, {'Q', T5(2, 5, 5, 7, 3)}, {'R', T5(6, 5, 6, 5, 5)},
    {'S', T5(3, 4, 2, 1, 6)}, {'T', T5(7, 2, 2, 2, 2)}, {'U', T5(5, 5, 5, 5, 7)},
    {'V', T5(5, 5, 5, 5, 2)}, {'W', T5(5, 5, 7, 7, 5)}, {'X', T5(5, 5, 2, 5, 5)},
    {'Y', T5(5, 5, 2, 2, 2)}, {'Z', T5(7, 1, 2, 4, 7)},
};
#undef T5

bool tinyBits(char c, uint16_t& bits) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (const auto& g : kTiny) {
        if (g.ch == c) { bits = g.bits; return true; }
    }
    return false;
}

inline void px(MatrixPanel_I2S_DMA* m, int x, int y, int s, uint16_t color, int clipX) {
    if (x >= clipX) return;
    int w = (x + s > clipX) ? clipX - x : s;
    m->fillRect(x, y, w, s, color);
}

// The built-in font's degree glyph (CP437 0xF8): columns 1..4 of the 5x7 cell, rows 0..3.
void drawDegreeG(MatrixPanel_I2S_DMA* m, int x, int y, int s, uint16_t color, int clipX) {
    static const uint8_t kRing[4] = {0x06, 0x09, 0x09, 0x06};   // bit 3 = column 1
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (kRing[r] & (0x8 >> c)) px(m, x + (c + 1) * s, y + r * s, s, color, clipX);
        }
    }
}

}  // namespace

void print(MatrixPanel_I2S_DMA* m, int x, int y, const char* text, int font, uint16_t color, int clipX) {
    if (!m || !text) return;
    if (font == TINY) {
        int cx = x;
        for (const char* p = text; *p && cx < clipX; p++, cx += 4) {
            uint16_t bits;
            if (!tinyBits(*p, bits)) continue;   // unknown: advance, draw nothing
            for (int r = 0; r < 5; r++) {
                uint8_t row = (bits >> (12 - 3 * r)) & 0x7;
                for (int c = 0; c < 3; c++) {
                    if (row & (0x4 >> c)) px(m, cx + c, y + r, 1, color, clipX);
                }
            }
        }
        return;
    }
    const int s = font;
    m->setFont(nullptr);
    m->setTextWrap(false);
    int cx = x;
    for (const char* p = text; *p && cx < clipX; p++, cx += 6 * s) {
        if (*p == graph::DEGREE) {
            drawDegreeG(m, cx, y, s, color, clipX);
            continue;
        }
        m->drawChar(cx, y, *p, color, color, s);   // bg == fg: transparent background
        if (cx + 5 * s > clipX) m->fillRect(clipX, y, cx + 5 * s - clipX, 7 * s, 0);   // blank the overflow
    }
}

namespace {
void drawCentred(MatrixPanel_I2S_DMA* m, const char* msg, uint16_t color) {
    print(m, (m->width() - width(msg, 1)) / 2, (m->height() - 7) / 2, msg, 1, color);
}
}  // namespace

void drawNoData(MatrixPanel_I2S_DMA* m) {
    if (!m) return;
    const Lang l = I18n::getLang();
    const char* msg = (l == Lang::FR) ? "PAS DE DONNEES" : (l == Lang::ES) ? "SIN DATOS" : "NO DATA";
    drawCentred(m, msg, m->color565(0x78, 0x78, 0x78));
}

void drawNotice(MatrixPanel_I2S_DMA* m, Message msg) {
    if (!m) return;
    const Lang l = I18n::getLang();
    if (msg == Message::Connecting) {
        const char* t = (l == Lang::FR) ? "CONNEXION" : (l == Lang::ES) ? "CONECTANDO" : "CONNECTING";
        drawCentred(m, t, m->color565(0x78, 0x78, 0x78));
    } else if (msg == Message::NoConnection) {
        const char* t = (l == Lang::FR) ? "PAS DE CONNEXION" : (l == Lang::ES) ? "SIN CONEXION" : "NO CONNECTION";
        drawCentred(m, t, m->color565(0xB4, 0x50, 0x50));
    } else {
        const char* t = (l == Lang::FR) ? "NON PRIS EN CHARGE" : (l == Lang::ES) ? "NO COMPATIBLE" : "UNSUPPORTED";
        drawCentred(m, t, m->color565(0xC0, 0x80, 0x00));
    }
}

}  // namespace panel_text
