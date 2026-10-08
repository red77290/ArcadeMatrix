#include "MetalSlugClock.h"

#if !defined(HARDWARE_PROFILE_ESP32_DEV)

#include "MetalSlugAssets.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

using namespace MetalSlugAssets;

MetalSlugClock::MetalSlugClock(IDrawingSurface* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 10, 42, 0 };
}

void MetalSlugClock::draw(const TimeData& t) {
    storedTime = t;
}

void MetalSlugClock::blitSprite(const uint16_t* palette, const uint8_t* pixels, int w, int h, int x, int y, bool flipH) {
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
            uint8_t idx = pixels[row * w + srcCol];
            if (idx == 0) continue;
            matrix->drawPixel(px, py, palette[idx]);
        }
    }
}

void MetalSlugClock::blitBackdrop(const uint16_t* palette, const uint8_t* packedPixels, int w, int h) {
    if (!matrix || !palette || !packedPixels) return;
    const int panelW = matrix->width();
    const int panelH = matrix->height();
    int drawW = w < panelW ? w : panelW;
    int drawH = h < panelH ? h : panelH;

    for (int row = 0; row < drawH; row++) {
        int src = row * (w * 3 / 4);
        for (int col = 0; col < drawW; col += 4) {
            uint8_t b0 = pgm_read_byte(&packedPixels[src++]);
            uint8_t b1 = pgm_read_byte(&packedPixels[src++]);
            uint8_t b2 = pgm_read_byte(&packedPixels[src++]);

            uint8_t p0 = b0 & 0x3F;
            uint8_t p1 = ((b0 >> 6) & 0x03) | ((b1 & 0x0F) << 2);
            uint8_t p2 = ((b1 >> 4) & 0x0F) | ((b2 & 0x03) << 4);
            uint8_t p3 = (b2 >> 2) & 0x3F;

            matrix->drawPixel(col + 0, row, pgm_read_word(&palette[p0]));
            if (col + 1 < drawW) matrix->drawPixel(col + 1, row, pgm_read_word(&palette[p1]));
            if (col + 2 < drawW) matrix->drawPixel(col + 2, row, pgm_read_word(&palette[p2]));
            if (col + 3 < drawW) matrix->drawPixel(col + 3, row, pgm_read_word(&palette[p3]));
        }
    }
}

