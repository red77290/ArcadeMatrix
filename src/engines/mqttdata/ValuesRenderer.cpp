#include "ValuesRenderer.h"
#include <string.h>
#include "PanelText.h"

// Stacked tiles, drawn with the same pixels as the RPi firmware.
namespace values_renderer {

namespace {

using panel_text::TINY;

// Stacked tiles (label above, value + unit under it, centred per cell), the same as the RPi
// firmware. Value font chains, largest first, per tile count.
struct Chain { uint8_t n; int8_t fonts[4]; };
const Chain kChainsSmall[4] = {{3, {3, 2, 1}}, {3, {3, 2, 1}}, {2, {2, 1}}, {1, {1}}};       // H < 64
const Chain kChainsLarge[4] = {{4, {5, 4, 3, 2}}, {3, {4, 3, 2}}, {3, {3, 2, 1}}, {3, {3, 2, 1}}};   // H >= 64

int unitFont(int valueFont) { return valueFont >= 2 ? 1 : TINY; }

// One column more than the value's scale, so "17.0h" does not read as one word.
int unitGap(int valueFont) { return valueFont + 1; }

int lineWidth(const char* value, const char* unit, int font, bool withUnit) {
    int w = panel_text::width(value, font);
    if (withUnit && unit[0]) w += unitGap(font) + panel_text::width(unit, unitFont(font));
    return w;
}

struct LabelChoice {
    bool present;
    int font;
    char text[sizeof(feed::Tile::label)];
};

// Full label at the intended font, then the short one, then (256x64) both in the tiny font, and only
// then the shortest text cut from the end. Values are never cut.
LabelChoice chooseLabel(const feed::Tile& t, bool large, int iw) {
    LabelChoice c{};
    c.present = t.label[0] || t.labelShort[0];
    if (!c.present) return c;
    const int intended = large ? 1 : TINY;
    const char* full = t.label[0] ? t.label : t.labelShort;
    const char* shortText = t.labelShort[0] ? t.labelShort : nullptr;
    const int fonts[2] = {intended, TINY};
    const int nFonts = large ? 2 : 1;
    for (int f = 0; f < nFonts; f++) {
        if (panel_text::width(full, fonts[f]) <= iw) { c.font = fonts[f]; strlcpy(c.text, full, sizeof(c.text)); return c; }
        if (shortText && panel_text::width(shortText, fonts[f]) <= iw) { c.font = fonts[f]; strlcpy(c.text, shortText, sizeof(c.text)); return c; }
    }
    c.font = TINY;
    strlcpy(c.text, shortText ? shortText : full, sizeof(c.text));
    size_t len = strlen(c.text);
    while (len > 0 && panel_text::width(c.text, c.font) > iw) c.text[--len] = '\0';
    return c;
}

struct ValueChoice { int font; bool unit; };

ValueChoice chooseValue(const feed::Tile& t, const Chain& chain, int iw, int vb) {
    for (int i = 0; i < chain.n; i++) {
        int f = chain.fonts[i];
        if (7 * f <= vb && lineWidth(t.value, t.unit, f, true) <= iw) return {f, t.unit[0] != '\0'};
    }
    for (int i = 0; i < chain.n; i++) {
        int f = chain.fonts[i];
        if (7 * f <= vb && panel_text::width(t.value, f) <= iw) return {f, false};
    }
    return {chain.fonts[chain.n - 1], false};   // drawn in full, centred, clipped only by the panel
}

void drawTile(MatrixPanel_I2S_DMA* matrix, const feed::Tile& t, int ix, int iw, int cellTop, int ch, bool large, const Chain& chain) {
    const int lg = large ? 2 : 1;
    const LabelChoice label = chooseLabel(t, large, iw);
    const int labelH = label.present ? panel_text::height(label.font) : 0;
    const int vb = ch - (label.present ? labelH + lg : 0);
    const ValueChoice v = chooseValue(t, chain, iw, vb);
    const int vh = 7 * v.font;
    const int blockH = (label.present ? labelH + lg : 0) + vh;
    const int top = cellTop + (ch - blockH) / 2;
    const uint16_t labelColor = matrix->color565(t.labelColor.r, t.labelColor.g, t.labelColor.b);

    if (label.present) {
        const int lw = panel_text::width(label.text, label.font);
        panel_text::print(matrix, ix + (iw - lw) / 2, top, label.text, label.font, labelColor);
    }
    const int vy = top + (label.present ? labelH + lg : 0);
    const int lineW = lineWidth(t.value, t.unit, v.font, v.unit);
    const int vx = ix + (iw - lineW) / 2;
    panel_text::print(matrix, vx, vy, t.value, v.font, matrix->color565(t.color.r, t.color.g, t.color.b));
    if (v.unit) {
        const int uf = unitFont(v.font);
        const int ux = vx + panel_text::width(t.value, v.font) + unitGap(v.font);
        panel_text::print(matrix, ux, vy + vh - panel_text::height(uf), t.unit, uf, labelColor);
    }
}

}  // namespace

void drawValues(MatrixPanel_I2S_DMA* matrix, const feed::ValuesData& v, bool showTitle) {
    const int W = matrix->width(), H = matrix->height();
    const bool large = H >= 64;
    const int n = v.count > feed::MAX_TILES ? feed::MAX_TILES : v.count;
    if (n == 0) return;
    const Chain& chain = (large ? kChainsLarge : kChainsSmall)[n - 1];

    if (n == 4) {
        // 2x2 grid, no title; inner boxes 2 px off each inner edge (a 4 px gutter).
        const int half = W / 2, chh = H / 2;
        for (int i = 0; i < 4; i++) {
            const int c = i % 2, r = i / 2;
            const int x = c * half;
            const int ix = x + (c == 1 ? 2 : 0);
            const int cellEnd = x + half - (c == 0 ? 2 : 0);
            drawTile(matrix, v.tiles[i], ix, cellEnd - ix, r * chh, chh, large, chain);
        }
        return;
    }

    const bool title = showTitle && v.title[0] != '\0';
    const int ct = title ? (large ? 9 : 8) : 0;
    if (title) panel_text::print(matrix, 0, 0, v.title, 1, matrix->color565(255, 255, 255), W);
    for (int i = 0; i < n; i++) {
        const int cellX = i * W / n;
        const int cellEnd = (i + 1) * W / n - (i < n - 1 ? 2 : 0);
        const int ix = cellX + (i > 0 ? 2 : 0);
        drawTile(matrix, v.tiles[i], ix, cellEnd - ix, ct, H - ct, large, chain);
    }
}

}  // namespace values_renderer
