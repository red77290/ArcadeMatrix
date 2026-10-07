#include "CastleClock.h"
#include "CastlevaniaAssets.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

using namespace CastlevaniaAssets;

CastleClock::CastleClock(IDrawingSurface* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 0, 0, 0 };
}

void CastleClock::draw(const TimeData& t) {
    storedTime = t;
}

void CastleClock::blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent, bool flipH) {
    if (!matrix || !data) return;
    const int panelW = matrix->width();
    const int panelH = matrix->height();
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= panelH) continue;
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= panelW) continue;
            int srcCol = flipH ? (w - 1 - col) : col;
            uint16_t c = data[row * w + srcCol];
            if (transparent && c == MASK) continue;
            matrix->drawPixel(px, py, c);
        }
    }
}

void CastleClock::drawGothicDigit(int x, int y, char c, uint16_t color, int scale) {
    int idx = -1;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == ':') idx = 10;
    if (idx < 0) return;

    for (int r = 0; r < 5; r++) {
        uint8_t rowBits = GOTHIC_FONT_3x5[idx][r];
        int py = y + r * scale;
        for (int b = 0; b < 3; b++) {
            if ((rowBits >> (2 - b)) & 1) {
                int px = x + b * scale;
                matrix->fillRect(px, py, scale, scale, color);
            }
        }
    }
}

void CastleClock::drawGothicTime(int startX, int startY, const char* str, uint16_t color, int scale) {
    int x = startX;
    for (int i = 0; str[i] != '\0'; i++) {
        char ch = str[i];
        drawGothicDigit(x, startY, ch, color, scale);
        int gw = (ch == ':') ? 1 : 3;
        x += (gw + 1) * scale;
    }
}

void CastleClock::drawScene(int w, int h) {
    if (h >= 48) {
        // --- 256x64 Dracula's Castle ---
        uint16_t nightSky = matrix->color565(16, 0, 24);
        matrix->fillRect(0, 0, w, 48, nightSky);

        // Blood Moon
        uint16_t moonOuter = matrix->color565(220, 40, 20);
        uint16_t moonInner = matrix->color565(240, 80, 40);
        matrix->fillCircle(216, 22, 18, moonOuter);
        matrix->fillCircle(214, 20, 15, moonInner);

        // Gothic castle battlements & parapets at y=48..63
        uint16_t stoneBase = matrix->color565(80, 80, 96);
        uint16_t stoneLight = matrix->color565(140, 140, 160);
        uint16_t stoneDark = matrix->color565(36, 36, 48);

        matrix->fillRect(0, 48, w, h - 48, stoneBase);
        matrix->drawFastHLine(0, 48, w, stoneLight);

        for (int x = 0; x < w; x += 16) {
            matrix->drawFastVLine(x, 48, h - 48, stoneDark);
            matrix->drawFastVLine(x + 1, 48, h - 48, stoneLight);
        }

        // Candle Torch Stand on right (x=240, y=26..47)
        uint16_t torchMetal = matrix->color565(140, 100, 50);
        matrix->fillRect(240, 34, 4, 14, torchMetal);
        matrix->fillRect(238, 30, 8, 4, torchMetal);

        // Torch flame
        const uint16_t* flame = FLAME_FRAMES[flameFrame % 3];
        blitSprite(flame, FLAME_F0_W, FLAME_F0_H, 238, 20, true, false);

        // Flying Vampire Bat flapping across blood moon
        const uint16_t* bat = BAT_FRAMES[batFrame % 2];
        blitSprite(bat, BAT_F0_W, BAT_F0_H, (int)batX, 10, true, false);
    } else {
        // --- 128x32 Dungeon Floor ---
        matrix->fillRect(0, 0, w, 26, 0x0000);

        // Brick floor at y=26..31
        uint16_t brickCol = matrix->color565(116, 40, 12);
        uint16_t brickMortar = matrix->color565(40, 16, 8);
        matrix->fillRect(0, 26, w, h - 26, brickCol);
        matrix->drawFastHLine(0, 26, w, matrix->color565(180, 70, 20));

        for (int x = 0; x < w; x += 8) {
            matrix->drawFastVLine(x, 26, h - 26, brickMortar);
        }

        // Candle Torch on right
        matrix->fillRect(116, 20, 2, 6, matrix->color565(140, 100, 50));
        const uint16_t* flame = FLAME_FRAMES[flameFrame % 3];
        blitSprite(flame, FLAME_F0_W, FLAME_F0_H, 113, 10, true, false);
    }
}

