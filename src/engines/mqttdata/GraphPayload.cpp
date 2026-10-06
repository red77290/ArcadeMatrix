#include "GraphPayload.h"
#include <string.h>
#include <math.h>

namespace graph {

namespace {

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint16_t clampIndex(long v, uint16_t limit) {
    if (v < 0) return 0;
    if (v > (long)limit) return limit;
    return (uint16_t)v;
}

bool isGap(float v) { return isnan(v) || isinf(v); }

}  // namespace

bool parseHexColor(const char* s, Rgb& out) {
    if (!s) return false;
    if (*s == '#') s++;
    if (strlen(s) != 6) return false;
    int v[6];
    for (int i = 0; i < 6; i++) {
        v[i] = hexNibble(s[i]);
        if (v[i] < 0) return false;
    }
    out.r = (uint8_t)(v[0] * 16 + v[1]);
    out.g = (uint8_t)(v[2] * 16 + v[3]);
    out.b = (uint8_t)(v[4] * 16 + v[5]);
    return true;
}

void copyPanelText(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    size_t n = 0;
    const unsigned char* p = (const unsigned char*)(src ? src : "");
    while (*p && n + 1 < cap) {
        if (*p < 0x80) {
            dst[n++] = (char)*p++;
        } else if (p[0] == 0xC2 && p[1] == 0xB0) {
            dst[n++] = DEGREE;
            p += 2;
        } else {
            // Skip the whole multi-byte sequence and show one placeholder for it.
            int len = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
            for (int i = 0; i < len && *p; i++) p++;
            dst[n++] = '?';
        }
    }
    dst[n] = '\0';
}

void attachPool(GraphData& g, float* pool, uint16_t pointCap) {
    g.pool = pool;
    g.pointCap = pool ? pointCap : 0;
    for (uint8_t s = 0; s < MAX_SERIES; s++) {
        g.series[s].data = pool ? pool + (size_t)s * pointCap : nullptr;
        g.series[s].count = 0;
    }
    g.seriesCount = 0;
    g.valid = false;
}

void copyGraph(GraphData& dst, const GraphData& src) {
    float* pool = dst.pool;
    const uint16_t cap = dst.pointCap;
    memcpy(&dst, &src, sizeof(GraphData));
    dst.pool = pool;
    dst.pointCap = cap;
    for (uint8_t s = 0; s < MAX_SERIES; s++) {
        dst.series[s].data = pool ? pool + (size_t)s * cap : nullptr;
        uint16_t n = src.series[s].count < cap ? src.series[s].count : cap;
        dst.series[s].count = n;
        if (n && pool && src.series[s].data) memcpy(dst.series[s].data, src.series[s].data, n * sizeof(float));
    }
}

bool parsePayload(char* json, JsonDocument& doc, GraphData& out) {
    out.valid = false;
    doc.clear();
    if (!json || deserializeJson(doc, json) != DeserializationError::Ok) return false;
    return parseDocument(doc, out);
}

bool parseDocument(const JsonDocument& doc, GraphData& out) {
    out.valid = false;
    JsonArrayConst series = doc["series"].as<JsonArrayConst>();
    if (series.isNull() || !out.pool || out.pointCap == 0) return false;

    copyPanelText(out.title, sizeof(out.title), doc["title"] | "");
    copyPanelText(out.titleShort, sizeof(out.titleShort), doc["title_short"] | "");
    copyPanelText(out.summary, sizeof(out.summary), doc["summary"] | "");
    out.summaryColor = {0xFF, 0xFF, 0xFF};
    out.hasSummaryColor = parseHexColor(doc["summary_color"] | "", out.summaryColor);
    copyPanelText(out.unit, sizeof(out.unit), doc["unit"] | "");
    out.hasMin = doc["min"].is<float>();
    out.min = out.hasMin ? doc["min"].as<float>() : 0.0f;
    out.max = doc["max"].is<float>() ? doc["max"].as<float>() : 0.0f;
    if (isGap(out.min)) { out.min = 0.0f; out.hasMin = false; }
    if (isGap(out.max)) out.max = 0.0f;
    out.stack = doc["stack"] | true;
    out.yaxis = doc["yaxis"] | false;
    out.showHeader = doc["show_header"] | true;

    static const Rgb kDefaultColors[MAX_SERIES] = {
        {0x20, 0x80, 0xFF}, {0x00, 0xD0, 0xA0}, {0xFF, 0xA0, 0x20}, {0xE0, 0x40, 0xE0}};

    out.seriesCount = 0;
    uint16_t longest = 0;
    for (JsonObjectConst s : series) {
        if (out.seriesCount >= MAX_SERIES) break;
        Series& dst = out.series[out.seriesCount];
        copyPanelText(dst.label, sizeof(dst.label), s["label"] | "");
        dst.color = kDefaultColors[out.seriesCount];
        parseHexColor(s["color"] | "", dst.color);
        const char* style = s["style"] | "bar";
        dst.line = strcmp(style, "line") == 0;
        dst.count = 0;
        JsonArrayConst data = s["data"].as<JsonArrayConst>();
        // Keep the newest pointCap points when a series is longer than we can hold.
        const size_t cap = out.pointCap;
        size_t total = data.isNull() ? 0 : data.size();
        size_t skip = total > cap ? total - cap : 0;
        size_t i = 0;
        for (JsonVariantConst p : data) {
            if (i++ < skip) continue;
            float v = p.is<float>() ? p.as<float>() : NAN;
            if (isinf(v)) v = NAN;
            dst.data[dst.count++] = v;
        }
        if (dst.count > longest) longest = dst.count;
        out.seriesCount++;
    }

    long slots = doc["slots"] | 0L;
    if (slots <= 0) slots = longest;
    if (slots > out.pointCap) slots = out.pointCap;
    out.slots = (uint16_t)slots;

    // Past the cap the NEWEST bands win (highest `from`): on a right-aligned graph those are the
    // recent hours, which matter most. Once full, a newer band replaces the oldest one held.
    out.bandCount = 0;
    for (JsonObjectConst b : doc["bands"].as<JsonArrayConst>()) {
        Band band;
        band.from = clampIndex(b["from"] | 0L, out.slots);
        band.to = clampIndex(b["to"] | 0L, out.slots);
        if (band.to <= band.from) continue;
        band.color = {0x50, 0x28, 0x00};
        parseHexColor(b["color"] | "", band.color);
        if (out.bandCount < MAX_BANDS) {
            out.bands[out.bandCount++] = band;
            continue;
        }
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < out.bandCount; i++) {
            if (out.bands[i].from < out.bands[oldest].from) oldest = i;
        }
        if (band.from > out.bands[oldest].from) out.bands[oldest] = band;
    }
    // Drawn in ascending `from` order, so a later band wins where two overlap (insertion sort, <= 96).
    for (uint8_t i = 1; i < out.bandCount; i++) {
        Band b = out.bands[i];
        int j = i - 1;
        while (j >= 0 && out.bands[j].from > b.from) { out.bands[j + 1] = out.bands[j]; j--; }
        out.bands[j + 1] = b;
    }

