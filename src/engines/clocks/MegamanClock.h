#ifndef MEGAMANCLOCK_H
#define MEGAMANCLOCK_H

#include "../ClockEngine.h"
#include <stdint.h>

/**
 * Animated NES Mega Man 2 Clock Face.
 *
 * Visuals:
 * - Mega Man idle with procedural eyelid blink cycle (eyes shut ~150ms every 3.5s).
 * - Vertical NES E-Tank energy gauge tracking seconds (0..59s) on 64p panels,
 *   or compact horizontal energy bar on 32p panels.
 * - City skyline background with lit windows and metallic Wily laboratory ground tiles.
 * - Sleeping/peeking Metool enemy on wide panels.
 * - Minute transition: Mega Man raises Buster cannon, fires a glowing plasma Lemon bullet
 *   across the screen that impacts the minute pod, producing a 12x12 hit spark, pod recoil
 *   bounce, and digit flip.
 * - Hour transition: Mega Man fires a heavy Charge Shot that updates both pods.
 *
 * Full Pixel-Perfect Compatibility:
 * - 256x64 & 128x64: Full wide skyline, central pods, horizontal lemon trajectory.
 * - 64x64: Square arena, duel-range Buster blast.
 * - 128x32: Pixel-perfect close-up arena (Mega Man on left, pods on right, zero sprite squishing).
 */
class MegamanClock : public ClockFace {
public:
    MegamanClock(IDrawingSurface* display, const EngineConfig* config = nullptr);

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
        lastMinute = -1;
        phase = Phase::Idle;
        phaseTimer = 0.0f;
        runTimer = 0.0f;
        runFrame = 0;
        megamanX = -50.0f;
        bulletActive = false;
        sparkActive = false;
        podBounce = 0.0f;
    }

private:
    enum class Phase : uint8_t {
        Idle,
        HeroEnter,
        HeroJump,
        HeroExit
    };

    TimeData storedTime;
    int lastMinute = -1;
    int lastSecond = -1;
    uint32_t lastFrameMs = 0;
    Phase phase = Phase::Idle;
    float phaseTimer = 0.0f;
    float runTimer = 0.0f;
    int runFrame = 0;
    float megamanX = -50.0f;
    float megamanY = 0.0f;

    // Bullet & hit spark physics
    bool bulletActive = false;
    float bulletX = 0.0f;
    float bulletY = 0.0f;
    float bulletTargetX = 0.0f;
    float bulletTargetY = 0.0f;
    bool sparkActive = false;
    float sparkTimer = 0.0f;
    float podBounce = 0.0f;
    bool isChargeShot = false;

    // Display state
    char shownHH[4] = "00";
    char shownMM[4] = "00";
    bool m_snapToNow = true;
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;

    void blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent = true);
    void drawScene(int w, int h);
    void drawCapcomDigit(int x, int y, char c, uint16_t color);
    void drawTimePod(int x, int y, const char* text, int bounce = 0, bool highlight = false);
    void drawEnergyGauge(int x, int y, int seconds);
    void drawMiniEnergyGauge(int x, int y, int seconds);
};

#endif // MEGAMANCLOCK_H
