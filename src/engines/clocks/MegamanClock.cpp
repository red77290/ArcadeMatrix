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

    if (m_snapToNow || lastMinute == -1) {
        m_snapToNow = false;
        strcpy(shownHH, hh);
        strcpy(shownMM, mm);
        lastMinute = storedTime.minutes;
        lastSecond = storedTime.seconds;
        phase = Phase::Idle;
        phaseTimer = 0.0f;
        runTimer = 0.0f;
        runFrame = 0;
        megamanX = -50.0f;
        bulletActive = false;
        sparkActive = false;
        podBounce = 0.0f;
        m_dirty = 2;
    }

    // Seconds bar update
    if (lastSecond != storedTime.seconds) {
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }

    // Minute change trigger: Mega Man enters running to strike the clock!
    if (lastMinute != -1 && storedTime.minutes != lastMinute && phase == Phase::Idle) {
        phase = Phase::HeroEnter;
        phaseTimer = 0.0f;
        runTimer = 0.0f;
        runFrame = 0;
        megamanX = -30.0f;
        isChargeShot = (storedTime.minutes == 0);
        bulletActive = false;
        sparkActive = false;
        m_dirty = 2;
    }

    // Run animation step cycle (only animates when running)
    if (phase == Phase::HeroEnter || phase == Phase::HeroExit) {
        runTimer += dt;
        if (runTimer >= 0.09f) {
            runTimer -= 0.09f;
            runFrame++;
            m_dirty = 2;
        }
    }

    // State machine updates
    const bool isWideTall = (h >= 48);
    const int groundTop = h - (isWideTall ? GROUND_TILE_H : 6);
    int hourX, minX, podY;

    if (isWideTall) {
        int midX = w / 2;
        podY = 10;
        hourX = midX - 28;
        minX = midX + 4;
    } else {
        // 128x32 close-up layout
        podY = 6;
        hourX = w - 54;
        minX = w - 27;
    }

    float jumpStartX = hourX - (isWideTall ? 44.0f : 32.0f);
    if (jumpStartX < 4.0f) jumpStartX = 4.0f;

    switch (phase) {
        case Phase::Idle:
            break;

        case Phase::HeroEnter:
            m_dirty = 2;
            megamanX += 85.0f * dt;
            megamanY = groundTop - MEGAMAN_RUN1_H;
            if (megamanX >= jumpStartX) {
                phase = Phase::HeroJump;
                phaseTimer = 0.0f;
            }
            break;

        case Phase::HeroJump: {
            m_dirty = 2;
            phaseTimer += dt;
            const float jumpDuration = 0.75f;
            float p = phaseTimer / jumpDuration;
            if (p > 1.0f) p = 1.0f;

            megamanX += 80.0f * dt;
            float maxLift = isWideTall ? 24.0f : 13.0f;
            float lift = sinf(p * 3.14159f) * maxLift;
            megamanY = (groundTop - MEGAMAN_JUMP_H) - lift;

            // At jump peak, fire Buster lemon towards minute pod
            if (p >= 0.32f && !bulletActive && !sparkActive) {
                bulletActive = true;
                bulletX = megamanX + MEGAMAN_JUMP_SHOOT_W - 2;
                bulletY = megamanY + 8;
                bulletTargetX = minX + 6;
                bulletTargetY = podY + 7;
            }

            if (p >= 1.0f) {
                // Landed on ground
                phase = Phase::HeroExit;
                phaseTimer = 0.0f;
            }
            break;
        }

        case Phase::HeroExit:
            m_dirty = 2;
            megamanX += 95.0f * dt;
            megamanY = groundTop - MEGAMAN_RUN1_H;
            if (megamanX > (float)w + 10.0f) {
                phase = Phase::Idle;
                phaseTimer = 0.0f;
                megamanX = -50.0f;
                m_dirty = 3;
            }
            break;
    }

    // Projectile physics (flies across screen to minute pod)
    if (bulletActive) {
        m_dirty = 2;
        float bulletSpeed = 420.0f * dt;
        bulletX += bulletSpeed;
        if (bulletX >= bulletTargetX) {
            bulletActive = false;
            sparkActive = true;
            sparkTimer = 0.0f;
            podBounce = isChargeShot ? 5.0f : 3.5f;
            strcpy(shownHH, hh);
            strcpy(shownMM, mm);
            lastMinute = storedTime.minutes;
        }
    }

    // Hit Spark physics
    if (sparkActive) {
        m_dirty = 2;
        sparkTimer += dt;
        podBounce *= 0.85f;
        if (sparkTimer >= 0.22f) {
            sparkActive = false;
            podBounce = 0.0f;
        }
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

    // Mega Man rendering (appears only during minute sequence)
    if (phase == Phase::HeroEnter || phase == Phase::HeroExit) {
        const uint16_t* runSprite;
        int rw, rh;
        switch (runFrame % 4) {
            case 0:
                runSprite = MEGAMAN_RUN1;
                rw = MEGAMAN_RUN1_W;
                rh = MEGAMAN_RUN1_H;
                break;
            case 1:
                runSprite = MEGAMAN_RUN_PASS1;
                rw = MEGAMAN_RUN_PASS_W;
                rh = MEGAMAN_RUN_PASS_H;
                break;
            case 2:
                runSprite = MEGAMAN_RUN2;
                rw = MEGAMAN_RUN2_W;
                rh = MEGAMAN_RUN2_H;
                break;
            default:
                runSprite = MEGAMAN_RUN_PASS2;
                rw = MEGAMAN_RUN_PASS_W;
                rh = MEGAMAN_RUN_PASS_H;
                break;
        }
        blitSprite(runSprite, rw, rh, (int)megamanX, groundTop - rh, true);
    } else if (phase == Phase::HeroJump) {
        float p = phaseTimer / 0.75f;
        if (p < 0.28f || p >= 0.65f) {
            blitSprite(MEGAMAN_JUMP, MEGAMAN_JUMP_W, MEGAMAN_JUMP_H, (int)megamanX, (int)megamanY, true);
        } else {
            // Peak jump shooting pose
            blitSprite(MEGAMAN_JUMP_SHOOT, MEGAMAN_JUMP_SHOOT_W, MEGAMAN_JUMP_SHOOT_H, (int)megamanX, (int)megamanY, true);
            if (p < 0.40f) {
                uint16_t muzzleAura = ((int)(phaseTimer * 30.0f) % 2 == 0) ? matrix->color565(0, 232, 216) : matrix->color565(252, 224, 0);
                matrix->drawCircle((int)megamanX + MEGAMAN_JUMP_SHOOT_W - 1, (int)megamanY + 8, 3, muzzleAura);
            }
        }
    }

    // Bullet drawing
    if (bulletActive) {
        if (isChargeShot) {
            matrix->fillCircle((int)bulletX + 4, (int)bulletY + 3, 5, matrix->color565(0, 232, 216));
            matrix->fillCircle((int)bulletX + 4, (int)bulletY + 3, 3, 0xFFFF);
        } else {
            blitSprite(BUSTER_BULLET, BULLET_W, BULLET_H, (int)bulletX, (int)bulletY, true);
        }
    }

    // Hit Spark drawing
    if (sparkActive) {
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
