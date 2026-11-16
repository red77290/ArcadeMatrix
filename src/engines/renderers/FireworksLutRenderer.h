#pragma once

#include <stdint.h>
#include "../../core/drawing/IDrawingSurface.h"

namespace renderers {

/**
 * @brief 16 isotropic polar direction vectors precomputed in Q8.8 fixed-point (radius = 256).
 * Occupies 64 bytes in Flash ROM; eliminates all floating-point sin/cos calls on Core 1.
 */
static constexpr int16_t FIREWORK_DIRS_Q8[16][2] = {
    { 256,   0}, { 237,  98}, { 181, 181}, {  98, 237},
    {   0, 256}, { -98, 237}, {-181, 181}, {-237,  98},
    {-256,   0}, {-237, -98}, {-181,-181}, { -98,-237},
    {   0,-256}, {  98,-237}, { 181,-181}, { 237, -98}
};

/**
 * @brief Elastic parabolic bounce offset table (in pixels) triggered at explosion impact (t = 0.6s).
 */
static constexpr int8_t BOUNCE_LUT[12] = {
    0, -3, -5, -6, -6, -5, -3, -1, 0, -2, -1, 0
};

/**
 * @brief Cooling RGB565 color LUT from pure white core to ember red tail.
 */
static constexpr uint16_t FIREWORK_COOLING_COLORS[6] = {
    0xFFFF, // Pure White (core flash)
    0xFFE0, // Arcade Gold
    0x07FF, // Cyber Cyan
    0xF81F, // Neon Magenta
    0xFD20, // Warm Orange
    0xC800  // Ember Red
};

/**
 * @brief Fixed-size POD particle (12 bytes) for 60 FPS Q8.8 fixed-point physics.
 */
struct FireworkParticlePod {
    int16_t x_q8 = 0;
    int16_t y_q8 = 0;
    int16_t vx_q8 = 0;
    int16_t vy_q8 = 0;
    uint8_t life = 0;
    uint8_t maxLife = 0;
    uint8_t colorSeed = 0;
    uint8_t kind = 0; // 0 = inactive, 1 = rising rocket, 2 = crown spark
};

static_assert(sizeof(FireworkParticlePod) == 12, "FireworkParticlePod must be exactly 12 bytes POD");

/**
 * @class FireworksLutRenderer
 * @brief Zero-allocation, zero-float Q8.8 fixed-point firework & star shower renderer.
 * Total state size: 24 * 12 + 8 = 296 bytes.
 */
class FireworksLutRenderer {
public:
    static constexpr uint8_t MAX_PARTICLES = 24;

    void reset(uint16_t panelW, uint16_t panelH) {
        m_frameTick = 0;
        m_burstCycle = 0;
        for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
            m_particles[i] = FireworkParticlePod{};
        }
        spawnRockets(panelW, panelH);
    }

    void spawnRockets(uint16_t panelW, uint16_t panelH) {
        if (panelW == 0) panelW = 128;
        if (panelH == 0) panelH = 32;
        const bool isTate = (panelH > panelW);

        // Rocket 0 (left side)
        m_particles[0].x_q8 = static_cast<int16_t>((panelW / 4) << 8);
        m_particles[0].y_q8 = static_cast<int16_t>((panelH - 2) << 8);
        m_particles[0].vx_q8 = 18;
        m_particles[0].vy_q8 = isTate ? -460 : -240;
        m_particles[0].life = 36;
        m_particles[0].maxLife = 36;
        m_particles[0].colorSeed = 0;
        m_particles[0].kind = 1;

        // Rocket 1 (right side)
        m_particles[1].x_q8 = static_cast<int16_t>(((panelW * 3) / 4) << 8);
        m_particles[1].y_q8 = static_cast<int16_t>((panelH - 2) << 8);
        m_particles[1].vx_q8 = -18;
        m_particles[1].vy_q8 = isTate ? -480 : -250;
        m_particles[1].life = 36;
        m_particles[1].maxLife = 36;
        m_particles[1].colorSeed = 2;
        m_particles[1].kind = 1;
    }

    void spawnCrownBurst(int16_t centerX_q8, int16_t centerY_q8, uint8_t startSlot, uint8_t count, uint8_t speedShift, uint8_t colorSeed) {
        for (uint8_t i = 0; i < count && (startSlot + i) < MAX_PARTICLES; ++i) {
            uint8_t dirIdx = (i * (16 / (count > 0 ? count : 1)) + colorSeed) & 0x0F;
            FireworkParticlePod& p = m_particles[startSlot + i];
            p.x_q8 = centerX_q8;
            p.y_q8 = centerY_q8;
            // Scale unit vector (256 = 1.0 px/frame)
            p.vx_q8 = static_cast<int16_t>((FIREWORK_DIRS_Q8[dirIdx][0] * static_cast<int16_t>(speedShift)) >> 2);
            p.vy_q8 = static_cast<int16_t>((FIREWORK_DIRS_Q8[dirIdx][1] * static_cast<int16_t>(speedShift)) >> 2);
            p.life = static_cast<uint8_t>(38 + ((i & 3) * 6));
            p.maxLife = p.life;
            p.colorSeed = static_cast<uint8_t>((colorSeed + i) & 0x03);
            p.kind = 2;
        }
    }

