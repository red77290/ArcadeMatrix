#ifndef STREETFIGHTERCLOCK_H
#define STREETFIGHTERCLOCK_H

#include "../ClockEngine.h"
#include <stdint.h>

/**
 * Animated Street Fighter Clock Face.
 *
 * Visuals:
 * - 128x32: Authentic 8-bit Ryu & Ken from Street Fighter X Mega Man (SFxMM),
 *   tatami dojo floor, arcade lifebars, central K.O. badge, Hadouken blast,
 *   punch & hit recoil frames, and golden arcade digits.
 * - 256x64: Suzaku Castle rooftop stage under crescent moon night sky,
 *   authentic 4-frame idle breathing cycle for Ryu and Ken (phase-offset),
 *   Ryu Hadouken windup & thrust attack pose, 12x10 Hadouken plasma projectile,
 *   Ken hit impact recoil, 12x12 hit spark, central K.O. flash, and arcade time digits.
 *
 * Performance:
 * - Zero dynamic allocation on Core 1 hot path.
 * - Double-buffer dirty tracking with sleep optimization (m_dirty = 2).
 * - Fast blitting with inline horizontal flip (zero memory copies).
 */
class StreetFighterClock : public ClockFace {
public:
    StreetFighterClock(IDrawingSurface* display, const EngineConfig* config = nullptr);

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
        breathTimer = 0.0f;
        breathFrame = 0;
    }

private:
    enum class Phase : uint8_t {
        Idle,
        HadoukenWindup,
        HadoukenFlying,
        HadoukenImpact,
        Cooldown
    };

    TimeData storedTime;
    int lastMinute = -1;
    int lastSecond = -1;
    uint32_t lastFrameMs = 0;

    Phase phase = Phase::Idle;
    float phaseTimer = 0.0f;
    float breathTimer = 0.0f;
    uint8_t breathFrame = 0;

    // Hadouken projectile state
    float hadoukenX = 0.0f;
    float hadoukenY = 0.0f;
    float hadoukenTargetX = 0.0f;
    float koFlashTimer = 0.0f;

    // Display state
    char shownHH[4] = "00";
    char shownMM[4] = "00";
    bool m_snapToNow = true;
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;

    void blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent = true, bool flipH = false);
    void drawArcadeDigit(int x, int y, char c, uint16_t color, int scale);
    void drawArcadeTime(int startX, int startY, const char* str, uint16_t color, int scale);
    void drawScene(int w, int h);
    void drawHealthBars(int w, int h);
};

#endif // STREETFIGHTERCLOCK_H
