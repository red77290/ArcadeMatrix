#include "GraphRenderer.h"
#include "../../core/drawing/IDrawingSurface.h"
#include <math.h>
#include "PanelText.h"

// Graph pages of the MQTT Data engine, drawn with the same pixels as the RPi firmware.
namespace graph_renderer {

static void drawHeader(IDrawingSurface* matrix, const graph::GraphData& g, const graph::Layout& layout) {
    auto color = [&](const graph::Rgb& c) { return matrix->color565(c.r, c.g, c.b); };
    const int size = layout.textSize;
    // Title in white, clipped one scale unit before the summary, which is right-aligned with its
    // last pixel on the last column in the first series' colour (title first: the clip blanks overflow).
    const int summaryW = panel_text::width(g.summary, size);
    const int summaryX = layout.title.w - summaryW;
    const int clip = summaryW ? summaryX - size : layout.title.w;
    // The full title when it fits, else the short one, else the shorter of the two clipped at the edge.
    const int room = clip - layout.title.x;
    const char* title = g.title;
    if (panel_text::width(g.title, size) > room) {
        if (g.titleShort[0] && panel_text::width(g.titleShort, size) <= room) title = g.titleShort;
        else if (g.titleShort[0] && strlen(g.titleShort) < strlen(g.title)) title = g.titleShort;
    }
    panel_text::print(matrix, layout.title.x, layout.title.y, title, size, matrix->color565(255, 255, 255), clip);
    if (summaryW) {
        uint16_t c = g.hasSummaryColor ? color(g.summaryColor)
                   : g.seriesCount ? color(g.series[0].color) : matrix->color565(255, 255, 255);
        panel_text::print(matrix, summaryX, layout.title.y, g.summary, size, c);
    }
}

void drawGraph(IDrawingSurface* matrix, const graph::GraphData& g, bool showHeader) {
    auto color = [&](const graph::Rgb& c) { return matrix->color565(c.r, c.g, c.b); };
    const graph::Layout layout = graph::calculateLayout(matrix->width(), matrix->height(), showHeader);
    if (layout.header) drawHeader(matrix, g, layout);

    int px = layout.plot.x, pw = layout.plot.w;
    // Legend (tall panels only): one tiny-font row at the bottom; the plot ends 7 rows higher
    // (256x64: plot rows 15..56, legend 58..62, row 63 blank).
    const bool legend = g.legendCount > 0 && matrix->height() >= 64;
    const int py = layout.plot.y;
    const int ph = layout.plot.h - (legend ? 7 : 0);
    const int legendY = py + ph + 1;
    if (pw <= 0 || ph <= 0 || g.slots == 0) return;
    const int base = py + ph - 1;
    const graph::Range range = graph::yRange(g);
    auto ly = [&](float v) { return base - graph::lineOffsetFor(v, range, ph); };

    // 0. Y-axis labels (tall panels only): range ends as integers in the tiny font, right-aligned
    // against the plot, which then starts one column after them.
    if (g.yaxis && matrix->height() >= 64) {
        char lmax[12], lmin[12];
        snprintf(lmax, sizeof(lmax), "%ld", lroundf(range.hi));
        snprintf(lmin, sizeof(lmin), "%ld", lroundf(range.lo));
        const int wmax = panel_text::width(lmax, panel_text::TINY);
        const int wmin = panel_text::width(lmin, panel_text::TINY);
        const int lw = wmax > wmin ? wmax : wmin;
        const uint16_t grey = matrix->color565(0x70, 0x70, 0x70);
        panel_text::print(matrix, lw - wmax, py, lmax, panel_text::TINY, grey);
        panel_text::print(matrix, lw - wmin, py + ph - 5, lmin, panel_text::TINY, grey);   // plot's bottom rows
        px = lw + 1;
        pw = matrix->width() - px;
        if (pw <= 0) return;
    }
    if (legend) {
        // Items from the plot's left edge, 8 px apart; the first that would pass the right edge is
        // dropped with every item after it (words are never cut).
        int lx = px;
        for (uint8_t i = 0; i < g.legendCount; i++) {
            const int w = panel_text::width(g.legend[i].text, panel_text::TINY);
            if (lx + w > matrix->width()) break;
            panel_text::print(matrix, lx, legendY, g.legend[i].text, panel_text::TINY, color(g.legend[i].color));
            lx += w + 8;
        }
    }

    // 1. Bands: full-height background over their slot range.
    for (uint8_t i = 0; i < g.bandCount; i++) {
        int x0 = graph::columnForSlot(g.bands[i].from, pw, g.slots);
        int x1 = graph::columnForSlot(g.bands[i].to, pw, g.slots);
        if (x1 > x0) matrix->fillRect(px + x0, py, x1 - x0, ph, color(g.bands[i].color));
    }
    // 2. Zero line, when bars straddle it.
    if (graph::zeroLineVisible(g, range)) {
        matrix->drawFastHLine(px, ly(0.0f), pw, matrix->color565(0x30, 0x30, 0x30));
    }
    // 3. Mid-range gridline on the tall panels.
    if (layout.gridline) {
        matrix->drawFastHLine(px, ly((range.lo + range.hi) * 0.5f), pw, matrix->color565(0x20, 0x20, 0x20));
    }
    // 4. Marks: dotted vertical lines at the left edge of their slot.
    for (uint8_t i = 0; i < g.markCount; i++) {
        int x = px + graph::columnForSlot(g.marks[i].at, pw, g.slots);
        uint16_t c = color(g.marks[i].color);
        for (int y = py; y <= base; y += 2) matrix->drawPixel(x, y, c);
    }
    uint16_t seriesColor[graph::MAX_SERIES];
    for (uint8_t s = 0; s < g.seriesCount; s++) seriesColor[s] = color(g.series[s].color);

    // 5. Bars from the zero line: stacked (positive and negative stacks apart) or overlaid.
    for (int x = 0; x < pw; x++) {
        uint16_t slot = graph::slotForColumn(x, pw, g.slots);
        float pos = 0.0f, neg = 0.0f;
        for (uint8_t s = 0; s < g.seriesCount; s++) {
            if (g.series[s].line) continue;
            float v = graph::valueAt(g, s, slot);
            if (v == 0.0f) continue;
            float from = 0.0f;
            if (g.stack) {
                if (v > 0.0f) { from = pos; pos += v; } else { from = neg; neg += v; }
            }
            int h0 = graph::heightFor(from, range, ph);
            int h1 = graph::heightFor(from + v, range, ph);
            int lo = h0 < h1 ? h0 : h1, hi = h0 < h1 ? h1 : h0;
            if (hi > lo) matrix->drawFastVLine(px + x, base - hi + 1, hi - lo, seriesColor[s]);
        }
    }
    // 6. Lines: 1 px, in series order (later on top), vertical runs between columns (step look),
    // no pixel and no join at a gap.
    for (uint8_t s = 0; s < g.seriesCount; s++) {
        if (!g.series[s].line) continue;
        int prevRow = -1;
        for (int x = 0; x < pw; x++) {
            float v = graph::pointAt(g, s, graph::slotForColumn(x, pw, g.slots));
            if (isnan(v)) { prevRow = -1; continue; }
            int row = ly(v);
            if (prevRow < 0 || prevRow == row) {
                matrix->drawPixel(px + x, row, seriesColor[s]);
            } else {
                int y0 = prevRow < row ? prevRow : row, y1 = prevRow < row ? row : prevRow;
                matrix->drawFastVLine(px + x, y0, y1 - y0 + 1, seriesColor[s]);
            }
            prevRow = row;
        }
    }
}

}  // namespace graph_renderer