void MetalSlugClock::drawArcadeDigit(int x, int y, char c, uint16_t color, int scale) {
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

void MetalSlugClock::drawArcadeTime(int startX, int startY, const char* str, uint16_t color, int scale) {
    if (strlen(str) < 5) return;
    int digitW = 3 * scale;
    int digitGap = 1 * scale;
    int S = 2 * scale; // Symmetric space on both sides of colon dot

    int xH1 = startX;
    int xH2 = xH1 + digitW + digitGap;
    int xColon = xH2 + 2 * scale + S;
    int xM1 = xColon + 2 * scale + S;
    int xM2 = xM1 + digitW + digitGap;

    uint16_t shadowColor = matrix->color565(20, 10, 10);
    // Draw subtle drop shadow first
    drawArcadeDigit(xH1 + 1, startY + 1, str[0], shadowColor, scale);
    drawArcadeDigit(xH2 + 1, startY + 1, str[1], shadowColor, scale);
    drawArcadeDigit(xColon + 1, startY + 1, str[2], shadowColor, scale);
    drawArcadeDigit(xM1 + 1, startY + 1, str[3], shadowColor, scale);
    drawArcadeDigit(xM2 + 1, startY + 1, str[4], shadowColor, scale);

    // Draw main bright font
    drawArcadeDigit(xH1, startY, str[0], color, scale);
    drawArcadeDigit(xH2, startY, str[1], color, scale);
    drawArcadeDigit(xColon, startY, str[2], color, scale);
    drawArcadeDigit(xM1, startY, str[3], color, scale);
    drawArcadeDigit(xM2, startY, str[4], color, scale);
}

void MetalSlugClock::drawScene(int w, int h) {
    // 1. Draw 256x64 Night Desert Warzone Backdrop (randomized per rotation)
    const auto& bg = BG_BACKDROPS[currentBackdropIdx % NUM_BACKDROPS];
    blitBackdrop(bg.palette, bg.packedPixels, BG_BACKDROP_W, BG_BACKDROP_H);

    // 2. Flying Helicopter in upper sky
    if (heliX > -50.0f && heliX < 270.0f) {
        int hy = 8 + (int)(sinf(phaseTimer * 3.0f) * 3.0f);
        blitSprite(ENEMY_HELI_PAL, ENEMY_HELI_PIXELS, ENEMY_HELI_W, ENEMY_HELI_H, (int)heliX, hy, false);
    }

    // 3. Rebel Army Di-Cokka Tank
    int tankY = 64 - TANK_ALIVE_H; // Ground level (y=8..63)
    if ((phase == Phase::Explosion && explosionFrame >= 2) || phase == Phase::Victory) {
        // Charred smoking wreck
        blitSprite(TANK_DEAD_PAL, TANK_DEAD_PIXELS, TANK_DEAD_W, TANK_DEAD_H, (int)tankX, tankY + 2, false);
    } else {
        // Active battle tank
        blitSprite(TANK_ALIVE_PAL, TANK_ALIVE_PIXELS, TANK_ALIVE_W, TANK_ALIVE_H, (int)tankX, tankY, false);
    }

    // 4. Tank Cannon Shell
    if (tankBulletActive && tankBulletX > marcoX) {
        blitSprite(TANK_BULLET_PAL, TANK_BULLET_PIXELS, TANK_BULLET_W, TANK_BULLET_H, (int)tankBulletX, 42, false);
    }

    // 5. Marco Rossi animation
    int marcoY = 64 - 38; // Ground level for player
    if (phase == Phase::Idle || phase == Phase::Explosion) {
        // Steady alert commando stance holding rifle forward
        blitSprite(MARCO_IDLE_0_PAL, MARCO_IDLE_0_PIXELS, MARCO_IDLE_0_W, MARCO_IDLE_0_H, (int)marcoX, marcoY, false);
    } else if (phase == Phase::Firefight) {
        if (animFrame % 2 == 0) {
            blitSprite(MARCO_SHOOT_0_PAL, MARCO_SHOOT_0_PIXELS, MARCO_SHOOT_0_W, MARCO_SHOOT_0_H, (int)marcoX, marcoY + 1, false);
        } else {
            blitSprite(MARCO_SHOOT_1_PAL, MARCO_SHOOT_1_PIXELS, MARCO_SHOOT_1_W, MARCO_SHOOT_1_H, (int)marcoX, marcoY + 1, false);
        }

        // Blazing bullet tracers from muzzle to tank
        uint16_t tracerColor = matrix->color565(255, 240, 60);
        uint16_t orangeCore = matrix->color565(255, 120, 20);
        int muzzleX = (int)marcoX + MARCO_SHOOT_0_W;
        int muzzleY = marcoY + 10;
        if (animFrame % 2 == 1) {
            matrix->drawFastHLine(muzzleX + 4, muzzleY, 40, tracerColor);
            matrix->drawFastHLine(muzzleX + 10, muzzleY + 1, 30, orangeCore);
            matrix->drawFastHLine(muzzleX + 70, muzzleY - 2, 45, tracerColor);
        }
    } else if (phase == Phase::GrenadeAssault) {
        blitSprite(MARCO_GRENADE_PAL, MARCO_GRENADE_PIXELS, MARCO_GRENADE_W, MARCO_GRENADE_H, (int)marcoX, marcoY, false);
    } else if (phase == Phase::Victory) {
        blitSprite(MARCO_VICTORY_PAL, MARCO_VICTORY_PIXELS, MARCO_VICTORY_W, MARCO_VICTORY_H, (int)marcoX, marcoY - 5, false);
    }

    // 6. Flying Grenade Arc
    if (grenadeActive) {
        uint16_t grenadeColor = matrix->color565(180, 160, 40);
        uint16_t grenadeSpark = matrix->color565(255, 220, 80);
        matrix->fillRect((int)grenadeX, (int)grenadeY, 3, 3, grenadeColor);
        matrix->drawPixel((int)grenadeX + 1, (int)grenadeY + 1, grenadeSpark);
    }

    // 7. Fiery Explosive Fireballs
    if (phase == Phase::Explosion) {
        int ef = explosionFrame % 4;
        const auto& exp = EXPLOSION_SPRITES[ef];
        blitSprite(exp.palette, exp.pixels, exp.w, exp.h, (int)tankX + 15, 64 - exp.h - 4, false);
    }

    // 8. Authentic SNK Neo Geo Arcade HUD
    // Top banner scanline shade (semi-transparent feel)
    uint16_t hudBg = matrix->color565(8, 8, 16);
    matrix->fillRect(0, 0, w, 12, hudBg);
    matrix->drawFastHLine(0, 12, w, matrix->color565(40, 40, 70));

    // Player 1 Score: 1UP 001042
    uint16_t hudYellow = matrix->color565(248, 208, 0);
    uint16_t hudWhite = matrix->color565(255, 255, 255);
    uint16_t hudRed = matrix->color565(230, 40, 30);
    uint16_t hudCyan = matrix->color565(40, 220, 240);

    // "1UP" text
    drawArcadeDigit(6, 3, '1', hudRed, 1);
    // Draw 'U' and 'P' in 3x5
    matrix->fillRect(11, 3, 1, 5, hudRed);
    matrix->fillRect(13, 3, 1, 5, hudRed);
    matrix->fillRect(11, 7, 3, 1, hudRed);
    matrix->fillRect(16, 3, 1, 5, hudRed);
    matrix->fillRect(16, 3, 3, 1, hudRed);
    matrix->fillRect(18, 3, 1, 3, hudRed);
    matrix->fillRect(16, 5, 3, 1, hudRed);

    // Score based on shown clock time (e.g. 001042)
    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "00%s%s", shownHH, shownMM);
    int sx = 22;
    for (int i = 0; scoreStr[i] != '\0'; i++) {
        drawArcadeDigit(sx, 3, scoreStr[i], hudWhite, 1);
        sx += 4;
    }

    // Main Big Glowing Arcade Time in Center (e.g. 10:42)
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%s:%s", shownHH, shownMM);
    drawArcadeTime(106, 1, timeStr, hudYellow, 2);

    // Ammo / Arms indicator: ARMS: [H] 1042
    // "ARMS"
    drawArcadeDigit(185, 3, '4', hudCyan, 1); // placeholder icon
    // [H] badge
    matrix->fillRect(195, 2, 7, 7, matrix->color565(220, 160, 20));
    matrix->drawRect(195, 2, 7, 7, matrix->color565(255, 240, 80));
    matrix->fillRect(197, 4, 1, 3, matrix->color565(0, 0, 0));
    matrix->fillRect(199, 4, 1, 3, matrix->color565(0, 0, 0));
    matrix->fillRect(197, 5, 3, 1, matrix->color565(0, 0, 0));

    // Ammo count: shown minute
    int ax = 205;
    for (int i = 0; shownMM[i] != '\0'; i++) {
        drawArcadeDigit(ax, 3, shownMM[i], hudYellow, 1);
        ax += 4;
    }

    // Bomb icon and count: BOMB 10
    uint16_t bombCol = matrix->color565(240, 80, 20);
    matrix->fillCircle(235, 5, 2, bombCol);
    matrix->drawPixel(236, 3, matrix->color565(255, 255, 0));
    drawArcadeDigit(241, 3, '1', hudWhite, 1);
    drawArcadeDigit(246, 3, '0', hudWhite, 1);
}

