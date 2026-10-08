#include "StreetFighterClock.h"
#include "StreetFighterAssets.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

using namespace StreetFighterAssets;

StreetFighterClock::StreetFighterClock(IDrawingSurface* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 0, 0, 0 };
}

void StreetFighterClock::draw(const TimeData& t) {
    storedTime = t;
}

void StreetFighterClock::blitSprite(const uint16_t* palette, const uint8_t* pixels, int w, int h, int x, int y, bool flipH) {
    if (!matrix || !palette || !pixels) return;
    const int panelW = matrix->width();
    const int panelH = matrix->height();
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= panelH) continue;
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= panelW) continue;
            int srcCol = flipH ? (w - 1 - col) : col;
            uint8_t idx = pgm_read_byte(&pixels[row * w + srcCol]);
            if (idx == 0) continue;
            matrix->drawPixel(px, py, pgm_read_word(&palette[idx]));
        }
    }
}

void StreetFighterClock::drawArcadeDigit(int x, int y, char c, uint16_t color, int scale) {
    int idx = -1;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == ':') idx = 10;
    if (idx < 0) return;

    for (int r = 0; r < 5; r++) {
        uint8_t rowBits = ARCADE_FONT_3x5[idx][r];
        int py = y + r * scale;
        for (int b = 0; b < 3; b++) {
            if ((rowBits >> (2 - b)) & 1) {
                int px = x + b * scale;
                matrix->fillRect(px, py, scale, scale, color);
            }
        }
    }
}

void StreetFighterClock::drawArcadeTime(int startX, int startY, const char* str, uint16_t color, int scale) {
    if (strlen(str) < 5) return;
    int digitW = 3 * scale;
    int digitGap = 1 * scale;
    int colonGap = 2 * scale;
    int colonW = 1 * scale;

    int x = startX;
    drawArcadeDigit(x, startY, str[0], color, scale);
    x += digitW + digitGap;
    drawArcadeDigit(x, startY, str[1], color, scale);
    x += digitW + colonGap;
    drawArcadeDigit(x, startY, str[2], color, scale);
    x += colonW + colonGap;
    drawArcadeDigit(x, startY, str[3], color, scale);
    x += digitW + digitGap;
    drawArcadeDigit(x, startY, str[4], color, scale);
}

void StreetFighterClock::drawScene(int w, int h) {
#if !defined(HARDWARE_PROFILE_ESP32_DEV)
    if (h >= 48) {
        // --- 256x64 Suzaku Castle Rooftop ---
        uint16_t skyColor = matrix->color565(16, 24, 56);
        matrix->fillRect(0, 0, w, 50, skyColor);

        // Crescent moon between Ryu and center
        uint16_t moonColor = matrix->color565(248, 248, 200);
        matrix->fillCircle(67, 17, 9, moonColor);
        matrix->fillCircle(72, 17, 9, skyColor);

        // Suzaku Castle Japanese tiled roof (y=50..63)
        uint16_t roofBase = matrix->color565(80, 90, 104);
        uint16_t roofHighlight = matrix->color565(160, 176, 192);
        uint16_t roofDark = matrix->color565(48, 56, 72);
        uint16_t roofTile = matrix->color565(120, 136, 152);

        matrix->fillRect(0, 50, w, h - 50, roofBase);
        matrix->drawFastHLine(0, 50, w, roofHighlight);

        for (int x = 0; x < w; x += 8) {
            matrix->drawFastVLine(x, 50, h - 50, roofDark);
            matrix->drawFastVLine(x + 1, 50, h - 50, roofTile);
        }
    } else
#endif
    {
        // --- 128x32 Dojo / Tatami Stage ---
        matrix->fillRect(0, 0, w, 27, matrix->color565(16, 16, 24));

        // Tatami floor at y=27..31
        uint16_t tatami = matrix->color565(180, 140, 90);
        uint16_t tatamiLight = matrix->color565(220, 180, 120);
        uint16_t tatamiBorder = matrix->color565(90, 60, 30);

        matrix->fillRect(0, 27, w, h - 27, tatami);
        matrix->drawFastHLine(0, 27, w, tatamiLight);
        matrix->drawFastHLine(0, 28, w, tatamiBorder);
    }
}

