#ifndef CASTLECLOCK_H
#define CASTLECLOCK_H

#include "../ClockEngine.h"

/**
 * Clock tower face, adapted from the 64x64 "Clockwise" clockface cw-cf-0x04.
 *
 * The tower scene sits in the middle of the panel at its own size, with its edge columns carried
 * out to the sides, and the two hands sweep from the tower's clock face exactly as in the original:
 * the hour hand shorter, both redrawn each minute.
 */
class CastleClock : public ClockFace {
public:
    CastleClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;
    bool wantsClear() const override { return false; }
    bool hasNewFrame() const override { return m_hasFrame; }
    void onActivated() override { m_dirty = 2; m_hasFrame = true; }

private:
    TimeData storedTime;
    int lastMinute = -1;
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;

    void drawHand(float angle, int length, uint16_t color, int panelW, int panelH);
};

#endif
