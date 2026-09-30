#ifndef POKEDEXCLOCK_H
#define POKEDEXCLOCK_H

#include "../ClockEngine.h"

/**
 * Handheld-index face, adapted from the 64x64 "Clockwise" clockface cw-cf-0x06.
 *
 * The device sits in the middle of the panel at its own size with its edge columns carried out to
 * the sides. Inside that square everything behaves as in the original: the hour and minutes in the
 * original font, a weekday marker, the seconds bar filling across the minute, the blinking lamp,
 * and a creature that changes every minute.
 */
class PokedexClock : public ClockFace {
public:
    PokedexClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;
    bool wantsClear() const override { return false; }
    bool hasNewFrame() const override { return m_hasFrame; }
    void onActivated() override { m_dirty = 2; m_hasFrame = true; }

private:
    TimeData storedTime;
    int lastMinute = -1;
    int lastSecond = -1;
    uint8_t spriteIndex = 0;
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;

    void drawFace(int w, int h);
};

#endif
