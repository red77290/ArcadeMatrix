#ifndef SONICCLOCK_H
#define SONICCLOCK_H

#include "../ClockEngine.h"
#include <stdint.h>

/**
 * Animated Sonic the Hedgehog Clock Face.
 *
 * Visuals:
 * - 128x32: Authentic 8-bit Master System Sonic, Green Hill grass & checkered soil,
 *   palm tree, rotating golden ring, and arcade time digits.
 * - 256x64: Authentic 16-bit Megadrive textures (LuckyLuke repository), Green Hill
 *   mountains and ground, 28x39 idle Sonic with procedural foot tapping cycle (36x42),
 *   32x32 Item Monitor box, spinning golden ring (4 rotation frames), and Badnik Motobug patrol.
 *
 * Performance:
 * - Zero dynamic allocation on Core 1 hot path.
 * - Double-buffer dirty tracking with sleep optimization (m_dirty = 2).
 * - PROGMEM RGB565 sprites (zero DRAM consumption).
 */
class SonicClock : public ClockFace {
public:
    SonicClock(IDrawingSurface* display, const EngineConfig* config = nullptr);

    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

    bool wantsClear() const override { return false; }
    bool hasNewFrame() const override { return m_hasFrame; }
    void onActivated() override {
        m_dirty = 2;
        m_hasFrame = true;
        m_snapToNow = true;
        lastFrameMs = 0;
        ringFrame = 0;
        ringTimer = 0.0f;
        tapTimer = 0.0f;
        isTapping = false;
        motobugTimer = 0.0f;
    }

private:
    TimeData storedTime;
    int lastMinute = -1;
    int lastSecond = -1;
    uint32_t lastFrameMs = 0;

    // Animation timers
    uint8_t ringFrame = 0;
    float ringTimer = 0.0f;
    float tapTimer = 0.0f;
    bool isTapping = false;
    float motobugTimer = 0.0f;

    // Display state
    char shownHH[4] = "00";
    char shownMM[4] = "00";
    bool m_snapToNow = true;
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;

    void blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent = true, bool flipH = false);
    void drawArcadeDigit(int x, int y, char c, uint16_t color, int scale);
    void drawArcadeTime(int startX, int startY, const char* str, uint16_t color, int scale);
    void drawGreenHillGround(int w, int h, int gHeight);
    void drawScene(int w, int h);
};

#endif // SONICCLOCK_H
