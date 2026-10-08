#ifndef METALSLUGCLOCK_H
#define METALSLUGCLOCK_H

#if !defined(HARDWARE_PROFILE_ESP32_DEV)

#include "../ClockEngine.h"
#include "MetalSlugAssets.h"
#include <stdint.h>

/**
 * Animated Metal Slug Clock Face (Marco Rossi vs Rebel Army Di-Cokka Tank).
 *
 * Visuals:
 * - Dedicated EXCLUSIVELY to 256x64 widescreen display geometry.
 * - Authentic SNK Neo Geo 16-bit arcade pixel art from Metal Slug.
 * - Mission 2 Arabian Desert / Warzone 256x64 Night Backdrop.
 * - Dynamic 60-second battle sequence:
 *   * Marco Rossi walking/patrolling with rifle
 *   * Heavy Machine Gun firefight with muzzle flashes and ejected shells
 *   * Rebel Di-Cokka tank advance and cannon fire
 *   * Flying Rebel Helicopter with spinning rotor blades
 *   * Parabolic stick-grenade toss on minute transition
 *   * Massive 4-stage screen-shaking fireball explosion
 *   * Tank collapses into charred smoking wreck
 *   * Marco Rossi thumbs-up victory celebration yell
 *   * Neo Geo arcade HUD (1UP score, glowing arcade clock, Heavy Machine Gun ammo)
 *
 * Performance:
 * - Zero dynamic allocation on Core 1 hot path.
 * - Double-buffer dirty tracking with sleep optimization (m_dirty = 2).
 * - PROGMEM RGB565 sprites (zero DRAM consumption).
 */
class MetalSlugClock : public ClockFace {
public:
    MetalSlugClock(IDrawingSurface* display, const EngineConfig* config = nullptr);

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
        animTimer = 0.0f;
        animFrame = 0;
        heliX = -60.0f;
        tankX = 185.0f;
        marcoX = 36.0f;
        tankBulletActive = false;
        grenadeActive = false;
        // Guaranteed different random backdrop on each rotation
        uint8_t nextIdx;
        do {
            nextIdx = (uint8_t)(esp_random() % MetalSlugAssets::NUM_BACKDROPS);
        } while (nextIdx == currentBackdropIdx && MetalSlugAssets::NUM_BACKDROPS > 1);
        currentBackdropIdx = nextIdx;
    }

private:
    enum class Phase : uint8_t {
        Idle,            // Ambient calm during normal seconds: Marco idles, Tank stations, Heli cruises
        Firefight,       // Minute reward 1: Marco opens fire with Heavy Machine Gun burst
        GrenadeAssault,  // Minute reward 2: Marco hurls stick grenade in high parabolic arc
        Explosion,       // Minute reward 3: Fireball blast destroys tank, minute flips!
        Victory          // Minute reward 4: Marco thumbs-up victory pose
    };

    TimeData storedTime;
    int lastMinute = -1;
    int lastSecond = -1;
    uint32_t lastFrameMs = 0;

    Phase phase = Phase::Idle;
    float phaseTimer = 0.0f;
    float animTimer = 0.0f;
    int animFrame = 0;

    // Entity positions
    float marcoX = 36.0f;
    float tankX = 185.0f;
    float heliX = -60.0f;

    // Projectiles
    bool tankBulletActive = false;
    float tankBulletX = 180.0f;

    bool grenadeActive = false;
    float grenadeX = 0.0f;
    float grenadeY = 0.0f;
    float grenadeVx = 0.0f;
    float grenadeVy = 0.0f;

    int explosionFrame = 0;
    float explosionTimer = 0.0f;

    char shownHH[4] = "00";
    char shownMM[4] = "00";
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;
    bool m_snapToNow = true;
    uint8_t currentBackdropIdx = 0;

    void blitSprite(const uint16_t* palette, const uint8_t* pixels, int w, int h, int x, int y, bool flipH = false);
    void blitBackdrop(const uint16_t* palette, const uint8_t* packedPixels, int w, int h);
    void drawArcadeDigit(int x, int y, char c, uint16_t color, int scale);
    void drawArcadeTime(int startX, int startY, const char* str, uint16_t color, int scale);
    void drawScene(int w, int h);
};

#endif // !HARDWARE_PROFILE_ESP32_DEV

#endif // METALSLUGCLOCK_H
