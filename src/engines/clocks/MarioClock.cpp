#include "MarioClock.h"
#include "MarioAssets.h"
#include "MarioFont.h"
#include <string.h>
#include <math.h>

// Sprite sizes, as they are in the source artwork.
namespace {
constexpr int BLOCK_W = 19, BLOCK_H = 19;
constexpr int GROUND_W = 8, GROUND_H = 8;
constexpr int HILL_W = 20, HILL_H = 22;
constexpr int BUSH_W = 21, BUSH_H = 9;
constexpr int CLOUD_W = 13, CLOUD_H = 12;
constexpr int CLOUD_FULL_W = 19, CLOUD_FULL_H = 12;
constexpr int MARIO_W = 13, MARIO_H = 16;
constexpr int MARIO_JUMP_W = 17;
constexpr int SCENE = 64;          ///< the original scene is 64 px square
}  // namespace

MarioClock::MarioClock(IDrawingSurface* display, const EngineConfig* config)
    : ClockFace(display, config) {
    faceFont.load(config);
    storedTime = { 0, 0, 0 };
}

void MarioClock::draw(const TimeData& t) {
    storedTime = t;
}

// Straight bitmap blits: the artwork is RGB565 already, and SKY_COLOR doubles as the mask.
void MarioClock::blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent) {
    if (!matrix || !data) return;
    const int panelW = matrix->width(), panelH = matrix->height();
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= panelH) continue;
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= panelW) continue;
            uint16_t c = data[row * w + col];
            if (transparent && c == _MASK) continue;
            matrix->drawPixel(px, py, c);
        }
    }
}

void MarioClock::drawBlockAt(int x, int y, const char* text) {
    blitSprite(BLOCK, BLOCK_W, BLOCK_H, x, y, false);
    matrix->setFont(&Super_Mario_Bros__24pt7b);
    matrix->setTextSize(1);
    // Its own font at its own size, and no outline: the digits sit in a 19 px block, where a
    // halo closes the counters and makes them unreadable.
    matrix->setTextColor(0x0000);
    matrix->setCursor(x + (strlen(text) == 1 ? 6 : 2), y + 12);
    matrix->print(text);
    matrix->setFont(nullptr);
}

// The scene: the square original in the middle, with the ground and clouds carried out to both
// edges so a wide panel looks like more of the same level rather than a stretched one.
void MarioClock::drawScene(int w, int h) {
    matrix->fillRect(0, 0, w, h, SKY_COLOR);

    int groundTop = h - GROUND_H;
    for (int x = 0; x < w; x += GROUND_W) {
        blitSprite(GROUND, GROUND_W, GROUND_H, x, groundTop, false);
    }

    if (h >= 48) {
        blitSprite(HILL, HILL_W, HILL_H, 0, groundTop - HILL_H + 2, true);
        blitSprite(BUSH, BUSH_W, BUSH_H, w - BUSH_W, groundTop - BUSH_H, true);
    }

    blitSprite(CLOUD1, CLOUD_W, CLOUD_H, 0, (h >= 48 ? 14 : 2), true);
    blitSprite(CLOUD2, CLOUD_W, CLOUD_H, w - CLOUD_W, (h >= 48 ? 6 : 2), true);

    if (w >= 128 && h >= 48) {
        blitSprite(CLOUD_FULL, CLOUD_FULL_W, CLOUD_FULL_H, 40, 6, true);
        blitSprite(CLOUD_FULL, CLOUD_FULL_W, CLOUD_FULL_H, 72, 14, true);
    }
    if (w >= 256 && h >= 48) {
        blitSprite(CLOUD_FULL, CLOUD_FULL_W, CLOUD_FULL_H, 165, 6, true);
        blitSprite(CLOUD_FULL, CLOUD_FULL_W, CLOUD_FULL_H, 198, 14, true);
    }
}