    void step(uint16_t panelW, uint16_t panelH) {
        if (panelW == 0) panelW = 128;
        if (panelH == 0) panelH = 32;
        const bool isTate = (panelH > panelW);

        m_frameTick++;

        // At frame 36 (0.6s) and every 72 frames afterwards, trigger staggered 16-particle crown bursts
        if (m_frameTick == 36 || (m_frameTick > 36 && ((m_frameTick - 36) % 72) == 0)) {
            int16_t cx0 = (m_particles[0].kind == 1)
                ? m_particles[0].x_q8
                : static_cast<int16_t>(((panelW / 4) + ((m_burstCycle & 1) ? 8 : -4)) << 8);
            int16_t cy0 = (m_particles[0].kind == 1)
                ? m_particles[0].y_q8
                : static_cast<int16_t>((isTate ? (panelH / 4) : (panelH / 3)) << 8);
            spawnCrownBurst(cx0, cy0, 0, 16, isTate ? 5 : 4, m_burstCycle);
            m_burstCycle++;
        } else if (m_frameTick == 54 || (m_frameTick > 54 && ((m_frameTick - 54) % 72) == 0)) {
            int16_t cx1 = static_cast<int16_t>((((panelW * 3) / 4) - ((m_burstCycle & 1) ? 6 : 0)) << 8);
            int16_t cy1 = static_cast<int16_t>((isTate ? (panelH / 3) : (panelH / 3 + 2)) << 8);
            spawnCrownBurst(cx1, cy1, 16, 8, isTate ? 4 : 3, static_cast<uint8_t>(m_burstCycle + 2));
        }

        for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
            FireworkParticlePod& p = m_particles[i];
            if (p.kind == 0 || p.life == 0) {
                p.kind = 0;
                continue;
            }

            p.x_q8 = static_cast<int16_t>(p.x_q8 + p.vx_q8);
            p.y_q8 = static_cast<int16_t>(p.y_q8 + p.vy_q8);

            if (p.kind == 2) {
                // Soft gravity (+5 in Q8.8 ~= 0.02 px/frame^2)
                p.vy_q8 = static_cast<int16_t>(p.vy_q8 + 5);
                // Aerodynamic drag via binary shift
                p.vx_q8 = static_cast<int16_t>(p.vx_q8 - (p.vx_q8 >> 5));
            }

            p.life--;
            if (p.life == 0) {
                p.kind = 0;
            }
        }
    }

    int8_t currentHeaderBounceOffset() const {
        if (m_frameTick < 36) return 0;
        uint16_t rel = m_frameTick - 36;
        if (rel < 12) {
            return BOUNCE_LUT[rel];
        }
        return 0;
    }

    bool isBorderFlashFrame() const {
        return (m_frameTick == 36 || m_frameTick == 37);
    }

    void render(IDrawingSurface* surface) const {
        if (!surface) return;
        const int16_t w = surface->width();
        const int16_t h = surface->height();

        for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
            const FireworkParticlePod& p = m_particles[i];
            if (p.kind == 0 || p.life == 0) continue;

            // Stroboscopic arcade twinkle at end of life
            if (p.life <= 12 && ((p.life & 1U) == 0U)) {
                continue;
            }

            int16_t px = p.x_q8 >> 8;
            int16_t py = p.y_q8 >> 8;
            if (px < 0 || px >= w || py < 0 || py >= h) continue;

            if (p.kind == 1) {
                // Rising rocket head + amber spark trail
                surface->drawPixel(px, py, 0xFFFF);
                if (py + 1 < h) surface->drawPixel(px, py + 1, 0xFFE0);
                if (py + 2 < h && (p.life & 1U)) surface->drawPixel(px, py + 2, 0xFD20);
            } else {
                // Cooling color selection from remaining life ratio
                uint16_t col = 0xFFFF;
                if (p.life * 4 > p.maxLife * 3) {
                    col = FIREWORK_COOLING_COLORS[0]; // White core
                } else if (p.life * 2 > p.maxLife) {
                    col = FIREWORK_COOLING_COLORS[1 + (p.colorSeed & 1U)]; // Gold or Cyan
                } else if (p.life * 4 > p.maxLife) {
                    col = FIREWORK_COOLING_COLORS[2 + (p.colorSeed & 1U)]; // Cyan or Magenta
                } else {
                    col = FIREWORK_COOLING_COLORS[4 + (p.colorSeed & 1U)]; // Orange or Ember Red
                }
                surface->drawPixel(px, py, col);
                // Bright cross sparkle right after burst
                if (p.life + 6 >= p.maxLife) {
                    if (px + 1 < w) surface->drawPixel(px + 1, py, 0xFFE0);
                    if (px > 0) surface->drawPixel(px - 1, py, 0xFFE0);
                }
            }
        }
    }

    const FireworkParticlePod& particle(uint8_t idx) const {
        return m_particles[idx < MAX_PARTICLES ? idx : 0];
    }

    uint16_t frameTick() const { return m_frameTick; }

private:
    FireworkParticlePod m_particles[MAX_PARTICLES]{};
    uint16_t m_frameTick = 0;
    uint8_t m_burstCycle = 0;
};

} // namespace renderers
