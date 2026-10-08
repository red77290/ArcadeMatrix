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

void CastleClock::blitSprite(const uint16_t* palette, const uint8_t* pixels, int w, int h, int x, int y, bool flipH) {
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
    if (strlen(str) < 5) return;
    int digitW = 3 * scale;
    int digitGap = 1 * scale;
    int S = 2 * scale; // Exact symmetric spacing on both sides of colon

    int xH1 = startX;
    int xH2 = xH1 + digitW + digitGap;
    int xColon = xH2 + 2 * scale + S;
    int xM1 = xColon + 2 * scale + S;
    int xM2 = xM1 + digitW + digitGap;

    drawGothicDigit(xH1, startY, str[0], color, scale);
    drawGothicDigit(xH2, startY, str[1], color, scale);
    drawGothicDigit(xColon, startY, str[2], color, scale);
    drawGothicDigit(xM1, startY, str[3], color, scale);
    drawGothicDigit(xM2, startY, str[4], color, scale);
}

void CastleClock::drawScene(int w, int h) {
    const bool isPortrait = (h > w);

    if (isPortrait) {
        // --- Portrait (64x256 / 64x128) Dracula Castle Tower ---
        uint16_t nightSky = matrix->color565(16, 0, 24);
        matrix->fillRect(0, 0, w, h, nightSky);

        // Blood Moon top center (x=32, y=22)
        uint16_t moonOuter = matrix->color565(220, 40, 20);
        uint16_t moonInner = matrix->color565(240, 80, 40);
        matrix->fillCircle(32, 22, 14, moonOuter);
        matrix->fillCircle(30, 20, 11, moonInner);

        // Flying Vampire Bat across moon
        const uint16_t* batPal = BAT_PALS[batFrame % 2];
        const uint8_t* batPix = BAT_PIXELS[batFrame % 2];
        blitSprite(batPal, batPix, BAT_F0_W, BAT_F0_H, (int)batX, 14, false);

        // Stone battlement / cornice at y=56
        uint16_t stoneBase = matrix->color565(80, 80, 96);
        uint16_t stoneLight = matrix->color565(140, 140, 160);
        uint16_t stoneDark = matrix->color565(36, 36, 48);

        matrix->drawFastHLine(0, 56, w, stoneLight);
        matrix->fillRect(0, 57, w, 4, stoneBase);
        for (int x = 0; x < w; x += 16) {
            matrix->drawFastVLine(x, 57, 4, stoneDark);
        }

        // Stepped stone staircase leading up towards candle
        for (int i = 0; i < 7; i++) {
            int stepX = i * 8;
            int stepY = 160 - i * 10;
            if (stepY + 4 < h) {
                matrix->fillRect(stepX, stepY, 16, 4, stoneBase);
                matrix->drawFastHLine(stepX, stepY, 16, stoneLight);
            }
        }

        // Candle torch on right platform (x=46, y=86)
        uint16_t torchMetal = matrix->color565(140, 100, 50);
        matrix->fillRect(46, 86, 4, 10, torchMetal);
        matrix->fillRect(44, 82, 8, 4, torchMetal);

        // Animated candle flame
        const uint16_t* flamePal = FLAME_PALS[flameFrame % 3];
        const uint8_t* flamePix = FLAME_PIXELS[flameFrame % 3];
        blitSprite(flamePal, flamePix, FLAME_F0_W, FLAME_F0_H, 44, 72, false);
    } else if (h >= 48) {
        // --- 256x64 Dracula's Castle Parapets ---
        uint16_t nightSky = matrix->color565(16, 0, 24);
        matrix->fillRect(0, 0, w, 48, nightSky);

        // Blood Moon on right
        uint16_t moonOuter = matrix->color565(220, 40, 20);
        uint16_t moonInner = matrix->color565(240, 80, 40);
        matrix->fillCircle(216, 22, 18, moonOuter);
        matrix->fillCircle(214, 20, 15, moonInner);

        // Parapets at y=48..63
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
        const uint16_t* flamePal = FLAME_PALS[flameFrame % 3];
        const uint8_t* flamePix = FLAME_PIXELS[flameFrame % 3];
        blitSprite(flamePal, flamePix, FLAME_F0_W, FLAME_F0_H, 238, 20, false);

        // Flying Vampire Bat flapping across blood moon
        const uint16_t* batPal = BAT_PALS[batFrame % 2];
        const uint8_t* batPix = BAT_PIXELS[batFrame % 2];
        blitSprite(batPal, batPix, BAT_F0_W, BAT_F0_H, (int)batX, 10, false);
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
        const uint16_t* flamePal = FLAME_PALS[flameFrame % 3];
        const uint8_t* flamePix = FLAME_PIXELS[flameFrame % 3];
        blitSprite(flamePal, flamePix, FLAME_F0_W, FLAME_F0_H, 113, 10, false);

        // Flying Bat
        const uint16_t* batPal = BAT_PALS[batFrame % 2];
        const uint8_t* batPix = BAT_PIXELS[batFrame % 2];
        blitSprite(batPal, batPix, BAT_F0_W, BAT_F0_H, (int)batX, 4, false);
    }
}

