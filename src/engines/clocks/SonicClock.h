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
 *   mountains and ground, Item Monitor box, spinning golden ring (4 rotation frames),
 *   and Badnik Motobug patrol.
 * - Minute reward: Motobug and golden rings animate in idle. At minute rollover,
 *   Sonic enters, spin-dashes/leaps into the monitor box to flip the minute, and exits.
 *
 * Performance:
 * - Zero dynamic allocation on Core 1 hot path.
 * - Double-buffer dirty tracking with sleep optimization (m_dirty = 2).
 * - PROGMEM paletted sprites (saves Flash and zero DRAM consumption).
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
        runFrame = 0;
        runTimer = 0.0f;
        ballFrame = 0;
        ballTimer = 0.0f;
        phase = Phase::Idle;
        phaseTimer = 0.0f;
        sonicX = -40.0f;
    }

private:
    enum class Phase : uint8_t {
        Idle,
        HeroEnter,
        JumpStrike,
        Impact,
        HeroExit
    };

    TimeData storedTime;
    int lastMinute = -1;
    int lastSecond = -1;
    uint32_t lastFrameMs = 0;

    Phase phase = Phase::Idle;
    float phaseTimer = 0.0f;
    float sonicX = -40.0f;
    float sonicY = 0.0f;

    // Animation timers
    uint8_t ringFrame = 0;
    float ringTimer = 0.0f;
    float motobugTimer = 0.0f;
    uint8_t runFrame = 0;
    float runTimer = 0.0f;
    uint8_t ballFrame = 0;
    float ballTimer = 0.0f;

    // Display state
    char shownHH[4] = "00";
    char shownMM[4] = "00";
    bool m_snapToNow = true;
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;

    uint8_t climbStage = 0;
    float jumpT = 0.0f;

    void blitSprite(const uint16_t* palette, const uint8_t* pixels, int w, int h, int x, int y, bool flipH = false);
    void drawArcadeDigit(int x, int y, char c, uint16_t color, int scale);
    void drawArcadeTime(int startX, int startY, const char* str, uint16_t color, int scale);
    void drawGreenHillGround(int w, int h, int gHeight);
    void drawCheckeredPlatform(int x, int y, int w, int h);
    void drawScene(int w, int h);
};

#endif // SONICCLOCK_H