void StreetFighterClock::drawHealthBars(int w, int h) {
    uint16_t barYellow = matrix->color565(252, 216, 0);
    uint16_t koRed = matrix->color565(220, 32, 24);
    uint16_t white = 0xFFFF;
    bool isFlashing = (koFlashTimer > 0.0f);

#if !defined(HARDWARE_PROFILE_ESP32_DEV)
    if (h >= 48) {
        // 256x64: Health bars situated between fighter heads and central KO
        matrix->fillRect(50, 3, 63, 4, barYellow);
        matrix->fillRect(144, 3, 63, 4, barYellow);

        // Central K.O. badge
        matrix->fillRect(122, 1, 13, 8, isFlashing ? white : koRed);
        matrix->drawRect(122, 1, 13, 8, white);
        matrix->drawPixel(125, 3, isFlashing ? koRed : white);
        matrix->drawPixel(131, 3, isFlashing ? koRed : white);
    } else
#endif
    {
        // 128x32: Compact lifebars at y=1..2
        matrix->fillRect(6, 1, 33, 2, barYellow);
        matrix->fillRect(90, 1, 33, 2, barYellow);

        // K.O. in center
        matrix->fillRect(60, 0, 9, 4, isFlashing ? white : koRed);
        matrix->drawPixel(62, 1, isFlashing ? koRed : white);
        matrix->drawPixel(66, 1, isFlashing ? koRed : white);
    }
}

void StreetFighterClock::update() {
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
        phase = Phase::Idle;
        phaseTimer = 0.0f;
        koFlashTimer = 0.0f;
        breathTimer = 0.0f;
        breathFrame = 0;
        m_dirty = 2;
    }

    // Second tick
    if (lastSecond != storedTime.seconds) {
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }

    // Minute transition -> Fire Hadouken!
    if (lastMinute != storedTime.minutes && phase == Phase::Idle) {
        phase = Phase::HadoukenWindup;
        phaseTimer = 0.0f;
        m_dirty = 2;
    }

    // Idle breathing cycle (advance frame every 200ms)
    if (phase == Phase::Idle) {
        breathTimer += dt;
        if (breathTimer >= 0.20f) {
            breathTimer = 0.0f;
            breathFrame = (breathFrame + 1) % 4;
            m_dirty = 2;
        }
    }

    // State machine updates
    switch (phase) {
        case Phase::Idle:
            break;

        case Phase::HadoukenWindup:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.18f) {
                phase = Phase::HadoukenFlying;
                phaseTimer = 0.0f;
#if !defined(HARDWARE_PROFILE_ESP32_DEV)
                if (h >= 48) {
                    hadoukenX = 18 + SUZAKU_RYU_HADOUKEN_W - 6;
                    hadoukenY = 22.0f;
                    hadoukenTargetX = 208.0f;
                } else
#endif
                {
                    hadoukenX = 6 + SFXMM_RYU_PUNCH_W - 2;
                    hadoukenY = 12.0f;
                    hadoukenTargetX = 101.0f;
                }
            }
            break;

        case Phase::HadoukenFlying: {
            phaseTimer += dt;
            m_dirty = 2;
#if !defined(HARDWARE_PROFILE_ESP32_DEV)
            float speed = (h >= 48) ? 380.0f : 240.0f; // px/sec
#else
            float speed = 240.0f;
#endif
            hadoukenX += speed * dt;
            if (hadoukenX >= hadoukenTargetX) {
                phase = Phase::HadoukenImpact;
                phaseTimer = 0.0f;
                koFlashTimer = 0.30f;
                strcpy(shownHH, hh);
                strcpy(shownMM, mm);
                lastMinute = storedTime.minutes;
            }
            break;
        }

        case Phase::HadoukenImpact:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.25f) {
                phase = Phase::Cooldown;
                phaseTimer = 0.0f;
            }
            break;

        case Phase::Cooldown:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.15f) {
                phase = Phase::Idle;
                phaseTimer = 0.0f;
            }
            break;
    }

    // KO Flash timer
    if (koFlashTimer > 0.0f) {
        koFlashTimer -= dt;
        m_dirty = 2;
        if (koFlashTimer < 0.0f) koFlashTimer = 0.0f;
    }

    // Sleep optimization: when double buffer is clean, stop redrawing
    if (m_dirty == 0) {
        m_hasFrame = false;
        return;
    }
    m_dirty--;
    m_hasFrame = true;

    // --- Render frame ---
    drawScene(w, h);
    drawHealthBars(w, h);

    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%s:%s", shownHH, shownMM);
    uint16_t goldTime = matrix->color565(252, 220, 0);

