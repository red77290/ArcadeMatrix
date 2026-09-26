#include "PacmanClock.h"
#include "PacmanSprites.h"
#include "../../core/ConfigLoader.h"
#include <math.h>

PacmanClock::PacmanClock(IDrawingSurface* display, const EngineConfig* config) : ClockFace(display, config) {
    faceFont.load(config);
    storedTime = {0, 0, 0};
    strcpy(oldTimeStr, "");
    strcpy(newTimeStr, "");
    primed = false;
    lastUpdateMs = 0;
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
                            uint16_t digitColor, uint16_t colonColor, int minX, int maxX) {
    int n = strlen(str);
    if (n <= 0 || n > 11) return;
    int16_t bx, by;
    uint16_t bw, bh;

    // Fixed tabular digit slot width across '0'-'9' so digits never shift horizontally
    int maxDigitInk = 0;
    for (char c = '0'; c <= '9'; c++) {
        char s[2] = { c, '\0' };
        matrix->getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
        if ((int)bw > maxDigitInk) maxDigitInk = bw;
    }
    int digitAdv = ClockFaceFont::advance(font, '0') * scale;
    int slotDigitW = max(maxDigitInk, digitAdv);

    // Fixed colon slot width
    char colChar[2] = { ':', '\0' };
    matrix->getTextBounds(colChar, 0, 0, &bx, &by, &bw, &bh);
    int colonAdv = ClockFaceFont::advance(font, ':') * scale;
    int slotColonW = max((int)bw, colonAdv);

    int gap = max(1, scale);

    int cellW[11], inkOff[11];
    int total = 0;
    for (int i = 0; i < n; i++) {
        char c = str[i];
        char one[2] = { c, '\0' };
        matrix->getTextBounds(one, 0, 0, &bx, &by, &bw, &bh);
        if (c >= '0' && c <= '9') {
            cellW[i] = slotDigitW;
            inkOff[i] = (slotDigitW - (int)bw) / 2 - bx;
        } else if (c == ':') {
            cellW[i] = slotColonW;
            inkOff[i] = (slotColonW - (int)bw) / 2 - bx;
        } else {
            int adv = ClockFaceFont::advance(font, c) * scale;
            cellW[i] = max((int)bw, adv);
            inkOff[i] = (cellW[i] - (int)bw) / 2 - bx;
        }
        total += cellW[i] + (i ? gap : 0);
    }

    matrix->getTextBounds(str, 0, 0, &bx, &by, &bw, &bh);
    int cursorY = centreY - (int)bh / 2 - by;
    int x = centreX - total / 2;
    for (int i = 0; i < n; i++) {
        int charLeft = x + inkOff[i];
        int charRight = charLeft + cellW[i];
        if (charRight > minX && charLeft < maxX) {
            uint16_t col = (str[i] == ':') ? colonColor : digitColor;
            if (col != 0) {
                matrix->setTextColor(col);
                matrix->setCursor(charLeft, cursorY);
                matrix->write((uint8_t)str[i]);
            }
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
    int w = PAC_FRAME_CLOSED_COLS * s;
    int h = PAC_FRAME_CLOSED_ROWS * s;
    int left = cx - w / 2;
    int top = cy - h / 2;
    if (left + w <= 0 || left >= matrix->width()) return;
    matrix->fillRect(left, top, w, h, 0);
    const uint16_t* rows = (frame == 0) ? PAC_FRAME_CLOSED : (frame == 1) ? PAC_FRAME_HALF : PAC_FRAME_OPEN;
    blit(rows, PAC_FRAME_CLOSED_ROWS, PAC_FRAME_CLOSED_COLS, left, top, s, matrix->color565(255, 255, 0), !facingRight);
}

// 14x14 ghost: 12-row body, 2-row skirt (two frames). Normal ghosts get white eyes with pupils looking
// the way they travel; frightened ghosts are blue with the pale eyes-and-wavy-mouth face.
void PacmanClock::drawGhost(int cx, int cy, int s, uint16_t color, int skirtFrame, bool lookRight, bool frightened) {
    if (!matrix || s < 1) return;
    int cols = GHOST_BODY_COLS;
    int w = cols * s;
    int h = (GHOST_BODY_ROWS + SKIRT_A_ROWS) * s;
    int left = cx - w / 2;
    int top = cy - h / 2;
    if (left + w <= 0 || left >= matrix->width()) return;
    matrix->fillRect(left, top, w, h, 0);

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

    bool appeared = !primed || (now - lastUpdateMs > 1500);
    lastUpdateMs = now;
    if (!primed) {
        primed = true;
        strcpy(oldTimeStr, nowStr);
        strcpy(newTimeStr, nowStr);
        lastMinute = storedTime.minutes;
        paradeDueMs = now + 400;
    } else if (appeared) {
        if (storedTime.minutes != lastMinute || strcmp(oldTimeStr, nowStr) != 0) {
            strcpy(newTimeStr, nowStr);
            paradeDueMs = now + 300;
        } else {
            paradeDueMs = now + 400;
        }
    } else if (storedTime.minutes != lastMinute && !transitioning) {
        lastMinute = storedTime.minutes;
        strcpy(newTimeStr, nowStr);
        paradeDueMs = now;
    }

    if (paradeDueMs && now >= paradeDueMs && !transitioning) {
        transitioning = true;
        transStartMs = now;
        pacX = 0.0f;
        paradeDueMs = 0;
    }

    if (!transitioning) {
        strcpy(oldTimeStr, nowStr);
        strcpy(newTimeStr, nowStr);
    }

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
    uint16_t oldDigitColor = matrix->color565(110, 110, 110);
    uint16_t oldColonColor = matrix->color565(50, 70, 130);

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
        font = faceFont.apply(*matrix, scale, newTimeStr, w, h);
    }

    // Sprite scale: the 14-px ghost fills the panel (or the tier, in tate) with a small margin.
    int lane = isTate ? min(w, h / 2) : h;
    int s = max(1, (lane - 2) / 14);
    int pacW = PAC_FRAME_CLOSED_COLS * s;
    int ghostW = GHOST_BODY_COLS * s;
    int ghostSpacing = ghostW + 2 * s;                              // ghost centre -> next ghost centre
    int firstGhost = pacW / 2 + 4 * s + ghostW / 2;                 // Pac-Man centre -> first ghost centre

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
        if (pacX >= maxPath) {
            transitioning = false;
            strcpy(oldTimeStr, newTimeStr);
            lastMinute = storedTime.minutes;
        }
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
        char hOld[6], mOld[6], hNew[6], mNew[6];
        splitTime(oldTimeStr, hOld, mOld);
        splitTime(newTimeStr, hNew, mNew);
        int cX = w / 2 + offX;
        int cyH = h / 4 + offY, cyM = 3 * h / 4 + offY, dotY = h / 2 + offY;
        int dotX[3] = { w / 4 + offX, w / 2 + offX, 3 * w / 4 + offX };

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

        if (!transitioning) {
            printTime(hNew, cX, cyH, scale, font, color1, color1);
            printTime(mNew, cX, cyM, scale, font, color1, color1);
            for (int i = 0; i < 3; i++) {
                matrix->fillRect(dotX[i] - 1, dotY - 1, 2, 2, dotColor);
            }
        } else {
            if (leg == 0) {
                // Leg 0: Pac-Man eats old hours transformed into pellets, reveals new hours
                int headX = (int)roundf(-pacW / 2.0f + legPos);
                int cutX = headX + pacW / 2;
                int revealX = (int)(headX - chaseLen - ghostW / 2);
                int margin = max(1, (cutX - revealX) / 2);
                int dissolveDist = 10 * s;
                int oldMinX = cutX + dissolveDist;

                printTime(hNew, cX, cyH, scale, font, color1, color1, -100, revealX + margin);
                printTime(hOld, cX, cyH, scale, font, oldDigitColor, oldColonColor, oldMinX, w + 100);
                if (cutX > 0 && revealX < w) {
                    int rL = max(0, revealX);
                    int rR = min(w, oldMinX);
                    if (rR > rL) matrix->fillRect(rL, 0, rR - rL, h / 2, 0);
                }

                // Pellets ahead of Pac-Man on the hours tier
                int pelletStep = max(4, 4 * s);
                for (int px = ((cutX / pelletStep) + 1) * pelletStep; px < w - 2; px += pelletStep) {
                    if (px > cutX && px < w) matrix->fillRect(px - 1, cyH - 1, 2, 2, dotColor);
                }

                for (int i = 0; i < 3; i++) matrix->fillRect(dotX[i] - 1, dotY - 1, 2, 2, dotColor);
                printTime(mOld, cX, cyM, scale, font, oldDigitColor, oldColonColor);
                if (!inPause) parade(0, legPos, cyH);
            } else if (leg == 1) {
                // Leg 1: Frightened ghosts flee across middle dots, Pac-Man chases
                printTime(hNew, cX, cyH, scale, font, color1, color1);
                printTime(mOld, cX, cyM, scale, font, oldDigitColor, oldColonColor);

                float headX = roundf((w + ghostW / 2.0f) - legPos);
                float eatenFrom = headX - ghostW / 2;
                float eatenTo = headX + chaseLen + pacW / 2;
                for (int i = 0; i < 3; i++) {
                    if (dotX[i] >= eatenFrom && dotX[i] <= eatenTo) continue;
                    matrix->fillRect(dotX[i] - 1, dotY - 1, 2, 2, dotColor);
                }
                if (!inPause) parade(1, legPos, dotY);
            } else {
                // Leg 2: Pac-Man eats old minutes transformed into pellets, reveals new minutes
                printTime(hNew, cX, cyH, scale, font, color1, color1);
                for (int i = 0; i < 3; i++) matrix->fillRect(dotX[i] - 1, dotY - 1, 2, 2, dotColor);

                int headX = (int)roundf(-pacW / 2.0f + legPos);
                int cutX = headX + pacW / 2;
                int revealX = (int)(headX - chaseLen - ghostW / 2);
                int margin = max(1, (cutX - revealX) / 2);
                int dissolveDist = 10 * s;
                int oldMinX = cutX + dissolveDist;

                printTime(mNew, cX, cyM, scale, font, color1, color1, -100, revealX + margin);
                printTime(mOld, cX, cyM, scale, font, oldDigitColor, oldColonColor, oldMinX, w + 100);
                if (cutX > 0 && revealX < w) {
                    int rL = max(0, revealX);
                    int rR = min(w, oldMinX);
                    if (rR > rL) matrix->fillRect(rL, h / 2, rR - rL, h - h / 2, 0);
                }

                // Pellets ahead of Pac-Man on the minutes tier
                int pelletStep = max(4, 4 * s);
                for (int px = ((cutX / pelletStep) + 1) * pelletStep; px < w - 2; px += pelletStep) {
                    if (px > cutX && px < w) matrix->fillRect(px - 1, cyM - 1, 2, 2, dotColor);
                }

                if (!inPause) parade(2, legPos, cyM);
            }
        }
    } else {
        // Landscape: one row, time centred
        int cX = w / 2 + offX;
        int cY = h / 2 + offY;

        if (!transitioning) {
            printTime(newTimeStr, cX, cY, scale, font, color1, colonOn ? colonColor : 0);
        } else {
            if (leg == 0) {
                // Leg 0: Old time transforms into pellets, Pac-Man eats pellets, new time revealed behind Clyde
                int headX = (int)roundf(-pacW / 2.0f + legPos);
                int cutX = headX + pacW / 2;
                int revealX = (int)(headX - chaseLen - ghostW / 2);
                int margin = max(1, (cutX - revealX) / 2);

                // 1. Draw new time revealed on left behind Clyde
                printTime(newTimeStr, cX, cY, scale, font, color1, colonColor, -100, revealX + margin);

                // 2. Old time starts dissolving into pellets ahead of Pac-Man:
                int dissolveDist = 14 * s;
                int oldMinX = cutX + dissolveDist;
                printTime(oldTimeStr, cX, cY, scale, font, oldDigitColor, oldColonColor, oldMinX, w + 100);

                // 3. Clear the parade gap and dissolve zone
                if (cutX > 0 && revealX < w) {
                    int rL = max(0, revealX);
                    int rR = min(w, oldMinX);
                    if (rR > rL) matrix->fillRect(rL, 0, rR - rL, h, 0);
                }

                // 4. In the dissolve zone, draw the pellets (points que Pac-Man mange!)
                int pelletStep = max(5, 5 * s);
                int energizerX = w - 4 * s;

                for (int px = ((cutX / pelletStep) + 1) * pelletStep; px < energizerX - pelletStep / 2; px += pelletStep) {
                    if (px > cutX && px < w) {
                        matrix->fillRect(px - 1, cY - 1, 2, 2, dotColor);
                    }
                }

                // Crumbling particle dissolution effect at the dissolve front:
                if (oldMinX < w - 6) {
                    uint8_t seed = (now / 40) ^ (uint8_t)oldMinX;
                    for (int p = 0; p < 6; p++) {
                        int pOffX = ((seed + p * 7) % (6 * s)) - 3 * s;
                        int pOffY = ((seed * 3 + p * 11) % (12 * s)) - 6 * s;
                        int px = oldMinX + pOffX;
                        int py = cY + pOffY;
                        if (px > cutX && px < w && py >= 0 && py < h) {
                            matrix->drawPixel(px, py, dotColor);
                        }
                    }
                }

                // 5. Flashing Energizer (Power Pellet) waiting at the far right edge:
                if (energizerX > cutX) {
                    bool flash = ((now / 180) & 1);
                    if (flash) {
                        int er = max(2, 2 * s);
                        matrix->fillRect(energizerX - er / 2, cY - er / 2, er, er, dotColor);
                    }
                }

                // 6. Draw Pac-Man and 4 ghosts parading across
                if (!inPause) {
                    drawPacman(headX, cY, s, pacFrame, true);
                    for (int i = 0; i < 4; i++) {
                        drawGhost(headX - firstGhost - i * ghostSpacing, cY, s, ghostColors[i], skirtFrame, true, false);
                    }
                }
            } else {
                // Leg 1: The 4 ghosts flee back BLUE and frightened (right->left), Pac-Man chases!
                printTime(newTimeStr, cX, cY, scale, font, color1, colonColor);

                if (!inPause) {
                    float headX = roundf((w + ghostW / 2.0f) - legPos);
                    for (int i = 0; i < 4; i++) {
                        drawGhost((int)headX + i * ghostSpacing, cY, s, ghostColors[i], skirtFrame, /*lookRight=*/ false, /*frightened=*/ true);
                    }
                    drawPacman((int)headX + chaseLen, cY, s, pacFrame, /*facingRight=*/ false);
                }
            }
        }
    }
}
