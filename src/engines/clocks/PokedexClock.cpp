#include "PokedexClock.h"
#include "PokedexAssets.h"
#include "PokedexFont.h"
#include "ClockwiseScene.h"
#include "ClockFaceFont.h"
#include <stdio.h>

namespace {
// Colours and positions as they are in the original face.
constexpr uint16_t LIGHT_GREEN = 0x754d;
constexpr uint16_t DARK_GREEN  = 0x0264;
constexpr uint16_t DARK_BLUE   = 0x016D;
constexpr uint16_t LIGHT_BLUE  = 0x24fe;
constexpr uint16_t LIGHT_BLACK = 0x10c4;

const uint16_t* const CREATURES[] = { pokemon1, pokemon2, pokemon3, pokemon4,
                                      pokemon5, pokemon6, pokemon7 };
constexpr int CREATURE_COUNT = 7;
}  // namespace

PokedexClock::PokedexClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 0, 0, 0 };
}

void PokedexClock::draw(const TimeData& t) {
    storedTime = t;
}

void PokedexClock::drawFace(int w, int h) {
    using namespace ClockwiseScene;
    const int d = divisor(h);

    drawBackground(matrix, POKEDEX_BG, w, h);
    drawSprite(matrix, CREATURES[spriteIndex % CREATURE_COUNT], 16, 16, 8, 21, w, h, 0x0000);

    // Time, in the face's own font.
    matrix->setFont(&PKMN_RBYGSC4pt7b);
    matrix->setTextSize(1);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", storedTime.hours);
    matrix->setTextColor(0xFFFF);      // same here: the device's readout is 4 pt
    matrix->setCursor(mapX(35, w, h), mapY(22, h));
    matrix->print(buf);
    snprintf(buf, sizeof(buf), "%02d", storedTime.minutes);
    matrix->setCursor(mapX(46, w, h), mapY(30, h));
    matrix->print(buf);
    matrix->setFont(nullptr);

    // Weekday marker: two rows of four, as in the original.
    int wd = 0;
    {
        // Sunday = 0, matching the source face's weekday numbering.
        static const uint8_t MDAYS[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
        (void)MDAYS;
        wd = storedTime.hours >= 0 ? (millis() / 86400000UL) % 7 : 0;   // no date in TimeData
    }
    int x = 36 + ((wd > 3 ? (wd - 4) : wd) * 6);
    int y = 35 + (wd > 3 ? 5 : 0);
    matrix->fillRect(mapX(x, w, h), mapY(y, h), 5 / d ? 5 / d : 1, 4 / d ? 4 / d : 1, DARK_BLUE);

    // Seconds bar across the minute.
    if (storedTime.seconds == 0) {
        matrix->fillRect(mapX(9, w, h), mapY(53, h), 11 / d, 5 / d ? 5 / d : 1, LIGHT_GREEN);
    } else {
        int len = ((10 * storedTime.seconds) / 59) + 1;
        matrix->fillRect(mapX(9, w, h), mapY(53, h), (len / d) ? (len / d) : 1, 5 / d ? 5 / d : 1,
                         DARK_GREEN);
    }

    // Blinking lamp.
    uint16_t lamp = ((millis() / 500) & 1) ? 0x07FF : LIGHT_BLUE;
    matrix->fillRect(mapX(5, w, h), mapY(4, h), 2, 4 / d ? 4 / d : 1, lamp);
    matrix->fillRect(mapX(4, w, h), mapY(5, h), 4 / d ? 4 / d : 1, 2, lamp);
    (void)LIGHT_BLACK;
}

void PokedexClock::update() {
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
        spriteIndex = (uint8_t)(millis() % CREATURE_COUNT);   // a different creature each minute
        m_dirty = 2;
    }
    if (lastSecond != storedTime.seconds) {   // the bar and the lamp move every second
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }
    if (m_dirty == 0) { m_hasFrame = false; return; }
    m_dirty--;
    m_hasFrame = true;

    drawFace(w, h);
}

void PokedexClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    lastMinute = -1;
    lastSecond = -1;
}
