#include "MatrixRainClock.h"
#include "MatrixRainFont.h"
#include "../../core/ConfigLoader.h"
#include <stdlib.h>

static uint8_t randomGlyph() {
    return rand() % MATRIX_RAIN_NUM_GLYPHS;
}

MatrixRainClock::MatrixRainClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config), numColumns(0), initialized(false), lastFrameTime(0) {
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

    int logicalSize = (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) > 0 ? (engineConfig ? engineConfig->getInt("clock_size", 1) : 1) : 2;
    int gfxSize = 1;
    if (logicalSize >= 5) gfxSize = sMax + 1;
    else if (logicalSize == 4) gfxSize = sMax;
    else if (logicalSize == 3) gfxSize = max(1, (sMax * 3) / 4);
    else if (logicalSize == 2) gfxSize = max(1, (sMax * 2) / 4);
    else gfxSize = max(1, sMax / 4);

    matrix->setTextSize(gfxSize);
    matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);

    int x = (matrix->width() - bw) / 2 + (engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0) - bx;
    int y = (matrix->height() - bh) / 2 - by + (engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0);

    // Black outline so time remains readable over the rain without blocking background kanjis
    matrix->setTextColor(matrix->color565(0, 0, 0));
    matrix->setCursor(x - 1, y); matrix->print(timeStr);
    matrix->setCursor(x + 1, y); matrix->print(timeStr);
    matrix->setCursor(x, y - 1); matrix->print(timeStr);
    matrix->setCursor(x, y + 1); matrix->print(timeStr);

    matrix->setTextColor(matrix->color565(0, 140, 0)); // Dark green text (Vert foncé)
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
            colHead[c] = -(rand() % 6);
            colSpeedDivider[c] = 1 + (rand() % 2);
            colTick[c] = 0;
            for (int r = 0; r < rows; r++) {
                colGlyphs[c][r] = randomGlyph();
            }
        }
        initialized = true;
    }

    int rows = matrix->height() / glyphH;
    if (rows > 16) rows = 16;

    // Advance fall animation
    for (int c = 0; c < numColumns; c++) {
        colTick[c]++;
        if (colTick[c] >= colSpeedDivider[c]) {
            colTick[c] = 0;
            colHead[c]++;
            // Occasionally mutate a glyph in the trail for classic Matrix code morphing
            if (colHead[c] >= 0 && colHead[c] < rows && (rand() % 4) == 0) {
                colGlyphs[c][colHead[c]] = randomGlyph();
            }
            if (colHead[c] - rows > 2) {
                // Immediately respawn near the top to maintain continuous rain
                colHead[c] = -(rand() % 4);
                colSpeedDivider[c] = 1 + (rand() % 2);
                for (int r = 0; r < rows; r++) {
                    colGlyphs[c][r] = randomGlyph();
                }
            }
        }
    }

    matrix->fillScreen(0);

    const int trailLength = 12;
    for (int c = 0; c < numColumns; c++) {
        for (int r = 0; r <= colHead[c]; r++) {
            if (r < 0 || r >= rows) continue;
            int distFromHead = colHead[c] - r;
            if (distFromHead > trailLength) continue;
            
            uint16_t color;
            if (distFromHead == 0) {
                color = matrix->color565(255, 255, 255); // Bright white head
            } else {
                int green = 255 - (distFromHead * (220 / trailLength));
                if (green < 30) green = 30;
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
