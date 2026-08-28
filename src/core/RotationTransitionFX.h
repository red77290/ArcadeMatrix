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
    RANDOM = 6
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
    void start(uint8_t fromRot, uint8_t toRot, RotationEffect effect, uint32_t durationMs = 400);

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

    uint16_t getRandomArcadeColor();
};
