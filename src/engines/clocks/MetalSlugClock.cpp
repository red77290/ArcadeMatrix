#include "MetalSlugClock.h"
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

void MetalSlugClock::blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent, bool flipH) {
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

void MetalSlugClock::blitBackdrop(const uint16_t* data, int w, int h) {
    if (!matrix || !data) return;
    const int panelW = matrix->width();
    const int panelH = matrix->height();
    int drawW = w < panelW ? w : panelW;
    int drawH = h < panelH ? h : panelH;

    for (int row = 0; row < drawH; row++) {
        for (int col = 0; col < drawW; col++) {
            matrix->drawPixel(col, row, data[row * w + col]);
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
    // Draw subtle drop shadow first
    uint16_t shadowColor = matrix->color565(20, 10, 10);
    int x = startX + 1;
    int y = startY + 1;
    for (int i = 0; str[i] != '\0'; i++) {
        char ch = str[i];
        drawArcadeDigit(x, y, ch, shadowColor, scale);
        int gw = (ch == ':') ? 1 : 3;
        x += (gw + 1) * scale;
    }

    // Draw main bright font
    x = startX;
    y = startY;
    for (int i = 0; str[i] != '\0'; i++) {
        char ch = str[i];
        drawArcadeDigit(x, y, ch, color, scale);
        int gw = (ch == ':') ? 1 : 3;
        x += (gw + 1) * scale;
    }
}

void MetalSlugClock::drawScene(int w, int h) {
    // 1. Draw 256x64 Night Desert Warzone Backdrop
    blitBackdrop(BG_DESERT_256x64, BG_DESERT_256x64_W, BG_DESERT_256x64_H);

    // 2. Flying Helicopter in upper sky
    if (heliX > -50.0f && heliX < 270.0f) {
        int hy = 8 + (int)(sinf(phaseTimer * 3.0f) * 3.0f);
        blitSprite(ENEMY_HELI, ENEMY_HELI_W, ENEMY_HELI_H, (int)heliX, hy, true, false);
    }

    // 3. Rebel Army Di-Cokka Tank
    int tankY = 64 - TANK_ALIVE_H; // Ground level (y=8..63)
    if (phase == Phase::ExplosionVictory && explosionFrame >= 2) {
        // Charred smoking wreck
        blitSprite(TANK_DEAD, TANK_DEAD_W, TANK_DEAD_H, (int)tankX, tankY + 2, true, false);
    } else {
        // Active battle tank
        blitSprite(TANK_ALIVE, TANK_ALIVE_W, TANK_ALIVE_H, (int)tankX, tankY, true, false);
    }

    // 4. Tank Cannon Shell
    if (tankBulletActive && tankBulletX > marcoX) {
        blitSprite(TANK_BULLET, TANK_BULLET_W, TANK_BULLET_H, (int)tankBulletX, 42, true, false);
    }

    // 5. Marco Rossi animation
    int marcoY = 64 - 38; // Ground level for player
    if (phase == Phase::Patrol) {
        const uint16_t* walk = (animFrame % 2 == 0) ? MARCO_WALK_0 : MARCO_WALK_1;
        blitSprite(walk, MARCO_WALK_0_W, MARCO_WALK_0_H, (int)marcoX, marcoY - 1, true, false);
    } else if (phase == Phase::Firefight) {
        const uint16_t* shoot = (animFrame % 2 == 0) ? MARCO_SHOOT_0 : MARCO_SHOOT_1;
        blitSprite(shoot, MARCO_SHOOT_0_W, MARCO_SHOOT_0_H, (int)marcoX, marcoY + 1, true, false);

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
        blitSprite(MARCO_GRENADE, MARCO_GRENADE_W, MARCO_GRENADE_H, (int)marcoX, marcoY, true, false);
    } else if (phase == Phase::ExplosionVictory) {
        blitSprite(MARCO_VICTORY, MARCO_VICTORY_W, MARCO_VICTORY_H, (int)marcoX, marcoY - 5, true, false);
    } else {
        // Idle breathing
        const uint16_t* idle = (animFrame % 2 == 0) ? MARCO_IDLE_0 : MARCO_IDLE_1;
        blitSprite(idle, MARCO_IDLE_0_W, MARCO_IDLE_0_H, (int)marcoX, marcoY, true, false);
    }

    // 6. Flying Grenade Arc
    if (grenadeActive) {
        uint16_t grenadeColor = matrix->color565(180, 160, 40);
        uint16_t grenadeSpark = matrix->color565(255, 220, 80);
        matrix->fillRect((int)grenadeX, (int)grenadeY, 3, 3, grenadeColor);
        matrix->drawPixel((int)grenadeX + 1, (int)grenadeY + 1, grenadeSpark);
    }

    // 7. Fiery Explosive Fireballs
    if (phase == Phase::ExplosionVictory) {
        const uint16_t* expSprites[4] = { EXPLOSION_0, EXPLOSION_1, EXPLOSION_2, EXPLOSION_3 };
        int ef = explosionFrame % 4;
        blitSprite(expSprites[ef], EXPLOSION_0_W, EXPLOSION_0_H, (int)tankX + 15, 64 - EXPLOSION_0_H - 4, true, false);
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

    // Score based on clock time (e.g. 001042)
    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "00%02d%02d", storedTime.hours, storedTime.minutes);
    int sx = 22;
    for (int i = 0; scoreStr[i] != '\0'; i++) {
        drawArcadeDigit(sx, 3, scoreStr[i], hudWhite, 1);
        sx += 4;
    }

    // Main Big Glowing Arcade Time in Center (e.g. 10:42)
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", storedTime.hours, storedTime.minutes);
    drawArcadeTime(108, 1, timeStr, hudYellow, 2);

    // Ammo / Arms indicator: ARMS: [H] 1042
    // "ARMS"
    drawArcadeDigit(185, 3, '4', hudCyan, 1); // placeholder icon
    // [H] badge
    matrix->fillRect(195, 2, 7, 7, matrix->color565(220, 160, 20));
    matrix->drawRect(195, 2, 7, 7, matrix->color565(255, 240, 80));
    matrix->fillRect(197, 4, 1, 3, matrix->color565(0, 0, 0));
    matrix->fillRect(199, 4, 1, 3, matrix->color565(0, 0, 0));
    matrix->fillRect(197, 5, 3, 1, matrix->color565(0, 0, 0));

    // Ammo count: current minute or 1042
    char ammoStr[8];
    snprintf(ammoStr, sizeof(ammoStr), "%02d%02d", storedTime.hours, storedTime.minutes);
    int ax = 205;
    for (int i = 0; ammoStr[i] != '\0'; i++) {
        drawArcadeDigit(ax, 3, ammoStr[i], hudYellow, 1);
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
    if (m_snapToNow || lastFrameMs == 0) {
        lastFrameMs = now;
        m_snapToNow = false;
    }
    float dt = (now - lastFrameMs) / 1000.0f;
    lastFrameMs = now;

    if (dt <= 0.0f || dt > 0.5f) dt = 0.033f;

    phaseTimer += dt;
    animTimer += dt;

    if (animTimer >= 0.12f) {
        animTimer -= 0.12f;
        animFrame++;
    }

    int currentSec = storedTime.seconds;
    int currentMin = storedTime.minutes;

    // Detect minute change for special victory blast sequence
    bool minuteChanged = (lastMinute != -1 && currentMin != lastMinute);
    lastMinute = currentMin;

    // State machine driving battle progression across 60 seconds
    if (minuteChanged || (currentSec >= 59 || currentSec <= 2)) {
        if (phase != Phase::ExplosionVictory) {
            phase = Phase::ExplosionVictory;
            explosionFrame = 0;
            explosionTimer = 0.0f;
            grenadeActive = false;
        }
    } else if (currentSec >= 48) {
        phase = Phase::GrenadeAssault;
    } else if (currentSec >= 15) {
        phase = Phase::Firefight;
    } else {
        phase = Phase::Patrol;
    }

    // Phase-specific entity physics and motion
    if (phase == Phase::Patrol) {
        // Marco walks forward towards battle position
        if (marcoX < 50.0f) {
            marcoX += 15.0f * dt;
        }
        // Tank rolls in from right
        if (tankX > 180.0f) {
            tankX -= 25.0f * dt;
        }
        // Helicopter patrols across sky
        heliX += 35.0f * dt;
        if (heliX > 270.0f) heliX = -60.0f;
        tankBulletActive = false;
        grenadeActive = false;
    } else if (phase == Phase::Firefight) {
        // Marco stands ground firing
        tankX = 180.0f;
        // Helicopter hovers and strafes
        heliX = 100.0f + sinf(phaseTimer * 2.0f) * 30.0f;

        // Tank fires cannon shells periodically
        if (!tankBulletActive && fmodf(phaseTimer, 2.5f) < 0.1f) {
            tankBulletActive = true;
            tankBulletX = tankX - 4.0f;
        }
        if (tankBulletActive) {
            tankBulletX -= 80.0f * dt;
            if (tankBulletX <= marcoX + 20.0f) {
                tankBulletActive = false;
            }
        }
    } else if (phase == Phase::GrenadeAssault) {
        tankBulletActive = false;
        // Toss grenade in high arc
        if (!grenadeActive && phaseTimer > 0.3f) {
            grenadeActive = true;
            grenadeX = marcoX + 24.0f;
            grenadeY = 64.0f - 35.0f;
            grenadeVx = (tankX + 20.0f - grenadeX) / 1.2f;
            grenadeVy = -80.0f;
        }
        if (grenadeActive) {
            grenadeX += grenadeVx * dt;
            grenadeVy += 130.0f * dt; // gravity
            grenadeY += grenadeVy * dt;

            if (grenadeX >= tankX + 20.0f || grenadeY >= 50.0f) {
                grenadeActive = false;
            }
        }
    } else if (phase == Phase::ExplosionVictory) {
        explosionTimer += dt;
        if (explosionTimer >= 0.10f) {
            explosionTimer -= 0.10f;
            if (explosionFrame < 3) {
                explosionFrame++;
            }
        }
        tankBulletActive = false;
        grenadeActive = false;
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
