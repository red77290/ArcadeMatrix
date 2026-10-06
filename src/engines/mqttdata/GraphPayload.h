#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

/**
 * Graph payload, a page of the MQTT Data engine (docs/MQTT_DATA_CONTRACT.md):
 *
 *   {"type":"graph", "v":1, "title":"AC 24H", "show_header":true, "summary":"12.3kWh", "unit":"kW", "slots":96, "min":0, "max":6,
 *    "stack":true, "yaxis":false,
 *    "series":[{"label":"UP","style":"bar","color":"#2080FF","data":[0.0,1.25,null,...]}, ...],
 *    "bands":[{"from":60,"to":72,"color":"#502800"}], "marks":[{"at":28,"color":"#404040"}]}
 *
 * Everything here is plain data with fixed capacity (the points live in a pool the owner sizes for
 * the board), so a parsed graph can be copied between the cores without touching the heap. The
 * parser and the layout helpers have no Arduino dependency and are covered by the host native tests.
 */
namespace graph {

static constexpr uint8_t MAX_SERIES = 4;
static constexpr uint16_t MAX_POINTS = 288;   ///< upper bound; the real capacity is GraphData::pointCap
static constexpr uint8_t MAX_BANDS = 96;   ///< one per 15-minute slot of a 24 h graph
static constexpr uint8_t MAX_MARKS = 8;
static constexpr uint8_t MAX_LEGEND = 4;

/// UTF-8 "°" is stored as this single byte so text widths stay one cell per character; the draw
/// code renders it as a small ring (the built-in font has no reliable degree glyph).
static constexpr char DEGREE = (char)0xB0;

struct Rgb { uint8_t r, g, b; };

struct Series {
    char label[8];
    Rgb color;
    bool line;                 ///< style "line" (1 px, gaps at null); otherwise a bar series
    uint16_t count;            ///< points held in `data` (newest last)
    float* data;               ///< pointCap floats in the owner's pool; NaN = null (a gap for lines, 0 for bars)
};

struct Band { uint16_t from, to; Rgb color; };   ///< slot range [from, to)
struct Mark { uint16_t at; Rgb color; };
struct LegendItem { char text[16]; Rgb color; };   ///< drawn in the tiny font on tall panels

struct GraphData {
    bool valid;
    char title[24];
    char titleShort[24];       ///< optional abbreviation, used when the full title does not fit
    char summary[16];
    bool hasSummaryColor;      ///< `summary_color` given; otherwise the first series' colour
    Rgb summaryColor;
    char unit[8];
    uint16_t slots;            ///< x positions; data is right-aligned into them
    float min;                 ///< y-axis bottom when given (hasMin)
    float max;                 ///< y-axis top; absent or <= min = auto
    bool hasMin;
    bool stack;                ///< bar series only
    bool yaxis;                ///< min/max labels on panels >= 64 px tall
    bool showHeader;           ///< "show_header" (default true): title and summary line
    uint8_t seriesCount;
    uint8_t bandCount;
    uint8_t markCount;
    uint8_t legendCount;
    Series series[MAX_SERIES];
    Band bands[MAX_BANDS];
    Mark marks[MAX_MARKS];
    LegendItem legend[MAX_LEGEND];
    uint16_t pointCap;         ///< points kept per series (newest win); sized from the board's memory
    float* pool;               ///< MAX_SERIES * pointCap floats, owned by whoever allocated the graph
};

/// Points a graph at its pool (MAX_SERIES * pointCap floats) and empties it.
void attachPool(GraphData& g, float* pool, uint16_t pointCap);

/// Bytes a pool needs for `pointCap` points per series.
inline size_t poolBytes(uint16_t pointCap) { return (size_t)MAX_SERIES * pointCap * sizeof(float); }

/// Deep copy: everything plus the points, into `dst`'s own pool (which must hold src.pointCap).
void copyGraph(GraphData& dst, const GraphData& src);

/// Parses "#RRGGBB" (or "RRGGBB"); returns false and leaves `out` alone on anything else.
bool parseHexColor(const char* s, Rgb& out);

/**
 * Copies `src` into `dst` (capacity `cap`, always NUL-terminated), turning UTF-8 "°" into DEGREE
 * and any other non-ASCII sequence into '?', so one byte is one character cell on the panel.
 */
void copyPanelText(char* dst, size_t cap, const char* src);

/**
 * Parses a v1 payload into `out`. `json` must be writable and NUL-terminated (ArduinoJson parses
 * it in place, so no strings are copied into `doc`). `doc` is caller-owned scratch, sized for the
 * largest payload. Returns false - with out.valid = false - when the payload is not a graph (no
 * `series` array) or is not valid JSON.
 */
bool parsePayload(char* json, JsonDocument& doc, GraphData& out);

/// Same, from an already parsed document.
bool parseDocument(const JsonDocument& doc, GraphData& out);

/// Raw point of a series at slot `slot` (right-aligned); NaN for a gap, null or missing point.
float pointAt(const GraphData& g, uint8_t series, uint16_t slot);

/// Bar value of a series at `slot`: like pointAt but null/missing read as 0. Negatives kept.
float valueAt(const GraphData& g, uint8_t series, uint16_t slot);

bool hasBars(const GraphData& g);
bool hasLines(const GraphData& g);

struct Range { float lo, hi; };

/**
 * The y range. Explicit when `max` > `min`. Otherwise automatic: bars only = [min(0, lowest
 * negative stack), highest positive stack]; any line series = [lowest, highest] over every drawn
 * value, padded by 5% of the span (at least 0.5) on each side. A given `min` is kept as the bottom
 * of an automatic range. hi > lo always.
 */
Range yRange(const GraphData& g);

/// Height in pixels (0..height) of `value` above the bottom of a `height`-pixel plot, clamped.
int heightFor(float value, const Range& r, int height);

/// Row offset (0 = bottom row .. height-1 = top row) of a line point, clamped to the plot.
int lineOffsetFor(float value, const Range& r, int height);

/// Whether the 1 px zero line is drawn: 0 strictly inside the range and at least one bar series.
bool zeroLineVisible(const GraphData& g, const Range& r);

/// Slot drawn at column `x` of a graph `width` columns wide.
inline uint16_t slotForColumn(int x, int width, uint16_t slots) {
    if (width <= 0 || slots == 0) return 0;
    return (uint16_t)(((uint32_t)x * slots) / (uint32_t)width);
}

/// First column of slot `slot` (the left edge used for marks).
inline int columnForSlot(uint16_t slot, int width, uint16_t slots) {
    if (slots == 0) return 0;
    return (int)(((uint32_t)slot * (uint32_t)width + slots - 1) / slots);
}

struct Area { int16_t x, y, w, h; };
struct Layout {
    bool header;
    uint8_t textSize;          ///< built-in 5x7 font scale for the header
    Area title;                ///< header row (title left, summary right)
    Area plot;
    bool gridline;             ///< dim line at mid range (64-px-tall panels)
};

/**
 * 128x32: header rows 0..6, graph rows 8..31. 256x64: header at text size 2, graph rows 15..63
 * with a mid-range gridline. Other sizes scale by height; the header is dropped below 24 px.
 */
Layout calculateLayout(int width, int height, bool showHeader);

}  // namespace graph
