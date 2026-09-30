#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

/**
 * @enum RotationEffect
 * @brief Visual transition animation styles when screen orientation changes.
 */
enum class RotationEffect : uint8_t {
    NONE = 0,
    PARTICLE_VORTEX = 1,
    CYBER_GLITCH = 2,
    SMOOTH_SLIDE = 3,
    TUNNEL_ZOOM = 4,
    MATRIX_RAIN = 5,
    RANDOM = 6,
    WIPE = 7,          ///< a bar sweeps across and takes the picture with it
    CURTAIN = 8,       ///< closes from both sides, then opens again
    DISSOLVE = 9,      ///< the picture breaks up into pixels
    CHECKER = 10,      ///< squares fill in, then clear
    SHUTTER = 11       ///< horizontal slats close and open
};

/**
 * @struct Particle
 * @brief 2D particle simulation node for vortex and explosion effects.
 */
struct Particle {
    float x, y;
    float vx, vy;
    float angle;
    float speed;
    float dist;
    uint16_t color;
    uint8_t life;
    uint8_t maxLife;
};

/**
 * @class RotationTransitionFX
 * @brief High-performance non-blocking visual transition engine for matrix orientation changes.
 */
class RotationTransitionFX {
public:
    RotationTransitionFX();
    ~RotationTransitionFX() = default;

    /**
     * @brief Starts a transition animation between old and target rotation.
     * @param fromRot Starting rotation index (0..3)
     * @param toRot Target rotation index (0..3)
     * @param effect Transition effect type
     * @param durationMs Total animation duration in milliseconds (e.g. 400ms)
     */
    void start(uint8_t fromRot, uint8_t toRot, RotationEffect effect, uint32_t durationMs = 400, int16_t w = 64, int16_t h = 32);

    /**
     * @brief Updates and renders the active transition animation.
     * @param display Pointer to Adafruit_GFX / Matrix display
     * @param onApexReached Callback invoked precisely at midpoint (t=0.5) to apply hardware rotation
     * @return true if transition is still running, false if complete
     */
    bool render(Adafruit_GFX* display, void (*onApexReached)(uint8_t targetRot));

    /**
     * @brief Checks if a transition animation is currently in progress.
     */
    bool isRunning() const { return _active; }

    /**
     * @brief Freeze the animation at its covered midpoint.
     *
     * The incoming engine does not always have a frame ready when the effect would finish: a GIF is
     * still reading its file, a weather screen has not repainted yet. Holding keeps the panel
     * covered instead of revealing black, and the reveal plays as soon as the hold is released.
     */
    void setHold(bool hold) { _holding = hold; }
    bool isHolding() const { return _holding && _active; }

    /**
     * @brief Forces active transition to stop immediately.
     */
    void stop();

    /**
     * @brief Helper to convert string to RotationEffect enum.
     */
    static RotationEffect parseEffect(const String& name);

    /**
     * @brief Helper to convert RotationEffect enum to string.
     */
    static String effectToString(RotationEffect effect);

private:
    bool _active;
    bool _apexApplied;
    bool _holding = false;
    uint8_t _fromRot;
    uint8_t _toRot;
    RotationEffect _configuredEffect;
    RotationEffect _activeEffect;
    uint32_t _durationMs;
    uint32_t _startTime;

    static const size_t MAX_PARTICLES = 48;
    Particle _particles[MAX_PARTICLES];

    void initParticles(int16_t w, int16_t h, bool clockwise);
    void renderVortex(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderGlitch(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderSlide(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderZoom(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderMatrixRain(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderWipe(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderCurtain(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderDissolve(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderChecker(Adafruit_GFX* display, float progress, int16_t w, int16_t h);
    void renderShutter(Adafruit_GFX* display, float progress, int16_t w, int16_t h);

    uint16_t getRandomArcadeColor();
};