void MarioClock::update() {
    if (!matrix) return;
    const int w = matrix->width();
    const int h = matrix->height();

    const bool isWideTall = (h >= 48);
    const int groundTop = h - GROUND_H;
    int hourX, minuteX, blockY;

    if (isWideTall) {
        const int sceneLeft = (w - SCENE) / 2;
        hourX = sceneLeft + 13;
        minuteX = sceneLeft + 32;
        blockY = 8;
    } else {
        // 128x32 compact layout: blocks placed side-by-side at the right edge
        hourX = w - 46;
        minuteX = w - 24;
        blockY = 3;
    }

    char hh[4], mm[4];
    snprintf(hh, sizeof(hh), "%d", storedTime.hours);
    snprintf(mm, sizeof(mm), "%02d", storedTime.minutes);

    uint32_t now = millis();
    float dt = (lastFrameMs == 0) ? 0.016f : (now - lastFrameMs) / 1000.0f;
    if (dt > 0.2f) dt = 0.2f;
    lastFrameMs = now;

    int speedPct = engineConfig ? engineConfig->getInt("clock_speed", 100) : 100;
    speedPct = constrain(speedPct, 25, 300);

    if (m_snapToNow) {
        m_snapToNow = false;
        strcpy(shownHH, hh);
        strcpy(shownMM, mm);
        lastMinute = storedTime.minutes;
        phase = Phase::Waiting;
        pendingDigits = false;
        blockBounce[0] = blockBounce[1] = 0.0f;
        shellActive = false;
        coinPop = false;
        m_dirty = 2;
    }

    if (lastMinute != storedTime.minutes) {
        bool newHour = (lastMinute >= 0 && storedTime.minutes == 0);
        lastMinute = storedTime.minutes;
        jumpTarget = newHour ? 0 : 1;
        if (isWideTall) {
            if (phase == Phase::Waiting) {
                phase = Phase::RunIn;
                runnerX = -(float)MARIO_JUMP_W;
                pendingDigits = true;
            } else {
                pendingDigits = true;
            }
        } else {
            // 128x32 compact mode: trigger green Koopa shell kick
            shellActive = true;
            shellX = 26.0f;
            pendingDigits = true;
            phase = Phase::RunIn;
        }
        m_dirty = 2;
    }
    if (shownHH[0] == '-') { strcpy(shownHH, hh); strcpy(shownMM, mm); m_dirty = 2; }

    if (isWideTall) {
        const int targetX = ((jumpTarget == 0) ? hourX : minuteX) + BLOCK_W / 2 - MARIO_W / 2;
        const float pace = 85.0f * (speedPct / 100.0f);

        switch (phase) {
            case Phase::Waiting:
                break;
            case Phase::RunIn:
                runnerX += pace * dt;
                if (runnerX >= targetX) {
                    runnerX = (float)targetX;
                    phase = Phase::Jump;
                    jumpT = 0.0f;
                }
                break;
            case Phase::Jump:
                jumpT += dt * 4.0f * (speedPct / 100.0f);
                if (jumpT >= 0.5f && pendingDigits) {
                    pendingDigits = false;
                    blockBounce[jumpTarget] = 0.001f;
                    strcpy(shownHH, hh);
                    strcpy(shownMM, mm);
                }
                if (jumpT >= 1.0f) { jumpT = 0.0f; phase = Phase::RunOut; }
                break;
            case Phase::RunOut:
                runnerX += pace * dt;
                if (runnerX > w) { phase = Phase::Waiting; }
                break;
        }
    } else {
        // 128x32 compact mode shell physics
        if (shellActive) {
            float shellSpeed = 280.0f * (speedPct / 100.0f);
            shellX += shellSpeed * dt;
            int targetX = (jumpTarget == 0) ? hourX : minuteX;
            if (shellX >= targetX - 6) {
                shellActive = false;
                blockBounce[jumpTarget] = 0.001f;
                strcpy(shownHH, hh);
                strcpy(shownMM, mm);
                pendingDigits = false;
                coinPop = true;
                phase = Phase::Waiting;
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        if (blockBounce[i] > 0.0f) {
            blockBounce[i] += dt * 3.2f;
            if (blockBounce[i] >= 1.0f) {
                blockBounce[i] = 0.0f;
                if (!isWideTall) coinPop = false;
            }
        }
    }

    bool animating = (phase != Phase::Waiting) || blockBounce[0] > 0.0f || blockBounce[1] > 0.0f || shellActive;
    if (!animating && m_dirty == 0) {
        m_hasFrame = false;
        return;
    }
    if (m_dirty > 0) m_dirty--;
    m_hasFrame = true;

    drawScene(w, h);

    for (int i = 0; i < 2; i++) {
        float b = blockBounce[i];
        int lift = (b > 0.0f) ? (int)(sinf(b * 3.14159f) * 4.0f) : 0;
        drawBlockAt(i == 0 ? hourX : minuteX, blockY - lift, i == 0 ? shownHH : shownMM);
    }

    if (isWideTall) {
        if (phase != Phase::Waiting) {
            bool airborne = (phase == Phase::Jump);
            int lift = airborne ? (int)(sinf(jumpT * 3.14159f) * (groundTop - blockY - BLOCK_H - 2)) : 0;
            int y = groundTop - MARIO_H - lift;
            if (airborne) {
                blitSprite(MARIO_JUMP, MARIO_JUMP_W, MARIO_H, (int)runnerX, y, true);
            } else {
                const int f = (((int)runnerX + 1200) / 6) % 3;
                blitSprite(MARIO_RUN[f], MARIO_RUN_W[f], MARIO_H, (int)runnerX - 2 + MARIO_RUN_LEFT[f], y, true);
            }
        }
    } else {
        // 128x32: Mario stands at x=14
        blitSprite(MARIO_IDLE, MARIO_W, MARIO_H, 14, groundTop - MARIO_H, true);
        if (shellActive) {
            blitSprite(KOOPA_SHELL, KOOPA_SHELL_W, KOOPA_SHELL_H, (int)shellX, groundTop - KOOPA_SHELL_H, true);
        }
        if (coinPop && blockBounce[jumpTarget] > 0.0f) {
            int targetX = (jumpTarget == 0) ? hourX : minuteX;
            float b = blockBounce[jumpTarget];
            int coinLift = (int)(sinf(b * 3.14159f) * 6.0f);
            blitSprite(GOLD_COIN, 8, 8, targetX + 5, blockY - 4 - coinLift, true);
        }
    }
}

void MarioClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    phase = Phase::Waiting;
    lastMinute = -1;
    shellActive = false;
    coinPop = false;
}
