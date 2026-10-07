#include "MegamanClock.h"
#include "MegamanAssets.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

using namespace MegamanAssets;

namespace {
struct Building {
    int16_t x;
    int16_t h;
    int16_t w;
};

// Preset building silhouettes for the background skyline
static const Building SKYLINE_BUILDINGS[] = {
    {0, 35, 18}, {20, 25, 16}, {40, 42, 22}, {65, 30, 25}, {95, 20, 20},
    {120, 38, 22}, {145, 28, 18}, {165, 45, 24}, {192, 22, 26}, {222, 34, 30}
};
constexpr size_t SKYLINE_COUNT = sizeof(SKYLINE_BUILDINGS) / sizeof(SKYLINE_BUILDINGS[0]);
} // namespace

MegamanClock::MegamanClock(IDrawingSurface* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 0, 0, 0 };
}

void MegamanClock::draw(const TimeData& t) {
    storedTime = t;
}

void MegamanClock::blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent) {
    if (!matrix || !data) return;
    const int panelW = matrix->width();
    const int panelH = matrix->height();
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= panelH) continue;
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= panelW) continue;
            uint16_t c = data[row * w + col];
            if (transparent && c == MASK) continue;
            matrix->drawPixel(px, py, c);
        }
    }
}

void MegamanClock::drawCapcomDigit(int x, int y, char c, uint16_t color) {
    int idx = -1;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == ':') idx = 10;
    if (idx < 0) return;

    for (int row = 0; row < 7; row++) {
        uint8_t rowBits = CAPCOM_FONT[idx][row];
        for (int col = 0; col < 5; col++) {
            if ((rowBits >> (4 - col)) & 1) {
                matrix->drawPixel(x + col, y + row, color);
            }
        }
    }
}

void MegamanClock::drawTimePod(int x, int y, const char* text, int bounce, bool highlight) {
    int py = y - bounce;
    uint16_t borderCol = highlight ? 0xFFFF : matrix->color565(0, 112, 236);
    uint16_t innerCol = highlight ? matrix->color565(0, 232, 216) : matrix->color565(0, 40, 80);
    uint16_t bgCol = matrix->color565(10, 16, 28);
    uint16_t textCol = highlight ? 0xFFFF : matrix->color565(0, 232, 216);

    matrix->fillRect(x, py, POD_W, POD_H, bgCol);
    matrix->drawRect(x, py, POD_W, POD_H, borderCol);
    matrix->drawFastHLine(x + 1, py + 1, POD_W - 2, innerCol);
    matrix->drawFastHLine(x + 1, py + POD_H - 2, POD_W - 2, innerCol);

    // Text centered: each digit 5px, 1px space -> 11px wide total for 2 digits
    int len = strlen(text);
    int totalW = (len == 2) ? 11 : 5;
    int tx = x + (POD_W - totalW) / 2;
    int ty = py + (POD_H - 7) / 2;

    for (int i = 0; i < len; i++) {
        drawCapcomDigit(tx + i * 6, ty, text[i], textCol);
    }
}

void MegamanClock::drawEnergyGauge(int x, int y, int seconds) {
    int bars = (seconds * 28) / 59;
    matrix->fillRect(x, y, 6, 30, 0x0000);
    matrix->drawRect(x, y, 6, 30, 0xFFFF);

    for (int b = 0; b < 28; b++) {
        int by = y + 28 - b;
        if (b < bars) {
            uint16_t col = (b % 2 == 0) ? matrix->color565(252, 224, 0) : 0xFFFF;
            matrix->drawFastHLine(x + 1, by, 4, col);
        }
    }
}

void MegamanClock::drawMiniEnergyGauge(int x, int y, int seconds) {
    matrix->fillRect(x, y, 22, 4, 0x0000);
    matrix->drawRect(x, y, 22, 4, matrix->color565(0, 112, 236));
    int fillLen = (seconds * 20) / 59;
    if (fillLen > 0) {
        matrix->drawFastHLine(x + 1, y + 1, fillLen, matrix->color565(252, 224, 0));
        matrix->drawFastHLine(x + 1, y + 2, fillLen, 0xFFFF);
    }
}

