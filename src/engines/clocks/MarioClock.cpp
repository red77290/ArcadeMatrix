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
constexpr int MARIO_W = 13, MARIO_H = 16;
constexpr int MARIO_JUMP_W = 17;
constexpr int SCENE = 64;          ///< the original scene is 64 px square
}  // namespace

MarioClock::MarioClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config)
    : ClockFace(display, config) {
    faceFont.load(config);
    storedTime = { 0, 0, 0 };
}

void MarioClock::draw(const TimeData& t) {
    storedTime = t;
}

// Straight bitmap blits: the artwork is RGB565 already, and SKY_COLOR doubles as the mask.
void MarioClock::blitSprite(const uint16_t* data, int w, int h, int x, int y, bool transparent) {
    if (!matrix) return;
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
// edges so a wide panel looks like more of the same level rather than a stretched one. The hill and
// the bush are each cut down one side to sit against a frame edge, so there is one of each, at the
// edge it was drawn for.
void MarioClock::drawScene(int w, int h) {
    const int sceneLeft = (w - SCENE) / 2;
    matrix->fillRect(0, 0, w, h, SKY_COLOR);

    int groundTop = h - GROUND_H;
    for (int x = 0; x < w; x += GROUND_W) {
        blitSprite(GROUND, GROUND_W, GROUND_H, x, groundTop, false);
    }

    // The hill is half a hill: its left side is a sheer vertical cut, drawn to sit flush against the
    // frame edge so it reads as a slope running on past it. Tiled across a wide panel that cut lands
    // in open sky and looks like a hill sliced off, so there is one, against the left edge, as in
    // the original.
    blitSprite(HILL, HILL_W, HILL_H, 0, groundTop - HILL_H + 2, true);

    // The bush is the hill's mirror: cut down its right side, drawn to sit flush against the other
    // frame edge (x 43 of 64, so its right edge lands exactly on it). One, against the right edge.
    blitSprite(BUSH, BUSH_W, BUSH_H, w - BUSH_W, groundTop - BUSH_H, true);

    // Clouds are whole sprites, so they carry on across the extra width.
    for (int x = sceneLeft % 64 - 64; x < w; x += 51) {
        blitSprite(CLOUD1, CLOUD_W, CLOUD_H, x, 8, true);
        blitSprite(CLOUD2, CLOUD_W, CLOUD_H, x + 25, 2, true);
    }
}

void MarioClock::update() {
    if (!matrix) return;
    const int w = matrix->width();
    const int h = matrix->height();
    if (w < 192 || h < 64) {     // laid out for a wide panel; see the theme name
        if (m_dirty == 0) { m_hasFrame = false; return; }
        m_dirty--;
        m_hasFrame = true;
        matrix->fillRect(0, 0, w, h, 0x0000);
        matrix->setFont(nullptr);
        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(255, 160, 0));
        matrix->setCursor(2, h / 2 - 7);
        matrix->print("NEEDS");
        matrix->setCursor(2, h / 2 + 1);
        matrix->print("256x64");
        return;
    }

    const int sceneLeft = (w - SCENE) / 2;
    const int groundTop = h - GROUND_H;
    // Original block positions inside the 64 px scene, kept so the pair sits where it should.
    const int hourX = sceneLeft + 13;
    const int minuteX = sceneLeft + 32;
    const int blockY = (h >= 64) ? 8 : 2;

    char hh[4], mm[4];
    // The original prints the hour unpadded, and the block centres one digit differently from two,
    // so 12-hour time reads "1" rather than "01".
    snprintf(hh, sizeof(hh), "%d", storedTime.hours);
    snprintf(mm, sizeof(mm), "%02d", storedTime.minutes);

    uint32_t now = millis();
    float dt = (lastFrameMs == 0) ? 0.016f : (now - lastFrameMs) / 1000.0f;
    if (dt > 0.2f) dt = 0.2f;
    lastFrameMs = now;

    int speedPct = engineConfig ? engineConfig->getInt("clock_speed", 100) : 100;
    speedPct = constrain(speedPct, 25, 300);

    if (lastMinute != storedTime.minutes) {
        bool newHour = (lastMinute >= 0 && storedTime.minutes == 0);
        lastMinute = storedTime.minutes;
        jumpTarget = newHour ? 0 : 1;
        if (phase == Phase::Waiting) {
            phase = Phase::RunIn;
            runnerX = -(float)MARIO_JUMP_W;
            pendingDigits = true;
        } else {
            pendingDigits = true;
        }
        m_dirty = 2;
    }
    if (shownHH[0] == '-') { strcpy(shownHH, hh); strcpy(shownMM, mm); m_dirty = 2; }

    const int targetX = ((jumpTarget == 0) ? hourX : minuteX) + BLOCK_W / 2 - MARIO_W / 2;
    const float pace = 34.0f * (speedPct / 100.0f);

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
            jumpT += dt * 1.6f * (speedPct / 100.0f);
            if (jumpT >= 0.5f && pendingDigits) {     // struck at the top of the arc
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
    for (int i = 0; i < 2; i++) {
        if (blockBounce[i] > 0.0f) {
            blockBounce[i] += dt * 3.2f;
            if (blockBounce[i] >= 1.0f) blockBounce[i] = 0.0f;
        }
    }

    // A full repaint costs a screenful of pixels, so it happens only when something changed: on
    // arrival (both DMA buffers), on a minute, and while Mario is on screen.
    bool animating = (phase != Phase::Waiting) || blockBounce[0] > 0.0f || blockBounce[1] > 0.0f;
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

    if (phase != Phase::Waiting) {
        bool airborne = (phase == Phase::Jump);
        int lift = airborne ? (int)(sinf(jumpT * 3.14159f) * (groundTop - blockY - BLOCK_H - 2)) : 0;
        int y = groundTop - MARIO_H - lift;
        if (airborne) {
            blitSprite(MARIO_JUMP, MARIO_JUMP_W, MARIO_H, (int)runnerX, y, true);
        } else {
            blitSprite(MARIO_IDLE, MARIO_W, MARIO_H, (int)runnerX, y, true);
        }
    }
}

void MarioClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    phase = Phase::Waiting;
    lastMinute = -1;
}
