#include "SonicClock.h"
#include "SonicAssets.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

using namespace SonicAssets;

SonicClock::SonicClock(IDrawingSurface* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 0, 0, 0 };
}

void SonicClock::draw(const TimeData& t) {
    storedTime = t;
}

void SonicClock::blitSprite(const uint16_t* palette, const uint8_t* pixels, int w, int h, int x, int y, bool flipH) {
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
            if (idx == 0) continue; // transparent index
            matrix->drawPixel(px, py, pgm_read_word(&palette[idx]));
        }
    }
}

void SonicClock::drawArcadeDigit(int x, int y, char c, uint16_t color, int scale) {
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

void SonicClock::drawArcadeTime(int startX, int startY, const char* str, uint16_t color, int scale) {
    if (strlen(str) < 5) return;
    int digitW = 3 * scale;
    int digitGap = 1 * scale;
    int S = 2 * scale; // Exact symmetric spacing on both sides of colon

    int xH1 = startX;
    int xH2 = xH1 + digitW + digitGap;
    int xColon = xH2 + 2 * scale + S;
    int xM1 = xColon + 2 * scale + S;
    int xM2 = xM1 + digitW + digitGap;

    drawArcadeDigit(xH1, startY, str[0], color, scale);
    drawArcadeDigit(xH2, startY, str[1], color, scale);
    drawArcadeDigit(xColon, startY, str[2], color, scale);
    drawArcadeDigit(xM1, startY, str[3], color, scale);
    drawArcadeDigit(xM2, startY, str[4], color, scale);
}

void SonicClock::drawGreenHillGround(int w, int h, int gHeight) {
    int gTop = h - gHeight;
    uint16_t grassLight = matrix->color565(0, 224, 0);
    uint16_t grassMid = matrix->color565(0, 160, 0);
    uint16_t grassDark = matrix->color565(0, 100, 0);
    uint16_t soilLight = matrix->color565(208, 112, 16);
    uint16_t soilDark = matrix->color565(144, 64, 0);

    // Wavy green grass on top
    matrix->drawFastHLine(0, gTop, w, grassLight);
    matrix->drawFastHLine(0, gTop + 1, w, grassMid);
    for (int x = 0; x < w; x++) {
        if ((x % 4) == 0 || (x % 4) == 1) {
            matrix->drawPixel(x, gTop + 2, grassDark);
        }
    }

    // Checkered brown soil
    for (int x = 0; x < w; x += 8) {
        int bw = ((x + 8) > w) ? (w - x) : 8;
        for (int y = gTop + 3; y < h; y += 6) {
            int bh = ((y + 6) > h) ? (h - y) : 6;
            bool alt = (((x / 8) + (y / 6)) % 2 == 0);
            matrix->fillRect(x, y, bw, bh, alt ? soilLight : soilDark);
        }
    }
}

void SonicClock::drawCheckeredPlatform(int px, int py, int pw, int ph) {
    uint16_t grassLight = matrix->color565(0, 224, 0);
    uint16_t grassMid = matrix->color565(0, 160, 0);
    uint16_t soilLight = matrix->color565(208, 112, 16);
    uint16_t soilDark = matrix->color565(144, 64, 0);

    // Green grass top
    matrix->drawFastHLine(px, py, pw, grassLight);
    matrix->drawFastHLine(px, py + 1, pw, grassMid);

    // Checkered soil
    for (int x = px; x < px + pw; x += 8) {
        int bw = ((x + 8) > (px + pw)) ? ((px + pw) - x) : 8;
        for (int y = py + 2; y < py + ph; y += 4) {
            int bh = ((y + 4) > (py + ph)) ? ((py + ph) - y) : 4;
            bool alt = (((x / 8) + (y / 4)) % 2 == 0);
            matrix->fillRect(x, y, bw, bh, alt ? soilLight : soilDark);
        }
    }
}

void SonicClock::drawScene(int w, int h) {
    uint16_t skyBlue = matrix->color565(64, 160, 248);
    const bool isPortrait = (h > w);

    if (isPortrait) {
        // --- Portrait (64x256 / 64x128) ---
        matrix->fillRect(0, 0, w, h, skyBlue);

        // Distant mountains
        uint16_t mountainCol = matrix->color565(32, 144, 96);
        matrix->fillTriangle(0, 48, 16, 28, 32, 48, mountainCol);
        matrix->fillTriangle(24, 48, 44, 24, 64, 48, mountainCol);

        // 3 Stepped Green Hill floating platforms leading up to the Item Monitor Box
        // Platform 3 (bottom): y=190, from x=16 to x=56 (width 40)
        drawCheckeredPlatform(16, 190, 40, 6);
        // Platform 2 (middle): y=130, from x=8 to x=48 (width 40)
        drawCheckeredPlatform(8, 130, 40, 6);
        // Platform 1 (top): y=70, from x=8 to x=56 (width 48)
        drawCheckeredPlatform(8, 70, 48, 6);

        // Bottom ground (16px high)
        drawGreenHillGround(w, h, 16);
    } else if (h >= 48) {
        // --- 256x64 Green Hill Zone ---
        matrix->fillRect(0, 0, w, 48, skyBlue);

        // Distant mountains
        uint16_t mountainCol = matrix->color565(32, 144, 96);
        for (int x = 0; x < w; x += 40) {
            matrix->fillTriangle(x, 48, x + 20, 24, x + 40, 48, mountainCol);
        }

        // Green Hill ground (16px high)
        drawGreenHillGround(w, h, 16);
    } else {
        // --- 128x32 Green Hill Close-Up ---
        matrix->fillRect(0, 0, w, 23, skyBlue);

        // Green Hill ground (9px high)
        drawGreenHillGround(w, h, 9);

        // Palm tree on right (x=114..126)
        matrix->drawLine(120, 10, 118, 23, matrix->color565(160, 112, 48));
        matrix->drawLine(121, 10, 119, 23, matrix->color565(160, 112, 48));
        uint16_t palmGreen = matrix->color565(0, 180, 0);
        matrix->fillTriangle(114, 8, 120, 6, 126, 8, palmGreen);
        matrix->fillTriangle(114, 8, 120, 11, 126, 8, palmGreen);
    }
}

void SonicClock::update() {
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
        ringFrame = 0;
        ringTimer = 0.0f;
        motobugTimer = 0.0f;
        phase = Phase::Idle;
        phaseTimer = 0.0f;
        sonicX = -40.0f;
        m_dirty = 2;
    }

    // Second tick
    if (lastSecond != storedTime.seconds) {
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }

    // Minute transition -> Sonic dashes in!
    if (lastMinute != storedTime.minutes && phase == Phase::Idle) {
        phase = Phase::HeroEnter;
        phaseTimer = 0.0f;
        if (isPortrait) {
            sonicX = -20.0f;
            sonicY = (float)(h - 16 - 24);
            climbStage = 0;
            jumpT = 0.0f;
        } else {
            sonicX = -30.0f;
            sonicY = (h >= 48) ? (64.0f - 16.0f - 39.0f) : 0.0f;
        }
        m_dirty = 2;
    }

    // Golden ring rotation (animates at ~120ms per frame)
    ringTimer += dt;
    if (ringTimer >= 0.12f) {
        ringTimer = 0.0f;
        ringFrame = (ringFrame + 1) % 4;
        m_dirty = 2;
    }

    // Motobug patrol timer
    motobugTimer += dt;
    float motoOffset = sinf(motobugTimer * 1.5f) * (isPortrait ? 8.0f : 14.0f);
    bool motoFlip = (cosf(motobugTimer * 1.5f) < 0.0f);

    // Running cycle timer (animates fast when moving)
    if (phase == Phase::HeroEnter || phase == Phase::HeroExit) {
        runTimer += dt;
        if (runTimer >= 0.07f) {
            runTimer = 0.0f;
            runFrame = (runFrame + 1) % 4;
            m_dirty = 2;
        }
    }

    // Spin ball animation timer
    if (phase == Phase::JumpStrike || phase == Phase::Impact || (isPortrait && phase == Phase::HeroEnter && (climbStage == 1 || climbStage == 3 || climbStage == 5))) {
        ballTimer += dt;
        if (ballTimer >= 0.05f) {
            ballTimer = 0.0f;
            ballFrame = (ballFrame + 1) % 4;
            m_dirty = 2;
        }
    }

    // State machine updates
    switch (phase) {
        case Phase::Idle:
            break;

        case Phase::HeroEnter:
            m_dirty = 2;
            if (isPortrait) {
                const float groundY = (float)(h - 16 - 24);
                if (climbStage == 0) {
                    // Stage 0: Dashes in on bottom ground
                    sonicX += 80.0f * dt;
                    sonicY = groundY;
                    if (sonicX >= 14.0f) {
                        climbStage = 1;
                        jumpT = 0.0f;
                    }
                } else if (climbStage == 1) {
                    // Stage 1: Spin jump from Ground to Platform 3 (y=190)
                    jumpT += dt * 2.8f;
                    float t = jumpT < 1.0f ? jumpT : 1.0f;
                    sonicX = 14.0f + (36.0f - 14.0f) * t;
                    float baseY = groundY + (166.0f - groundY) * t;
                    float arc = 4.0f * 14.0f * t * (1.0f - t);
                    sonicY = baseY - arc;
                    if (jumpT >= 1.0f) {
                        jumpT = 0.0f;
                        sonicY = 166.0f;
                        climbStage = 2;
                    }
                } else if (climbStage == 2) {
                    // Stage 2: Runs left on Platform 3
                    sonicX -= 65.0f * dt;
                    sonicY = 166.0f;
                    if (sonicX <= 30.0f) {
                        climbStage = 3;
                        jumpT = 0.0f;
                    }
                } else if (climbStage == 3) {
                    // Stage 3: Spin jump from Platform 3 to Platform 2 (y=130)
                    jumpT += dt * 2.8f;
                    float t = jumpT < 1.0f ? jumpT : 1.0f;
                    sonicX = 30.0f + (16.0f - 30.0f) * t;
                    float baseY = 166.0f + (106.0f - 166.0f) * t;
                    float arc = 4.0f * 14.0f * t * (1.0f - t);
                    sonicY = baseY - arc;
                    if (jumpT >= 1.0f) {
                        jumpT = 0.0f;
                        sonicY = 106.0f;
                        climbStage = 4;
                    }
                } else if (climbStage == 4) {
                    // Stage 4: Runs right on Platform 2
                    sonicX += 65.0f * dt;
                    sonicY = 106.0f;
                    if (sonicX >= 24.0f) {
                        climbStage = 5;
                        jumpT = 0.0f;
                    }
                } else if (climbStage == 5) {
                    // Stage 5: Spin jump from Platform 2 to Platform 1 (y=70)
                    jumpT += dt * 2.8f;
                    float t = jumpT < 1.0f ? jumpT : 1.0f;
                    sonicX = 24.0f + (28.0f - 24.0f) * t;
                    float baseY = 106.0f + (46.0f - 106.0f) * t;
                    float arc = 4.0f * 14.0f * t * (1.0f - t);
                    sonicY = baseY - arc;
                    if (jumpT >= 1.0f) {
                        jumpT = 0.0f;
                        sonicY = 46.0f;
                        climbStage = 6;
                    }
                } else if (climbStage == 6) {
                    // Stage 6: Dashing towards the Item Monitor Box on Platform 1
                    sonicX -= 65.0f * dt;
                    sonicY = 46.0f;
                    if (sonicX <= 26.0f) {
                        sonicX = 26.0f;
                        phase = Phase::JumpStrike;
                        phaseTimer = 0.0f;
                    }
                }
            } else {
                // Dashes forward into view
                float targetX = (h >= 48) ? 36.0f : 10.0f;
                sonicX += 130.0f * dt;
                if (sonicX >= targetX) {
                    sonicX = targetX;
                    phase = Phase::JumpStrike;
                    phaseTimer = 0.0f;
                }
            }
            break;

        case Phase::JumpStrike:
            phaseTimer += dt;
            m_dirty = 2;
            if (phaseTimer >= 0.22f) {
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
                // Sprints right along Platform 1 to exit offscreen
                sonicX += 85.0f * dt;
                sonicY = 70.0f - 24.0f;
                if (sonicX > (float)w + 24.0f) {
                    phase = Phase::Idle;
                    phaseTimer = 0.0f;
                    m_dirty = 3;
                }
            } else {
                sonicX += 150.0f * dt;
                if (sonicX > (float)w + 24.0f) {
                    phase = Phase::Idle;
                    phaseTimer = 0.0f;
                    m_dirty = 3;
                }
            }
            break;
    }

    // Double buffer sleep check
    if (m_dirty == 0) {
        m_hasFrame = false;
        return;
    }
    m_dirty--;
    m_hasFrame = true;

    // --- Render Frame ---
    drawScene(w, h);

    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%s:%s", shownHH, shownMM);
    uint16_t yellowTime = matrix->color565(252, 224, 0);

    const uint16_t* ringPal = SONIC_RING_PALS[ringFrame % 4];
    const uint8_t* ringPix = SONIC_RING_PIXELS[ringFrame % 4];

    if (isPortrait) {
        // --- Portrait (64x256 / 64x128) ---
        // Symmetrically centered time at top (scale 2 = 44px wide, centered at 10)
        drawArcadeTime(10, 16, timeBuf, yellowTime, 2);

        // Monitor box sitting squarely on Platform 1 (y=70, monitor height 32 -> y=38)
        blitSprite(SONIC_ITEM_BOX_PAL, SONIC_ITEM_BOX_PIXELS, SONIC_ITEM_BOX_W, SONIC_ITEM_BOX_H, 16, 70 - 32, false);

        // Rotating golden rings floating above platforms
        blitSprite(ringPal, ringPix, 16, 16, 20, 110, false);
        blitSprite(ringPal, ringPix, 16, 16, 28, 170, false);

        // Motobug patrol on bottom ground
        int motoX = 12 + (int)motoOffset;
        blitSprite(SONIC_MOTOBUG_PAL, SONIC_MOTOBUG_PIXELS, SONIC_MOTOBUG_W, SONIC_MOTOBUG_H, motoX, h - 16 - 29, motoFlip);

        // Sonic during minute reward sequence
        if (phase != Phase::Idle) {
            bool isAirborne = (phase == Phase::JumpStrike || phase == Phase::Impact || (phase == Phase::HeroEnter && (climbStage == 1 || climbStage == 3 || climbStage == 5)));
            if (isAirborne) {
                blitSprite(SONIC_MD_IDLE_PAL, SONIC_MD_BALL_PIXELS[ballFrame], SONIC_MD_BALL_0_W, SONIC_MD_BALL_0_H, (int)sonicX, (int)sonicY, false);
                if (phase == Phase::Impact) {
                    matrix->fillCircle(32, 54, 5, 0xFFFF);
                }
            } else {
                blitSprite(SONIC_SMS_IDLE_PAL, SONIC_SMS_RUN_PIXELS[runFrame], SONIC_SMS_RUN_0_W, SONIC_SMS_RUN_0_H, (int)sonicX, (int)sonicY, false);
            }
        }
    } else if (h >= 48) {
        // --- 256x64 Layout ---
        // Item Monitor box (x=52, y=64-16-32=16)
        blitSprite(SONIC_ITEM_BOX_PAL, SONIC_ITEM_BOX_PIXELS, SONIC_ITEM_BOX_W, SONIC_ITEM_BOX_H, 52, 16, false);

        // Symmetrically Centered Arcade Time (scale 4 = 88px wide, centered at 84)
        drawArcadeTime(84, 15, timeBuf, yellowTime, 4);

        // Rotating golden ring (x=178, y=22)
        blitSprite(ringPal, ringPix, 16, 16, 178, 22, false);

        // Badnik Motobug on far right with smooth patrol
        int motoX = 208 + (int)motoOffset;
        blitSprite(SONIC_MOTOBUG_PAL, SONIC_MOTOBUG_PIXELS, SONIC_MOTOBUG_W, SONIC_MOTOBUG_H, motoX, 64 - 16 - 29, motoFlip);

        // Sonic enters only on minute change to smash monitor
        if (phase != Phase::Idle) {
            if (phase == Phase::JumpStrike || phase == Phase::Impact) {
                // Curled into high-speed spin ball
                blitSprite(SONIC_MD_IDLE_PAL, SONIC_MD_BALL_PIXELS[ballFrame], SONIC_MD_BALL_0_W, SONIC_MD_BALL_0_H, (int)sonicX + 4, (int)sonicY + 12, false);
                if (phase == Phase::Impact) {
                    // Spark on monitor
                    matrix->fillCircle(52 + 16, 16 + 16, 5, 0xFFFF);
                }
            } else {
                // Running sprint cycle
                blitSprite(SONIC_MD_IDLE_PAL, SONIC_MD_RUN_PIXELS[runFrame], SONIC_MD_RUN_0_W, SONIC_MD_RUN_0_H, (int)sonicX, (int)sonicY, false);
            }
        }
    } else {
        // --- 128x32 Layout ---
        // Rotating golden ring on left (x=10, y=6)
        blitSprite(ringPal, ringPix, 16, 16, 10, 6, false);

        // Symmetrically Centered Arcade Time (scale 3 = 66px wide, centered at 31)
        drawArcadeTime(31, 6, timeBuf, yellowTime, 3);

        // Sonic enters only on minute change
        if (phase != Phase::Idle) {
            if (phase == Phase::JumpStrike || phase == Phase::Impact) {
                blitSprite(SONIC_MD_IDLE_PAL, SONIC_MD_BALL_PIXELS[ballFrame], SONIC_MD_BALL_0_W, SONIC_MD_BALL_0_H, (int)sonicX, (int)sonicY, false);
                if (phase == Phase::Impact) {
                    matrix->fillCircle(20, 14, 3, 0xFFFF);
                }
            } else {
                blitSprite(SONIC_SMS_IDLE_PAL, SONIC_SMS_RUN_PIXELS[runFrame], SONIC_SMS_RUN_0_W, SONIC_SMS_RUN_0_H, (int)sonicX, (int)sonicY, false);
            }
        }
    }
}

void SonicClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    m_snapToNow = true;
    lastFrameMs = 0;
    motobugTimer = 0.0f;
    phase = Phase::Idle;
    phaseTimer = 0.0f;
    sonicX = -40.0f;
}
