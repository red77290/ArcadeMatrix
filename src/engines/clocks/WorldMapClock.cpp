#include "WorldMapClock.h"
#include "WorldMapAssets.h"
#include "SmallFont.h"
#include "ClockFaceFont.h"
#include "ClockwiseScene.h"
#include <stdio.h>
#include <time.h>

namespace {
constexpr int MAP_W = 120;
constexpr int MAP_H = 56;
constexpr int TZ_SIZE = 5;          ///< pixels per hour of longitude, as in the original
constexpr int HOME_COLUMN = 32;     ///< the marker's column inside the 64 px scene
constexpr int REFERENCE_OFFSET = -3;  ///< the source face pinned this longitude under the marker

/// Map column for a UTC offset, from the original's calibration of the marker column.
int columnForOffset(float offsetHours) {
    int c = HOME_COLUMN + (int)lroundf((offsetHours - REFERENCE_OFFSET) * TZ_SIZE);
    return ((c % MAP_W) + MAP_W) % MAP_W;
}

/// Hours the configured timezone is ahead of UTC, from the running clock rather than a constant.
float localUtcOffsetHours() {
    time_t now = time(nullptr);
    if (now < 1600000000) return 0.0f;
    struct tm lt, ut;
    localtime_r(&now, &lt);
    gmtime_r(&now, &ut);
    float diff = (lt.tm_hour - ut.tm_hour) + (lt.tm_min - ut.tm_min) / 60.0f;
    if (lt.tm_yday != ut.tm_yday) diff += (lt.tm_yday > ut.tm_yday || lt.tm_year > ut.tm_year) ? 24.0f : -24.0f;
    return diff;
}
constexpr uint16_t MAP_MASK = 0xF81F;
constexpr uint16_t MARKER = 0xF000;
}  // namespace

WorldMapClock::WorldMapClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 0, 0, 0 };
}

void WorldMapClock::draw(const TimeData& t) {
    storedTime = t;
}

void WorldMapClock::update() {
    if (!matrix) return;
    const int w = matrix->width();
    const int h = matrix->height();

    if (lastMinute != storedTime.minutes) {   // the terminator moves, so a repaint each minute
        lastMinute = storedTime.minutes;
        m_dirty = 2;
    }
    if (m_dirty == 0) { m_hasFrame = false; return; }
    m_dirty--;
    m_hasFrame = true;

    using namespace ClockwiseScene;
    const int d = divisor(h);
    const int ox = originX(w, h);
    const int oy = originY(h);

    // The marker stands for the configured timezone, so the map is anchored to put that longitude
    // under it rather than the fixed one the source face used.
    const float utcOffset = localUtcOffsetHours();
    int offset = columnForOffset(utcOffset) - HOME_COLUMN;
    offset = ((offset % MAP_W) + MAP_W) % MAP_W;

    // Where the sun is: the longitude whose local time is noon. Everything more than six hours from
    // it is night, and drawn dimmed, which is what gives the map its sense of the time of day now
    // that it no longer scrolls by the hour.
    time_t nowT = time(nullptr);
    struct tm utc;
    float utcHours = 0.0f;
    if (nowT > 1600000000) {
        gmtime_r(&nowT, &utc);
        utcHours = utc.tm_hour + utc.tm_min / 60.0f;
    }
    const int sunCol = columnForOffset(12.0f - utcHours);

    matrix->fillRect(0, 0, w, h, 0x0000);

    // The map wraps, so it can run the full width: the middle 64 px is what the original showed.
    for (int py = 0; py < MAP_H / d; py++) {
        int sy = py * d;
        for (int px = 0; px < w; px++) {
            int sceneX = (px - ox) * d;
            int col = ((offset + sceneX) % MAP_W + MAP_W) % MAP_W;
            uint16_t c = _WORLD_MAP[sy * MAP_W + col];
            if (c == MAP_MASK) continue;
            int away = abs(col - sunCol);
            if (away > MAP_W / 2) away = MAP_W - away;
            if (away > 6 * TZ_SIZE) {          // night side: same picture, dimmed
                uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
                c = (uint16_t)((r / 3) << 11) | (uint16_t)((g / 3) << 5) | (uint16_t)(b / 3);
            }
            matrix->drawPixel(px, oy + py, c);
        }
    }

    matrix->drawFastVLine(mapX(HOME_COLUMN, w, h), oy, 64 / d, MARKER);

    char buf[12];
    snprintf(buf, sizeof(buf), "%d:%02d", storedTime.hours, storedTime.minutes);
    matrix->setFont(&small4pt7b);
    matrix->setTextSize(1);
    matrix->setTextColor(0xFFFF);      // a 4 pt readout has no room for an outline
    matrix->setCursor(mapX(1, w, h), mapY(62, h));
    matrix->print(buf);
    matrix->setFont(nullptr);
}

void WorldMapClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    lastMinute = -1;
}