void MetalSlugClock::update() {
    uint32_t now = millis();
    float dt = (lastFrameMs == 0) ? 0.033f : (now - lastFrameMs) / 1000.0f;
    if (dt <= 0.0f || dt > 0.5f) dt = 0.033f;
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
        animTimer = 0.0f;
        animFrame = 0;
        tankX = 185.0f;
        marcoX = 36.0f;
        heliX = -60.0f;
        tankBulletActive = false;
        grenadeActive = false;
        m_dirty = 2;
    }

    // Second tick
    if (lastSecond != storedTime.seconds) {
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }

    phaseTimer += dt;
    animTimer += dt;

    if (animTimer >= 0.12f) {
        animTimer -= 0.12f;
        animFrame++;
    }

    int currentMin = storedTime.minutes;

    // Helicopter cruises steadily across the sky
    heliX += 22.0f * dt;
    if (heliX > 280.0f) heliX = -60.0f;

    // Detect minute change for special victory blast sequence
    bool minuteChanged = (lastMinute != -1 && currentMin != lastMinute);

    switch (phase) {
        case Phase::Idle:
            tankBulletActive = false;
            grenadeActive = false;
            tankX = 185.0f;
            marcoX = 36.0f;
            if (minuteChanged) {
                phase = Phase::Firefight;
                phaseTimer = 0.0f;
                animTimer = 0.0f;
                animFrame = 0;
            }
            break;

        case Phase::Firefight:
            phaseTimer += dt;
            if (phaseTimer >= 0.8f) {
                phase = Phase::GrenadeAssault;
                phaseTimer = 0.0f;
                grenadeActive = true;
                grenadeX = marcoX + 24.0f;
                grenadeY = 64.0f - 35.0f;
                grenadeVx = (tankX + 15.0f - grenadeX) / 0.7f;
                grenadeVy = -85.0f;
            }
            break;

        case Phase::GrenadeAssault:
            phaseTimer += dt;
            if (grenadeActive) {
                grenadeX += grenadeVx * dt;
                grenadeVy += 220.0f * dt; // gravity
                grenadeY += grenadeVy * dt;

                if (grenadeX >= tankX + 15.0f || grenadeY >= 48.0f || phaseTimer >= 1.2f) {
                    grenadeActive = false;
                    phase = Phase::Explosion;
                    phaseTimer = 0.0f;
                    explosionFrame = 0;
                    explosionTimer = 0.0f;
                    snprintf(shownHH, sizeof(shownHH), "%02d", storedTime.hours);
                    snprintf(shownMM, sizeof(shownMM), "%02d", storedTime.minutes);
                    lastMinute = currentMin; // Minute flips upon explosion impact!
                }
            }
            break;

        case Phase::Explosion:
            phaseTimer += dt;
            explosionTimer += dt;
            if (explosionTimer >= 0.12f) {
                explosionTimer -= 0.12f;
                if (explosionFrame < 3) {
                    explosionFrame++;
                }
            }
            if (phaseTimer >= 0.65f) {
                phase = Phase::Victory;
                phaseTimer = 0.0f;
            }
            break;

        case Phase::Victory:
            phaseTimer += dt;
            if (phaseTimer >= 1.4f) {
                phase = Phase::Idle;
                phaseTimer = 0.0f;
            }
            break;
    }

    if (matrix) {
        drawScene(matrix->width(), matrix->height());
    }

    m_dirty = 2;
    m_hasFrame = true;
}

void MetalSlugClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    m_hasFrame = true;
    m_snapToNow = true;
}

#endif // !HARDWARE_PROFILE_ESP32_DEV