void CastleClock::update() {
    if (!matrix) return;
    const int w = matrix->width();
    const int h = matrix->height();
    const bool isPortrait = (h > w);

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
        batX = (float)w;
        simonX = -40.0f;
        m_dirty = 2;
    }

    // Second tick
    if (lastSecond != storedTime.seconds) {
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }

    // Minute transition -> Simon enters to strike!
    if (lastMinute != storedTime.minutes && phase == Phase::Idle) {
        phase = Phase::HeroEnter;
        phaseTimer = 0.0f;
        if (isPortrait) {
            simonX = 4.0f;
            simonY = 160.0f;
        } else {
            simonX = -20.0f;
            simonY = (h >= 48) ? (48.0f - 30.0f) : 0.0f;
        }
        m_dirty = 2;
    }

    // Flame flicker (every 120ms)
    flameTimer += dt;
    if (flameTimer >= 0.12f) {
        flameTimer = 0.0f;
        flameFrame = (flameFrame + 1) % 3;
        m_dirty = 2;
    }

    // Bat flap and glide across sky
    batTimer += dt;
    if (batTimer >= 0.18f) {
        batTimer = 0.0f;
        batFrame = (batFrame + 1) % 2;
        m_dirty = 2;
    }
    batX -= (isPortrait ? 18.0f : 25.0f) * dt;
    if (batX < -20.0f) batX = (float)w + 10.0f;

    // Walk frame step cycle (only animates when actually moving)
    if (phase == Phase::HeroEnter || phase == Phase::HeroExit) {
        walkTimer += dt;
        if (walkTimer >= 0.18f) {
            walkTimer = 0.0f;
            walkFrame++;
            m_dirty = 2;
        }
    }

    // Minute reward state machine
    switch (phase) {
        case Phase::Idle:
            break;

        case Phase::HeroEnter:
            m_dirty = 2;
            if (isPortrait) {
                // Simon climbs stairs upwards from behind
                simonX += 12.0f * dt;
                simonY -= 15.0f * dt;
                if (simonY <= 96.0f) {
                    phase = Phase::WhipWindup;
                    phaseTimer = 0.0f;
                }
            } else {
                // Simon advances forward into striking position
                float targetX = (h >= 48) ? 20.0f : 6.0f;
                simonX += 65.0f * dt;
                if (simonX >= targetX) {
                    simonX = targetX;
                    phase = Phase::WhipWindup;
                    phaseTimer = 0.0f;
                }
            }
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
            if (phaseTimer >= 0.18f) {
                phase = Phase::HeroExit;
                phaseTimer = 0.0f;
            }
            break;

        case Phase::HeroExit:
            m_dirty = 2;
            if (isPortrait) {
                // Simon continues climbing upwards and exits
                simonX += 14.0f * dt;
                simonY -= 18.0f * dt;
                if (simonY < -35.0f || simonX > w) {
                    phase = Phase::Idle;
                    phaseTimer = 0.0f;
                }
            } else {
                // Simon walks forward past the clock
                simonX += 75.0f * dt;
                if (simonX > (float)w) {
                    phase = Phase::Idle;
                    phaseTimer = 0.0f;
                }
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

    if (isPortrait) {
        // --- Portrait (64x256 / 64x128): Blood Red Gothic Digits Centered (scale 2 = 44px wide) ---
        drawGothicTime(10, 38, timeBuf, crimson, 2);

        // Simon Belmont rendered during minute reward sequence
        if (phase != Phase::Idle) {
            if (phase == Phase::WhipWindup) {
                blitSprite(SIMON_WHIP_WINDUP_PAL, SIMON_WHIP_WINDUP_PIXELS, SIMON_WHIP_WINDUP_W, SIMON_WHIP_WINDUP_H, (int)simonX, (int)simonY, false);
            } else if (phase == Phase::WhipStrike || phase == Phase::Impact) {
                blitSprite(SIMON_WHIP_STRIKE_PAL, SIMON_WHIP_STRIKE_PIXELS, SIMON_WHIP_STRIKE_W, SIMON_WHIP_STRIKE_H, (int)simonX, (int)simonY, false);
                blitSprite(WHIP_EXTENDED_PAL, WHIP_EXTENDED_PIXELS, WHIP_EXTENDED_W, WHIP_EXTENDED_H, (int)simonX + 16, (int)simonY + 12, false);
                if (phase == Phase::Impact) {
                    matrix->fillCircle((int)simonX + 16 + 40, (int)simonY + 14, 3, 0xFFFF);
                }
            } else {
                // Ascending stairs seen from behind
                int sf = walkFrame % 2;
                const uint16_t* pal = SIMON_STAIR_BACK_PALS[sf];
                const uint8_t* pix = SIMON_STAIR_BACK_PIXELS[sf];
                int sw = (sf == 0) ? SIMON_STAIR_BACK_0_W : SIMON_STAIR_BACK_1_W;
                blitSprite(pal, pix, sw, 32, (int)simonX, (int)simonY, false);
            }
        }
    } else if (h >= 48) {
        // --- 256x64 Layout: Blood Red Gothic Digits Centered (scale 4 = 88px wide) ---
        drawGothicTime(84, 16, timeBuf, crimson, 4);

        if (phase != Phase::Idle) {
            if (phase == Phase::WhipWindup) {
                blitSprite(SIMON_WHIP_WINDUP_PAL, SIMON_WHIP_WINDUP_PIXELS, SIMON_WHIP_WINDUP_W, SIMON_WHIP_WINDUP_H, (int)simonX - 8, (int)simonY, false);
            } else if (phase == Phase::WhipStrike || phase == Phase::Impact) {
                blitSprite(SIMON_WHIP_STRIKE_PAL, SIMON_WHIP_STRIKE_PIXELS, SIMON_WHIP_STRIKE_W, SIMON_WHIP_STRIKE_H, (int)simonX, (int)simonY, false);
                blitSprite(WHIP_EXTENDED_PAL, WHIP_EXTENDED_PIXELS, WHIP_EXTENDED_W, WHIP_EXTENDED_H, (int)simonX + 16, (int)simonY + 12, false);
                if (phase == Phase::Impact) {
                    matrix->fillCircle((int)simonX + 16 + 40, (int)simonY + 14, 3, 0xFFFF);
                }
            } else {
                int wf = walkFrame % 3;
                const uint16_t* pal = SIMON_WALK_PALS[wf];
                const uint8_t* pix = SIMON_WALK_PIXELS[wf];
                blitSprite(pal, pix, 16, 30, (int)simonX, (int)simonY, false);
            }
        }
    } else {
        // --- 128x32 Layout: Blood Red Gothic Digits Centered (scale 3 = 66px wide) ---
        drawGothicTime(31, 8, timeBuf, crimson, 3);

        if (phase != Phase::Idle) {
            if (phase == Phase::WhipWindup) {
                blitSprite(SIMON_WHIP_WINDUP_PAL, SIMON_WHIP_WINDUP_PIXELS, SIMON_WHIP_WINDUP_W, SIMON_WHIP_WINDUP_H, (int)simonX, (int)simonY, false);
            } else if (phase == Phase::WhipStrike || phase == Phase::Impact) {
                blitSprite(SIMON_WHIP_STRIKE_PAL, SIMON_WHIP_STRIKE_PIXELS, SIMON_WHIP_STRIKE_W, SIMON_WHIP_STRIKE_H, (int)simonX, (int)simonY, false);
                blitSprite(WHIP_EXTENDED_PAL, WHIP_EXTENDED_PIXELS, WHIP_EXTENDED_W, WHIP_EXTENDED_H, (int)simonX + 16, (int)simonY + 12, false);
                if (phase == Phase::Impact) {
                    matrix->fillCircle((int)simonX + 16 + 40, (int)simonY + 14, 2, 0xFFFF);
                }
            } else {
                int wf = walkFrame % 3;
                const uint16_t* pal = SIMON_WALK_PALS[wf];
                const uint8_t* pix = SIMON_WALK_PIXELS[wf];
                blitSprite(pal, pix, 16, 30, (int)simonX, (int)simonY, false);
            }
        }
    }
}

void CastleClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    m_snapToNow = true;
    lastFrameMs = 0;
    phase = Phase::Idle;
    phaseTimer = 0.0f;
    batX = 140.0f;
    simonX = -40.0f;
}
