#include "MatrixRainClock.h"
#include "MatrixRainFont.h"
#include "../../core/ConfigLoader.h"
#include <stdlib.h>

extern ConfigLoader config;

static uint8_t randomGlyph() {
    return rand() % MATRIX_RAIN_NUM_GLYPHS;
}

MatrixRainClock::MatrixRainClock(MatrixPanel_I2S_DMA* display)
    : ClockFace(display), numColumns(0), initialized(false), lastFrameTime(0) {
    storedTime = {0, 0, 0};
}

void MatrixRainClock::draw(const TimeData& t) {
    storedTime = t;
}

void MatrixRainClock::drawTime() {
    matrix->setFont(NULL);
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);

    matrix->setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
    if (bw == 0 || bh == 0) { bw = 48; bh = 7; } // Fallback

    int maxScaleW = matrix->width() / bw;
    int maxScaleH = matrix->height() / bh;
    int sMax = min(maxScaleW, maxScaleH);
    if (sMax < 1) sMax = 1;

    int logicalSize = config.time.clock_size > 0 ? config.time.clock_size : 2;
    int gfxSize = 1;
    if (logicalSize >= 5) gfxSize = sMax + 1;
    else if (logicalSize == 4) gfxSize = sMax;
    else if (logicalSize == 3) gfxSize = max(1, (sMax * 3) / 4);
    else if (logicalSize == 2) gfxSize = max(1, (sMax * 2) / 4);
    else gfxSize = max(1, sMax / 4);

    matrix->setTextSize(gfxSize);
    matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);

    int x = (matrix->width() - bw) / 2 + config.time.clock_offset_x - bx;
    int y = (matrix->height() - bh) / 2 - by + config.time.clock_offset_y;

    // Solid backing box so the time stays legible over the noisy rain background.
    matrix->fillRect(x + bx - 2, y + by - 2, bw + 4, bh + 4, matrix->color565(0, 0, 0));
    matrix->setTextColor(matrix->color565(220, 255, 220));
    matrix->setCursor(x, y);
    matrix->print(timeStr);
}

void MatrixRainClock::update() {
    const int glyphW = 8; // TrueMatrix is 8x8
    const int glyphH = 8;

    if (!initialized) {
        numColumns = matrix->width() / glyphW;
        if (numColumns > MAX_COLUMNS) numColumns = MAX_COLUMNS;
        int rows = matrix->height() / glyphH;
        if (rows > 16) rows = 16;

        for (int c = 0; c < numColumns; c++) {
            colHead[c] = -(rand() % 16);
            colSpeedDivider[c] = 1 + (rand() % 3);
            colTick[c] = 0;
            for (int r = 0; r < rows; r++) {
                colGlyphs[c][r] = randomGlyph();
            }
        }
        initialized = true;
    }

    int rows = matrix->height() / glyphH;
    if (rows > 16) rows = 16;

    // Throttle the fall speed independently of the display's ~30 FPS refresh loop.
    if (true) {
        lastFrameTime = millis();
        for (int c = 0; c < numColumns; c++) {
            colTick[c]++;
            if (colTick[c] >= colSpeedDivider[c]) {
                colTick[c] = 0;
                colHead[c]++;
                // Occasionally mutate a glyph in the trail for a bit of extra "flicker".
                if (colHead[c] >= 0 && colHead[c] < rows && (rand() % 4) == 0) {
                    colGlyphs[c][colHead[c]] = randomGlyph();
                }
                if (colHead[c] - rows > 4) {
                    // Off the bottom of the screen: respawn this column from the top.
                    colHead[c] = -(rand() % 16);
                    colSpeedDivider[c] = 1 + (rand() % 3);
                    for (int r = 0; r < rows; r++) {
                        colGlyphs[c][r] = randomGlyph();
                    }
                }
            }
        }
    }

    matrix->fillScreen(0);

    const int trailLength = 8;
    for (int c = 0; c < numColumns; c++) {
        for (int r = 0; r <= colHead[c] && r >= colHead[c] - trailLength; r++) {
            if (r < 0 || r >= rows) continue;
            int distFromHead = colHead[c] - r;
            uint16_t color;
            if (distFromHead == 0) {
                color = matrix->color565(255, 255, 255); // Bright white head
            } else {
                int green = 255 - (distFromHead * (255 / trailLength));
                if (green < 40) green = 40;
                color = matrix->color565(0, green, 0);
            }
            
            int x = c * glyphW;
            int y = r * glyphH;
            uint8_t glyphIndex = colGlyphs[c][r];
            
            // Draw the 8x8 1-bit bitmap
            matrix->drawBitmap(x, y, MATRIX_RAIN_GLYPHS[glyphIndex], 8, 8, color);
        }
    }

    drawTime();
}