#if !defined(HARDWARE_PROFILE_ESP32_DEV)
    if (h >= 48) {
        // --- 256x64 Layout ---
        // Ryu
        if (phase == Phase::HadoukenWindup || phase == Phase::HadoukenFlying) {
            blitSprite(SUZAKU_RYU_HADOUKEN_PAL, SUZAKU_RYU_HADOUKEN_PIXELS, SUZAKU_RYU_HADOUKEN_W, SUZAKU_RYU_HADOUKEN_H, 18, 6, false);
        } else {
            blitSprite(SUZAKU_RYU_BREATH_PALS[breathFrame % 4], SUZAKU_RYU_BREATH_PIXELS[breathFrame % 4], 26, 44, 18, 6, false);
        }

        // Ken (opposite Ryu, breathing with +2 phase shift)
        if (phase == Phase::HadoukenImpact) {
            blitSprite(SUZAKU_KEN_HIT_PAL, SUZAKU_KEN_HIT_PIXELS, SUZAKU_KEN_HIT_W, SUZAKU_KEN_HIT_H, 208, 6, true);
            // Hit Spark
            blitSprite(HIT_SPARK_PAL, HIT_SPARK_PIXELS, HIT_SPARK_W, HIT_SPARK_H, 208 - 4, 18, false);
        } else {
            blitSprite(SUZAKU_KEN_BREATH_PALS[(breathFrame + 2) % 4], SUZAKU_KEN_BREATH_PIXELS[(breathFrame + 2) % 4], 30, 44, 208, 6, true);
        }

        // Hadouken projectile
        if (phase == Phase::HadoukenFlying) {
            blitSprite(HADOUKEN_BALL_PAL, HADOUKEN_BALL_PIXELS, HADOUKEN_BALL_W, HADOUKEN_BALL_H, (int)hadoukenX, (int)hadoukenY, false);
        }

        // Central arcade time digits (scale 5 = 95px wide, centered at 80)
        drawArcadeTime(80, 16, timeBuf, goldTime, 5);
    } else
#endif
    {
        // --- 128x32 Layout ---
        int ryuY = 4 + (phase == Phase::Idle && (breathFrame % 2 == 1) ? 1 : 0);
        if (phase == Phase::HadoukenWindup || phase == Phase::HadoukenFlying) {
            blitSprite(SFXMM_RYU_PUNCH_PAL, SFXMM_RYU_PUNCH_PIXELS, SFXMM_RYU_PUNCH_W, SFXMM_RYU_PUNCH_H, 6, 4, false);
        } else {
            blitSprite(SFXMM_RYU_PAL, SFXMM_RYU_PIXELS, SFXMM_RYU_W, SFXMM_RYU_H, 6, ryuY, false);
        }

        // Ken
        int kenY = 4 + (phase == Phase::Idle && ((breathFrame + 1) % 2 == 1) ? 1 : 0);
        if (phase == Phase::HadoukenImpact) {
            blitSprite(SFXMM_KEN_HIT_PAL, SFXMM_KEN_HIT_PIXELS, SFXMM_KEN_HIT_W, SFXMM_KEN_HIT_H, 101, 4, true);
            blitSprite(HIT_SPARK_PAL, HIT_SPARK_PIXELS, HIT_SPARK_W, HIT_SPARK_H, 98, 10, false);
        } else {
            blitSprite(SFXMM_KEN_PAL, SFXMM_KEN_PIXELS, SFXMM_KEN_W, SFXMM_KEN_H, 101, kenY, true);
        }

        // Hadouken projectile
        if (phase == Phase::HadoukenFlying) {
            blitSprite(HADOUKEN_BALL_PAL, HADOUKEN_BALL_PIXELS, HADOUKEN_BALL_W, HADOUKEN_BALL_H, (int)hadoukenX, (int)hadoukenY, false);
        }

        // Central arcade time digits (scale 3 = 57px wide, centered at 35)
        drawArcadeTime(35, 10, timeBuf, goldTime, 3);
    }
}

void StreetFighterClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    m_snapToNow = true;
    lastFrameMs = 0;
    phase = Phase::Idle;
    phaseTimer = 0.0f;
}
