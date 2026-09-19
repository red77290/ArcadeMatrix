#include "PacmanClock.h"
#include "PacmanSprites.h"
#include "../../core/ConfigLoader.h"
#include <math.h>

PacmanClock::PacmanClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) {
    faceFont.load(config);
    storedTime = {0, 0, 0};
    strcpy(timeStr, "");
    primed = false;
    lastUpdateMs = 0;
    appearedMs = 0;
    paradeDueMs = 0;
    lastMinute = -1;
    transitioning = false;
    transStartMs = 0;
    pacX = 0.0f;

    if (matrix) {
        ghostColors[0] = matrix->color565(255, 0, 0);       // Blinky
        ghostColors[1] = matrix->color565(255, 184, 255);   // Pinky
        ghostColors[2] = matrix->color565(0, 255, 255);     // Inky
        ghostColors[3] = matrix->color565(255, 184, 82);    // Clyde
    }
}

void PacmanClock::draw(const TimeData& t) {
    storedTime = t;
}

// Time in the instance's format. The engine already folds the hour to 12h when the format asks for
// it; this decides the leading zero (kept for 24h, dropped for 12h: "8:08") and whether seconds show
// (any format containing %S, and the "system" default). %p is not rendered.
void PacmanClock::formatTime(char* out, size_t n) const {
    extern ConfigLoader config;
    bool is24h = config.acquireSnapshot()->system.format24h;
    bool seconds = true;
    if (engineConfig) {
        String fmt = engineConfig->getString("clock_format", "system");
        if (fmt.isEmpty() || fmt.equalsIgnoreCase("system")) fmt = engineConfig->getString("format", "system");
        if (fmt.indexOf("%I") >= 0) is24h = false;
        else if (fmt.indexOf("%H") >= 0) is24h = true;
        if (!(fmt.isEmpty() || fmt.equalsIgnoreCase("system"))) seconds = fmt.indexOf("%S") >= 0;
    }
    if (seconds) {
        snprintf(out, n, is24h ? "%02d:%02d:%02d" : "%d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    } else {
        snprintf(out, n, is24h ? "%02d:%02d" : "%d:%02d", storedTime.hours, storedTime.minutes);
    }
}

// "H:MM[:SS]" -> hours and minutes (seconds are not shown on the stacked tate layout).
void PacmanClock::splitTime(const char* str, char* hours, char* minutes) {
    const char* colon = strchr(str, ':');
    if (!colon) { strncpy(hours, str, 5); hours[5] = '\0'; minutes[0] = '\0'; return; }
    size_t hl = min((size_t)(colon - str), (size_t)5);
    memcpy(hours, str, hl); hours[hl] = '\0';
    const char* end = strchr(colon + 1, ':');
    size_t ml = end ? (size_t)(end - colon - 1) : strlen(colon + 1);
    if (ml > 5) ml = 5;
    memcpy(minutes, colon + 1, ml); minutes[ml] = '\0';
}

/**
 * Print a time string centred on (centreX, centreY), spacing glyphs by their ink rather than by the
 * font's fixed advance: digits sit one digit-gap apart whatever their width (a narrow "1" no longer
 * floats away from the colon), and the colon keeps its full cell so it stays centred between digits.
 * Colons take `colonColor`; 0 hides the colon (blink off) without disturbing the layout.
 */
void PacmanClock::printTime(const char* str, int centreX, int centreY, int scale, const GFXfont* font,
                            uint16_t digitColor, uint16_t colonColor) {
    int n = strlen(str);
    if (n <= 0 || n > 11) return;
    int16_t bx, by;
    uint16_t bw, bh;

    // Digit gap = the font's own inter-digit spacing at this size.
    matrix->getTextBounds("0", 0, 0, &bx, &by, &bw, &bh);
    int gap = max(1, ClockFaceFont::advance(font, '0') * scale - (int)bw);

    int cellW[11], inkOff[11];
    int total = 0;
    for (int i = 0; i < n; i++) {
        char one[2] = { str[i], '\0' };
        matrix->getTextBounds(one, 0, 0, &bx, &by, &bw, &bh);
        int adv = ClockFaceFont::advance(font, str[i]) * scale;
        if (str[i] >= '0' && str[i] <= '9') {
            cellW[i] = bw;                          // ink only
            inkOff[i] = -bx;                        // cursor so the ink starts at the cell's left edge
        } else {
            cellW[i] = adv;                         // full cell, ink centred in it
            inkOff[i] = (adv - (int)bw) / 2 - bx;
        }
        total += cellW[i] + (i ? gap : 0);
    }

    matrix->getTextBounds(str, 0, 0, &bx, &by, &bw, &bh);
    int cursorY = centreY - (int)bh / 2 - by;
    int x = centreX - total / 2;
    for (int i = 0; i < n; i++) {
        uint16_t col = (str[i] == ':') ? colonColor : digitColor;
        if (col != 0) {
            matrix->setTextColor(col);
            matrix->setCursor(x + inkOff[i], cursorY);
            matrix->write((uint8_t)str[i]);
        }
        x += cellW[i] + gap;
    }
}

// Draw a row-mask sprite at integer scale `s`, one fillRect per run of set pixels.
void PacmanClock::blit(const uint16_t* rows, int nRows, int nCols, int left, int top, int s, uint16_t color, bool mirror) {
    int w = matrix->width();
    if (left + nCols * s <= 0 || left >= w) return;
    for (int r = 0; r < nRows; r++) {
        uint16_t bits = rows[r];
        int y = top + r * s;
        int c = 0;
        while (c < nCols) {
            int src = mirror ? (nCols - 1 - c) : c;
            if (!(bits & (1u << (nCols - 1 - src)))) { c++; continue; }
            int run = 1;
            while (c + run < nCols) {
                int s2 = mirror ? (nCols - 1 - (c + run)) : (c + run);
                if (!(bits & (1u << (nCols - 1 - s2)))) break;
                run++;
            }
            matrix->fillRect(left + c * s, y, run * s, s, color);
            c += run;
        }
    }
}

// frame: 0 closed, 1 half open, 2 wide open. Sprite is 13x13, centred on (cx, cy).
void PacmanClock::drawPacman(int cx, int cy, int s, int frame, bool facingRight) {
    if (!matrix || s < 1) return;
    const uint16_t* rows = (frame == 0) ? PAC_FRAME_CLOSED : (frame == 1) ? PAC_FRAME_HALF : PAC_FRAME_OPEN;
    int left = cx - (PAC_FRAME_CLOSED_COLS * s) / 2;
    int top = cy - (PAC_FRAME_CLOSED_ROWS * s) / 2;
    blit(rows, PAC_FRAME_CLOSED_ROWS, PAC_FRAME_CLOSED_COLS, left, top, s, matrix->color565(255, 255, 0), !facingRight);
}

// 14x14 ghost: 12-row body, 2-row skirt (two frames). Normal ghosts get white eyes with pupils looking
// the way they travel; frightened ghosts are blue with the pale eyes-and-wavy-mouth face.
void PacmanClock::drawGhost(int cx, int cy, int s, uint16_t color, int skirtFrame, bool lookRight, bool frightened) {
    if (!matrix || s < 1) return;
    int cols = GHOST_BODY_COLS;
    int left = cx - (cols * s) / 2;
    int top = cy - ((GHOST_BODY_ROWS + SKIRT_A_ROWS) * s) / 2;
    if (left + cols * s <= 0 || left >= matrix->width()) return;

    uint16_t body = frightened ? matrix->color565(33, 33, 255) : color;
    blit(GHOST_BODY, GHOST_BODY_ROWS, cols, left, top, s, body, false);
    blit(skirtFrame ? SKIRT_B : SKIRT_A, SKIRT_A_ROWS, cols, left, top + GHOST_BODY_ROWS * s, s, body, false);
    if (frightened) {
        blit(FRIGHT_FACE, FRIGHT_FACE_ROWS, cols, left, top + 5 * s, s, matrix->color565(255, 184, 174), false);
    } else {
        blit(EYES, EYES_ROWS, cols, left, top + 3 * s, s, matrix->color565(255, 255, 255), false);
        blit(lookRight ? PUPIL_R : PUPIL_L, PUPIL_R_ROWS, cols, left, top + 5 * s, s, matrix->color565(33, 33, 255), false);
    }
}

void PacmanClock::update() {
    if (!matrix) return;
    char nowStr[12];
    formatTime(nowStr, sizeof(nowStr));
    uint32_t now = millis();

    // One parade each time the face comes on screen (first update, or the first update after a gap:
    // only the active engine is updated), starting shortly after the time is readable. A face left up
    // for a long stretch parades again on each minute change, but never twice inside a short slot.
    bool appeared = !primed || (now - lastUpdateMs > 1500);
    lastUpdateMs = now;
    if (appeared) {
        primed = true;
        appearedMs = now;
        paradeDueMs = now + 600;
        lastMinute = storedTime.minutes;
    } else if (storedTime.minutes != lastMinute) {
        lastMinute = storedTime.minutes;
        if (now - appearedMs >= 30000 && !transitioning) paradeDueMs = now;
    }
    if (paradeDueMs && now >= paradeDueMs && !transitioning) {
        transitioning = true;
        transStartMs = now;
        pacX = 0.0f;
        paradeDueMs = 0;
    }
    strcpy(timeStr, nowStr);

    int w = matrix->width();
    int h = matrix->height();
    bool isTate = (w < 48 || h > (w * 3) / 2);

    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (gfxSize < 1) gfxSize = 1;
    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;
    int speedPct = engineConfig ? engineConfig->getInt("clock_speed", 100) : 100;
    speedPct = constrain(speedPct, 25, 300);

    uint16_t color1 = matrix->color565(255, 255, 255);
    const char* colStr = engineConfig ? engineConfig->getString("clock_color_1", "").c_str() : "";
    if (colStr[0] == '#') {
        long c1 = strtol(&colStr[1], NULL, 16);
        color1 = matrix->color565((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF);
    }
    if (color1 == 0) color1 = matrix->color565(255, 255, 255);
    uint16_t colonColor = matrix->color565(60, 100, 255);
    uint16_t dotColor = matrix->color565(255, 183, 174);

    // Font: the configured face font at the configured size, falling back to the built-in one if the
    // time would not fit.
    matrix->setTextWrap(false);
    int scale;
    const GFXfont* font;
    if (isTate) {
        scale = (w >= 64) ? 3 : 2;
        if (gfxSize >= 1 && gfxSize <= 4) scale = min(scale, gfxSize);
        font = faceFont.apply(*matrix, scale, "88", w, h / 2);
    } else {
        scale = gfxSize;
        font = faceFont.apply(*matrix, scale, timeStr, w, h);
    }

    // Sprite scale: the 14-px ghost fills the panel (or the tier, in tate) with a small margin.
    int lane = isTate ? min(w, h / 2) : h;
    int s = max(1, (lane - 2) / 14);
    int pacW = PAC_FRAME_CLOSED_COLS * s;
    int ghostW = GHOST_BODY_COLS * s;
    int ghostSpacing = ghostW + 2 * s;                              // ghost centre -> next ghost centre
    int firstGhost = pacW / 2 + 4 * s + ghostW / 2;                 // Pac-Man centre -> first ghost centre

    // Out leg: Pac-Man leads, ghosts chase (left -> right). Return leg: the ghosts flee back blue and
    // frightened with Pac-Man chasing (right -> left). Clocked on wall time, two ghost widths per
    // second at 100%; tate runs out over the hours, back over the pellets, out again over the minutes.
    float speed = 2.0f * ghostW * (speedPct / 100.0f);
    int chaseLen = firstGhost + 3 * ghostSpacing;                   // leader centre -> last follower centre
    float legOut = w + pacW / 2 + chaseLen + ghostW / 2;            // Pac-Man enters left, last ghost exits right
    float legBack = w + ghostW / 2 + chaseLen + pacW / 2;           // lead ghost enters right, Pac-Man exits left
    float pauseLen = speed * 0.4f;                                  // beat between the legs
    float path[3] = { legOut, legBack, legOut };
    int legs = isTate ? 3 : 2;
    float maxPath = 0;
    for (int i = 0; i < legs; i++) maxPath += path[i] + (i ? pauseLen : 0);
    if (transitioning) {
        pacX = ((now - transStartMs) / 1000.0f) * speed;
        if (pacX >= maxPath) transitioning = false;
    }
    // Which leg are we in, and how far along it (negative during a pause)?
    int leg = 0;
    float legPos = pacX;
    for (int i = 0; i < legs - 1 && legPos >= path[i] + pauseLen; i++) { legPos -= path[i] + pauseLen; leg = i + 1; }
    bool inPause = transitioning && legPos >= path[leg] && leg < legs - 1;

    static const uint8_t chompSeq[4] = { 0, 1, 2, 1 };
    int pacFrame = chompSeq[(now / 50) & 3];                         // closed-half-open-half, 200 ms cycle
    int skirtFrame = (now / 160) & 1;
    bool colonOn = ((now / 500) & 1) == 0;

    matrix->fillScreen(0);

    if (isTate) {
        // Stacked portrait layout: hours on the top tier, pellets in the middle, minutes on the bottom.
        // The parade runs three legs: hours left->right, pellets right->left, minutes left->right.
        char hStr[6], mStr[6];
        splitTime(timeStr, hStr, mStr);
        int cX = w / 2 + offX;
        int cyH = h / 4 + offY, cyM = 3 * h / 4 + offY, dotY = h / 2 + offY;
        int dotX[3] = { w / 4 + offX, w / 2 + offX, 3 * w / 4 + offX };

        printTime(hStr, cX, cyH, scale, font, color1, color1);
        printTime(mStr, cX, cyM, scale, font, color1, color1);

        // Parade helper: out legs lead with Pac-Man moving right; the back leg leads with a frightened
        // ghost moving left and Pac-Man chasing.
        auto parade = [&](int legIdx, float pos, int cY) {
            if (legIdx == 1) {
                float headX = (w + ghostW / 2) - pos;
                for (int i = 0; i < 4; i++) drawGhost((int)headX + i * ghostSpacing, cY, s, ghostColors[i], skirtFrame, false, true);
                drawPacman((int)headX + chaseLen, cY, s, pacFrame, false);
            } else {
                float headX = -pacW / 2 + pos;
                drawPacman((int)headX, cY, s, pacFrame, true);
                for (int i = 0; i < 4; i++) drawGhost((int)headX - firstGhost - i * ghostSpacing, cY, s, ghostColors[i], skirtFrame, true, false);
            }
        };

        float eatenFrom = 1, eatenTo = 0;   // empty range: every pellet showing
        if (transitioning && !inPause && leg == 1) {
            float headX = (w + ghostW / 2) - legPos;
            eatenFrom = headX - ghostW / 2;
            eatenTo = headX + chaseLen + pacW / 2;   // pellets regrow behind Pac-Man
        }
        for (int i = 0; i < 3; i++) {
            if (dotX[i] >= eatenFrom && dotX[i] <= eatenTo) continue;
            matrix->fillRect(dotX[i] - 1, dotY - 1, 2, 2, dotColor);
        }

        if (transitioning && !inPause) {
            parade(leg, legPos, (leg == 0) ? cyH : (leg == 1) ? dotY : cyM);
        }
    } else {
        // Landscape: one row, time centred, parade left -> right over it.
        printTime(timeStr, w / 2 + offX, h / 2 + offY, scale, font, color1,
                  (transitioning || colonOn) ? colonColor : 0);
        if (transitioning && !inPause) {
            if (leg == 0) {
                float headX = -pacW / 2 + legPos;
                drawPacman((int)headX, h / 2, s, pacFrame, true);
                for (int i = 0; i < 4; i++) drawGhost((int)headX - firstGhost - i * ghostSpacing, h / 2, s, ghostColors[i], skirtFrame, true, false);
            } else {
                float headX = (w + ghostW / 2) - legPos;
                for (int i = 0; i < 4; i++) drawGhost((int)headX + i * ghostSpacing, h / 2, s, ghostColors[i], skirtFrame, false, true);
                drawPacman((int)headX + chaseLen, h / 2, s, pacFrame, false);
            }
        }
    }
}