void MegamanClock::drawScene(int w, int h) {
    // Night sky vertical gradient
    for (int y = 0; y < h; y++) {
        uint8_t grad = (uint8_t)(10 + (y * 22) / (h > 0 ? h : 1));
        matrix->drawFastHLine(0, y, w, matrix->color565(6, 10, grad));
    }

    // Skyline silhouettes
    uint16_t buildingCol = matrix->color565(16, 24, 40);
    uint16_t litYellow = matrix->color565(252, 224, 0);
    uint16_t litCyan = matrix->color565(0, 232, 216);

    for (size_t i = 0; i < SKYLINE_COUNT; i++) {
        const Building& b = SKYLINE_BUILDINGS[i];
        if (b.x >= w) continue;
        int bw = (b.x + b.w > w) ? (w - b.x) : b.w;
        int by = h - b.h;
        if (by < 0) by = 0;
        matrix->fillRect(b.x, by, bw, h - by, buildingCol);

        // Windows
        for (int wx = b.x + 3; wx < b.x + bw - 3; wx += 5) {
            for (int wy = by + 4; wy < h - 8; wy += 6) {
                if ((wx + wy) % 7 == 0) {
                    matrix->drawPixel(wx, wy, litYellow);
                } else if ((wx + wy) % 11 == 0) {
                    matrix->drawPixel(wx, wy, litCyan);
                }
            }
        }
    }

    // Tech ground tiles across bottom
    int groundTop = h - GROUND_TILE_H;
    for (int gx = 0; gx < w; gx += GROUND_TILE_W) {
        blitSprite(GROUND_TILE, GROUND_TILE_W, GROUND_TILE_H, gx, groundTop, false);
    }
}