void CastleClock::update() {
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
        walkTimer = 0.0f;
        walkFrame = 0;
        flameTimer = 0.0f;
        flameFrame = 0;
        batTimer = 0.0f;
        batFrame = 0;
        batX = 140.0f;
        m_dirty = 2;
    }

    // Second tick
    if (lastSecond != storedTime.seconds) {
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }

    // Minute transition -> Crack whip!
    if (lastMinute != storedTime.minutes && phase == Phase::Idle) {
        phase = Phase::WhipWindup;
        phaseTimer = 0.0f;
        m_dirty = 2;
    }

    // Flame flicker (every 120ms)
    flameTimer += dt;
    if (flameTimer >= 0.12f) {
        flameTimer = 0.0f;
        flameFrame = (flameFrame + 1) % 3;
        m_dirty = 2;
    }

    // Bat flap and glide
    batTimer += dt;
    if (batTimer >= 0.18f) {
        batTimer = 0.0f;
        batFrame = (batFrame + 1) % 2;
        m_dirty = 2;
    }
    batX -= 25.0f * dt;
    if (batX < -20.0f) batX = (float)w + 10.0f;

    // Idle step cycle (every 300ms)
    if (phase == Phase::Idle) {
        walkTimer += dt;
        if (walkTimer >= 0.30f) {
            walkTimer = 0.0f;
            walkFrame = (walkFrame + 1) % 3;
            m_dirty = 2;
        }
    }

    // Attack state machine
    switch (phase) {
        case Phase::Idle:
            break;

        case Phase::WhipWindup:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.15f) {
                phase = Phase::WhipStrike;
                phaseTimer = 0.0f;
            }
            break;

        case Phase::WhipStrike:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.20f) {
                phase = Phase::Impact;
                phaseTimer = 0.0f;
                strcpy(shownHH, hh);
                strcpy(shownMM, mm);
                lastMinute = storedTime.minutes;
            }
            break;

        case Phase::Impact:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.15f) {
                phase = Phase::Cooldown;
                phaseTimer = 0.0f;
            }
            break;

        case Phase::Cooldown:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.12f) {
                phase = Phase::Idle;
                phaseTimer = 0.0f;
            }
            break;
    }

    // Double buffer sleep optimization
    if (m_dirty == 0) {
        m_hasFrame = false;
        return;
    }
    m_dirty--;
    m_hasFrame = true;

    // --- Render frame ---
    drawScene(w, h);

    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%s:%s", shownHH, shownMM);
    uint16_t crimson = matrix->color565(248, 56, 0);

    if (h >= 48) {
        // --- 256x64 Layout ---
        int simonX = 20;
        int simonY = 48 - 30; // on parapet floor

        if (phase == Phase::WhipWindup) {
            blitSprite(SIMON_WHIP_WINDUP, SIMON_WHIP_WINDUP_W, SIMON_WHIP_WINDUP_H, simonX - 8, simonY, true, false);
        } else if (phase == Phase::WhipStrike || phase == Phase::Impact) {
            blitSprite(SIMON_WHIP_STRIKE, SIMON_WHIP_STRIKE_W, SIMON_WHIP_STRIKE_H, simonX, simonY, true, false);
            // Whip cracks forward across the screen!
            blitSprite(WHIP_EXTENDED, WHIP_EXTENDED_W, WHIP_EXTENDED_H, simonX + 16, simonY + 12, true, false);
            if (phase == Phase::Impact) {
                // Spark at whip tip
                matrix->fillCircle(simonX + 16 + 40, simonY + 14, 3, 0xFFFF);
            }
        } else {
            const uint16_t* simonSprite = SIMON_WALK_FRAMES[walkFrame % 3];
            blitSprite(simonSprite, 16, 30, simonX, simonY, true, false);
        }

        // Blood Red Gothic Time Digits in Center
        drawGothicTime(88, 16, timeBuf, crimson, 5);
    } else {
        // --- 128x32 Layout ---
        int simonX = 6;
        int simonY = 0; // on brick floor (y=26-30 clamped to 0)

        if (phase == Phase::WhipWindup) {
            blitSprite(SIMON_WHIP_WINDUP, SIMON_WHIP_WINDUP_W, SIMON_WHIP_WINDUP_H, simonX, simonY, true, false);
        } else if (phase == Phase::WhipStrike || phase == Phase::Impact) {
            blitSprite(SIMON_WHIP_STRIKE, SIMON_WHIP_STRIKE_W, SIMON_WHIP_STRIKE_H, simonX, simonY, true, false);
            blitSprite(WHIP_EXTENDED, WHIP_EXTENDED_W, WHIP_EXTENDED_H, simonX + 16, simonY + 12, true, false);
            if (phase == Phase::Impact) {
                matrix->fillCircle(simonX + 16 + 40, simonY + 14, 2, 0xFFFF);
            }
        } else {
            const uint16_t* simonSprite = SIMON_WALK_FRAMES[walkFrame % 3];
            blitSprite(simonSprite, 16, 30, simonX, simonY, true, false);
        }

        // Blood Red Gothic Digits in Center
        drawGothicTime(42, 8, timeBuf, crimson, 3);
    }
}

void CastleClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    m_snapToNow = true;
    lastFrameMs = 0;
    phase = Phase::Idle;
    phaseTimer = 0.0f;
    batX = 140.0f;
}