    out.markCount = 0;
    for (JsonObjectConst m : doc["marks"].as<JsonArrayConst>()) {
        if (out.markCount >= MAX_MARKS) break;
        long at = m["at"] | -1L;
        if (at < 0 || at >= (long)out.slots) continue;
        Mark& dst = out.marks[out.markCount];
        dst.at = (uint16_t)at;
        dst.color = {0x40, 0x40, 0x40};
        parseHexColor(m["color"] | "", dst.color);
        out.markCount++;
    }

    out.legendCount = 0;
    for (JsonObjectConst l : doc["legend"].as<JsonArrayConst>()) {
        if (out.legendCount >= MAX_LEGEND) break;
        LegendItem& dst = out.legend[out.legendCount];
        copyPanelText(dst.text, sizeof(dst.text), l["text"] | "");
        if (!dst.text[0] || !parseHexColor(l["color"] | "", dst.color)) continue;   // empty or bad colour: skip
        out.legendCount++;
    }

    out.valid = true;
    return true;
}

float pointAt(const GraphData& g, uint8_t series, uint16_t slot) {
    if (series >= g.seriesCount || slot >= g.slots) return NAN;
    const Series& s = g.series[series];
    // Right alignment: the last point sits in the last slot; a shorter series leaves the left empty.
    int idx = (int)s.count - (int)g.slots + (int)slot;
    if (idx < 0 || idx >= (int)s.count) return NAN;
    return s.data[idx];
}