void MegamanClock::update() {
    if (!matrix) return;
    const int w = matrix->width();
    const int h = matrix->height();

    uint32_t now = millis();
    float dt = (lastFrameMs == 0) ? 0.016f : (now - lastFrameMs) / 1000.0f;
    if (dt > 0.2f) dt = 0.2f;
    lastFrameMs = now;

    char hh[4], mm[4];
    snprintf(hh, sizeof(hh), "%02d", storedTime.hours);
    snprintf(mm, sizeof(mm), "%02d", storedTime.minutes);

    if (m_snapToNow) {
        m_snapToNow = false;
        strcpy(shownHH, hh);
        strcpy(shownMM, mm);
        lastMinute = storedTime.minutes;
        lastSecond = storedTime.seconds;
        phase = Phase::Waiting;
        phaseTimer = 0.0f;
        podBounce = 0.0f;
        m_dirty = 2;
    }

    // Seconds bar update
    if (lastSecond != storedTime.seconds) {
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }

    // Minute change trigger
    if (lastMinute != storedTime.minutes && phase == Phase::Waiting) {
        phase = Phase::ShootPrep;
        phaseTimer = 0.0f;
        isChargeShot = (storedTime.minutes == 0);
        m_dirty = 2;
    }

    // Blink timer (blinks every ~3.5s for 150ms)
    blinkTimer += dt;
    if (blinkTimer >= 3.5f) {
        isBlinking = true;
        m_dirty = 2;
        if (blinkTimer >= 3.65f) {
            isBlinking = false;
            blinkTimer = 0.0f;
            m_dirty = 2;
        }
    }

    // Metool peek timer (peeks every 8s)
    metoolTimer += dt;
    if (metoolTimer >= 8.0f) {
        isMetoolPeeking = true;
        m_dirty = 2;
        if (metoolTimer >= 8.8f) {
            isMetoolPeeking = false;
            metoolTimer = 0.0f;
            m_dirty = 2;
        }
    }

    // State machine updates
    const bool isWideTall = (h >= 48);
    const int groundTop = h - (isWideTall ? GROUND_TILE_H : 6);
    int hourX, minX, podY, mmX, mmY;

    if (isWideTall) {
        int midX = w / 2;
        podY = 10;
        hourX = midX - 28;
        minX = midX + 4;
        mmX = 18;
        mmY = groundTop - MEGAMAN_IDLE_H;
    } else {
        // 128x32 close-up layout
        podY = 6;
        hourX = w - 54;
        minX = w - 27;
        mmX = 8;
        mmY = groundTop - MEGAMAN_IDLE_H;
    }

    switch (phase) {
        case Phase::Waiting:
            break;

        case Phase::ShootPrep:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.15f) {
                phase = Phase::BulletFlying;
                phaseTimer = 0.0f;
                if (isWideTall) {
                    int lift = (int)(sinf(0.35f * 3.14159f) * 16.0f);
                    int currentY = groundTop - MEGAMAN_JUMP_SHOOT_H - lift;
                    bulletX = mmX + 4 + MEGAMAN_JUMP_SHOOT_W - 4;
                    bulletY = currentY + 9;
                } else {
                    bulletX = mmX + MEGAMAN_SHOOT_W - 4;
                    bulletY = mmY + 7;
                }
                bulletTargetX = minX + 4;
                bulletTargetY = podY + 7;
            }
            break;

        case Phase::BulletFlying: {
            phaseTimer += dt;
            m_dirty = 2;
            float bulletSpeed = 380.0f; // px/sec
            bulletX += bulletSpeed * dt;
            if (bulletX >= bulletTargetX) {
                phase = Phase::HitSpark;
                phaseTimer = 0.0f;
                podBounce = isChargeShot ? 4.5f : 3.0f;
                strcpy(shownHH, hh);
                strcpy(shownMM, mm);
                lastMinute = storedTime.minutes;
            }
            break;
        }

        case Phase::HitSpark:
            phaseTimer += dt;
            m_dirty = 2;
            podBounce *= 0.85f;
            if (phaseTimer >= 0.20f) {
                phase = Phase::Cooldown;
                phaseTimer = 0.0f;
            }
            break;

        case Phase::Cooldown:
            phaseTimer += dt;
            m_dirty = 2;
            podBounce *= 0.80f;
            if (phaseTimer >= 0.12f) {
                phase = Phase::Waiting;
                phaseTimer = 0.0f;
                podBounce = 0.0f;
            }
            break;
    }

    if (m_dirty == 0) {
        m_hasFrame = false;
        return;
    }
    m_dirty--;
    m_hasFrame = true;

    // --- Render frame ---
    drawScene(w, h);

    // Pods
    int bounceInt = (int)podBounce;
    bool sparkActive = (phase == Phase::HitSpark);
    drawTimePod(hourX, podY, shownHH, isChargeShot ? bounceInt : 0, isChargeShot && sparkActive);
    drawTimePod(minX, podY, shownMM, bounceInt, sparkActive);

    // Energy gauge
    if (isWideTall) {
        drawEnergyGauge(3, 6, storedTime.seconds);
        if (w >= 128) {
            int metoolX = w - METOOL_W - 4;
            blitSprite(METOOL_SLEEP, METOOL_W, METOOL_H, metoolX, groundTop - METOOL_H, true);
        }
    } else {
        drawMiniEnergyGauge(6, 2, storedTime.seconds);
    }

    // Mega Man rendering
    if (isWideTall && (phase == Phase::ShootPrep || phase == Phase::BulletFlying || phase == Phase::HitSpark)) {
        float jumpProgress = (phase == Phase::ShootPrep) ? (phaseTimer / 0.15f * 0.35f) :
                             (phase == Phase::BulletFlying) ? (0.35f + (bulletX - mmX) / (bulletTargetX > mmX ? (bulletTargetX - mmX) : 1.0f) * 0.35f) :
                             (0.70f + (phaseTimer / 0.20f) * 0.30f);
        if (jumpProgress > 1.0f) jumpProgress = 1.0f;
        int lift = (int)(sinf(jumpProgress * 3.14159f) * 16.0f);
        int jx = mmX + (int)(jumpProgress * 12.0f);
        int jy = groundTop - MEGAMAN_JUMP_SHOOT_H - lift;
        blitSprite(MEGAMAN_JUMP_SHOOT, MEGAMAN_JUMP_SHOOT_W, MEGAMAN_JUMP_SHOOT_H, jx, jy, true);
        if (phase == Phase::BulletFlying) {
            blitSprite(BUSTER_BULLET, BULLET_W, BULLET_H, (int)bulletX, (int)bulletY, true);
        }
    } else if (phase == Phase::ShootPrep || phase == Phase::BulletFlying) {
        blitSprite(MEGAMAN_SHOOT, MEGAMAN_SHOOT_W, MEGAMAN_SHOOT_H, mmX, mmY, true);
        if (phase == Phase::BulletFlying) {
            blitSprite(BUSTER_BULLET, BULLET_W, BULLET_H, (int)bulletX, (int)bulletY, true);
        }
    } else {
        const uint16_t* mmSprite = isBlinking ? MEGAMAN_BLINK : MEGAMAN_IDLE;
        blitSprite(mmSprite, MEGAMAN_IDLE_W, MEGAMAN_IDLE_H, mmX, mmY, true);
    }

    // Hit Spark
    if (phase == Phase::HitSpark) {
        int sparkX = minX - (SPARK_W / 2) + 2;
        int sparkY = podY + (POD_H / 2) - (SPARK_H / 2);
        blitSprite(HIT_SPARK, SPARK_W, SPARK_H, sparkX, sparkY, true);
    }
}

void MegamanClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    m_snapToNow = true;
    lastFrameMs = 0;
}
