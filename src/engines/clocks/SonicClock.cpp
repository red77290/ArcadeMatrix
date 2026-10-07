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

void SonicClock::blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent, bool flipH) {
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
    int x = startX;
    for (int i = 0; str[i] != '\0'; i++) {
        char ch = str[i];
        drawArcadeDigit(x, startY, ch, color, scale);
        int gw = (ch == ':') ? 1 : 3;
        x += (gw + 1) * scale;
    }
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

void SonicClock::drawScene(int w, int h) {
    uint16_t skyBlue = matrix->color565(64, 160, 248);

    if (h >= 48) {
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
        tapTimer = 0.0f;
        isTapping = false;
        motobugTimer = 0.0f;
        m_dirty = 2;
    }

    // Second tick
    if (lastSecond != storedTime.seconds) {
        lastSecond = storedTime.seconds;
        m_dirty = 2;
    }

    // Minute transition
    if (lastMinute != storedTime.minutes) {
        strcpy(shownHH, hh);
        strcpy(shownMM, mm);
        lastMinute = storedTime.minutes;
        m_dirty = 2;
    }

    // Golden ring rotation (animates at ~120ms per frame)
    ringTimer += dt;
    if (ringTimer >= 0.12f) {
        ringTimer = 0.0f;
        ringFrame = (ringFrame + 1) % 4;
        m_dirty = 2;
    }

    // Foot tapping cycle: stands profile for 4s, then taps foot impatiently for 2.5s
    tapTimer += dt;
    if (tapTimer >= 4.0f) {
        if (!isTapping) {
            isTapping = true;
            m_dirty = 2;
        }
        if (tapTimer >= 6.5f) {
            isTapping = false;
            tapTimer = 0.0f;
            m_dirty = 2;
        }
    }

    // Motobug patrol timer
    motobugTimer += dt;
    float motoOffset = sinf(motobugTimer * 1.5f) * 6.0f;
    bool motoFlip = (cosf(motobugTimer * 1.5f) < 0.0f);

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

    const uint16_t* ringSprite = SONIC_RING_FRAMES[ringFrame % 4];

    if (h >= 48) {
        // --- 256x64 Layout ---
        // Sonic: idle profile vs hands on hips foot tapping
        if (isTapping) {
            blitSprite(SONIC_MD_TAP, SONIC_MD_TAP_W, SONIC_MD_TAP_H, 10, 64 - 16 - 42, true, false);
        } else {
            blitSprite(SONIC_MD_IDLE, SONIC_MD_IDLE_W, SONIC_MD_IDLE_H, 14, 64 - 16 - 39, true, false);
        }

        // Item Monitor box (x=52, y=64-16-32=16)
        blitSprite(SONIC_ITEM_BOX, SONIC_ITEM_BOX_W, SONIC_ITEM_BOX_H, 52, 16, true, false);

        // Big Arcade Time centered with scale 4 (approx 68px wide)
        drawArcadeTime(100, 15, timeBuf, yellowTime, 4);

        // Rotating golden ring (x=178, y=22)
        blitSprite(ringSprite, 16, 16, 178, 22, true, false);

        // Badnik Motobug on far right with smooth patrol
        int motoX = 208 + (int)motoOffset;
        blitSprite(SONIC_MOTOBUG, SONIC_MOTOBUG_W, SONIC_MOTOBUG_H, motoX, 64 - 16 - 29, true, motoFlip);
    } else {
        // --- 128x32 Layout ---
        // 8-bit Sonic on left (x=8, y=23-24=-1 clamped to 0)
        blitSprite(SONIC_SMS_IDLE, SONIC_SMS_IDLE_W, SONIC_SMS_IDLE_H, 8, 0, true, false);

        // Rotating golden ring (x=30, y=6)
        blitSprite(ringSprite, 16, 16, 30, 6, true, false);

        // Big Arcade Time centered with scale 3
        drawArcadeTime(50, 6, timeBuf, yellowTime, 3);
    }
}

void SonicClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    m_snapToNow = true;
    lastFrameMs = 0;
    tapTimer = 0.0f;
    isTapping = false;
    motobugTimer = 0.0f;
}