float valueAt(const GraphData& g, uint8_t series, uint16_t slot) {
    float v = pointAt(g, series, slot);
    return isGap(v) ? 0.0f : v;
}

bool hasBars(const GraphData& g) {
    for (uint8_t s = 0; s < g.seriesCount; s++) if (!g.series[s].line) return true;
    return false;
}

bool hasLines(const GraphData& g) {
    for (uint8_t s = 0; s < g.seriesCount; s++) if (g.series[s].line) return true;
    return false;
}

Range yRange(const GraphData& g) {
    if (g.max > g.min) return {g.min, g.max};

    const bool bars = hasBars(g);
    const bool lines = hasLines(g);
    bool any = false;
    float lo = 0.0f, hi = 0.0f;
    auto take = [&](float v) {
        if (!any) { lo = hi = v; any = true; return; }
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    };
    for (uint16_t slot = 0; slot < g.slots; slot++) {
        float pos = 0.0f, neg = 0.0f;
        for (uint8_t s = 0; s < g.seriesCount; s++) {
            if (g.series[s].line) {
                float v = pointAt(g, s, slot);
                if (!isGap(v)) take(v);
                continue;
            }
            float v = valueAt(g, s, slot);
            if (g.stack) {
                if (v >= 0.0f) pos += v; else neg += v;
            } else {
                take(v);
            }
        }
        if (bars && g.stack) { take(pos); take(neg); }
    }
    if (bars) take(0.0f);   // bars grow from the zero line, so it is always in range

    Range r;
    if (!any) {
        float lo0 = g.hasMin ? g.min : 0.0f;
        return {lo0, lo0 + 1.0f};
    } else if (lines) {
        float pad = (hi - lo) * 0.05f;
        if (pad < 0.5f) pad = 0.5f;
        r = {lo - pad, hi + pad};
    } else {
        r = {lo, hi};
    }
    if (g.hasMin) r.lo = g.min;
    if (r.hi - r.lo < 0.001f) r.hi = r.lo + 0.001f;
    return r;
}

int heightFor(float value, const Range& r, int height) {
    if (height <= 0 || isGap(value)) return 0;
    float span = r.hi - r.lo;
    if (span <= 0.0f) return 0;
    int h = (int)lroundf((value - r.lo) / span * (float)height);
    if (h < 0) h = 0;
    if (h > height) h = height;
    return h;
}

int lineOffsetFor(float value, const Range& r, int height) {
    if (height <= 1 || isGap(value)) return 0;
    float span = r.hi - r.lo;
    if (span <= 0.0f) return 0;
    int o = (int)lroundf((value - r.lo) / span * (float)(height - 1));
    if (o < 0) o = 0;
    if (o > height - 1) o = height - 1;
    return o;
}

bool zeroLineVisible(const GraphData& g, const Range& r) {
    return hasBars(g) && r.lo < 0.0f && r.hi > 0.0f;
}

Layout calculateLayout(int width, int height, bool showHeader) {
    Layout l{};
    l.header = showHeader && height >= 24;
    l.textSize = height >= 64 ? 2 : 1;
    l.gridline = height >= 64;
    if (!l.header) {
        l.title = {0, 0, 0, 0};
        l.plot = {0, 0, (int16_t)width, (int16_t)height};
        return l;
    }
    // The built-in font is 7 px tall per text size; one blank row separates it from the plot, so
    // the plot starts on row 8 at 128x32 and on row 15 at 256x64.
    int headerH = 7 * l.textSize;
    l.title = {0, 0, (int16_t)width, (int16_t)headerH};
    int plotY = headerH + 1;
    l.plot = {0, (int16_t)plotY, (int16_t)width, (int16_t)(height - plotY)};
    return l;
}

}  // namespace graph
