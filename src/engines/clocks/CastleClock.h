#ifndef CASTLECLOCK_H
#define CASTLECLOCK_H

#include "../ClockEngine.h"
#include <stdint.h>

/**
 * Animated Castlevania Clock Face (Simon Belmont & Dracula's Castle).
 *
 * Visuals:
 * - 128x32: Authentic NES Konami Simon Belmont on brick dungeon floor,
 *   animated candle torch, flapping vampire bat, and crimson gothic digits.
 * - 256x64: Dracula's castle parapets under a full blood moon, Simon Belmont
 *   walking/breathing idle cycle, whip attack on minute change, flickering flame,
 *   flapping bat across the moon, and large crimson digital clock.
 *
 * Performance:
 * - Zero dynamic allocation on Core 1 hot path.
 * - Double-buffer dirty tracking with sleep optimization (m_dirty = 2).
 * - PROGMEM RGB565 sprites (zero DRAM consumption).
 */
class CastleClock : public ClockFace {
public:
    CastleClock(IDrawingSurface* display, const EngineConfig* config = nullptr);

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
        phase = Phase::Idle;
        phaseTimer = 0.0f;
        walkTimer = 0.0f;
        walkFrame = 0;
        flameTimer = 0.0f;
        flameFrame = 0;
        batTimer = 0.0f;
        batFrame = 0;
    }

private:
    enum class Phase : uint8_t {
        Idle,
        WhipWindup,
        WhipStrike,
        Impact,
        Cooldown
    };

    TimeData storedTime;
    int lastMinute = -1;
    int lastSecond = -1;
    uint32_t lastFrameMs = 0;

    Phase phase = Phase::Idle;
    float phaseTimer = 0.0f;

    // Animation timers
    float walkTimer = 0.0f;
    uint8_t walkFrame = 0;
    float flameTimer = 0.0f;
    uint8_t flameFrame = 0;
    float batTimer = 0.0f;
    uint8_t batFrame = 0;
    float batX = 140.0f;

    // Display state
    char shownHH[4] = "00";
    char shownMM[4] = "00";
    bool m_snapToNow = true;
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;

    void blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent = true, bool flipH = false);
    void drawGothicDigit(int x, int y, char c, uint16_t color, int scale);
    void drawGothicTime(int startX, int startY, const char* str, uint16_t color, int scale);
    void drawScene(int w, int h);
};

#endif // CASTLECLOCK_H
