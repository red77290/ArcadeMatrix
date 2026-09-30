#ifndef WORLDMAPCLOCK_H
#define WORLDMAPCLOCK_H

#include "../ClockEngine.h"

/**
 * World clock face, adapted from the 64x64 "Clockwise" clockface cw-cf-0x03.
 *
 * The map scrolls by the hour so the meridian under the marker line is the one where it is noon,
 * exactly as in the original. The original could only show 64 px of a 120 px map; on a wide panel
 * the map simply continues to both edges and wraps, so the sides show the rest of the world instead
 * of being cropped away. The marker and the readout stay where they are in the middle square.
 */
class WorldMapClock : public ClockFace {
public:
    WorldMapClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
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
};

#endif
