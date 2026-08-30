#include "MatrixRainClock.h"
#include "MatrixRainFont.h"
#include "../../core/ConfigLoader.h"
#include <stdlib.h>

static uint8_t randomGlyph() {
    return rand() % MATRIX_RAIN_NUM_GLYPHS;
}

MatrixRainClock::MatrixRainClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) 
    : ClockFace(display, config), numColumns(0), numRows(0), initialized(false), lastFrameTime(0) {
    storedTime = {0, 0, 0};
}

void MatrixRainClock::draw(const TimeData& t) {
    storedTime = t;
}

void MatrixRainClock::drawTime() {
    matrix->setFont(NULL);
    int w = matrix->width();
    int h = matrix->height();
    bool isTate = (w < 48 || h > (w * 3) / 2);

    int logicalSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (logicalSize < 1) logicalSize = 1;

    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;

    uint16_t glowGreen = matrix->color565(0, 255, 60);    // Outer Neon Glow
    uint16_t coreMint  = matrix->color565(225, 255, 225); // Ultra-bright Mint-White Core
    uint16_t secMint   = matrix->color565(120, 255, 140); // Emerald Mint for seconds
    uint16_t haloDark  = matrix->color565(0, 20, 5);      // Deep dark matrix backing
    uint16_t borderCol = matrix->color565(0, 60, 20);

    if (isTate) {
        // ====================================================================
        // Stacked Portrait Layout (HH, MM, SS)
        // ====================================================================
        char hStr[8], mStr[8], sStr[8];
        sprintf(hStr, "%02d", storedTime.hours);
        sprintf(mStr, "%02d", storedTime.minutes);
        sprintf(sStr, "%02d", storedTime.seconds);

        int scale = (w >= 64) ? 3 : 2;
        if (logicalSize >= 1 && logicalSize <= 4) scale = min(scale, logicalSize);
        matrix->setTextSize(scale);

        int textW = 12 * scale - scale;
        int textH = 8 * scale;
        int tx = (w - textW) / 2 + offX;

        int yH = (h / 6) - (textH / 2) + offY;
        int yM = (h / 2) - (textH / 2) + offY;
        int yS = (5 * h / 6) - (textH / 2) + offY;

        // Tier 1: Hours (Glow + Core)
        matrix->setTextColor(glowGreen);
        matrix->setCursor(tx - 1, yH); matrix->print(hStr);
        matrix->setCursor(tx + 1, yH); matrix->print(hStr);
        matrix->setCursor(tx, yH - 1); matrix->print(hStr);
        matrix->setCursor(tx, yH + 1); matrix->print(hStr);
        matrix->setTextColor(coreMint);
        matrix->setCursor(tx, yH); matrix->print(hStr);

        // Tier 2: Minutes (Glow + Core)
        matrix->setTextColor(glowGreen);
        matrix->setCursor(tx - 1, yM); matrix->print(mStr);
        matrix->setCursor(tx + 1, yM); matrix->print(mStr);
        matrix->setCursor(tx, yM - 1); matrix->print(mStr);
        matrix->setCursor(tx, yM + 1); matrix->print(mStr);
        matrix->setTextColor(coreMint);
        matrix->setCursor(tx, yM); matrix->print(mStr);

        // Tier 3: Seconds (Subtle Emerald)
        matrix->setTextColor(glowGreen);
        matrix->setCursor(tx - 1, yS); matrix->print(sStr);
        matrix->setCursor(tx + 1, yS); matrix->print(sStr);
        matrix->setTextColor(secMint);
        matrix->setCursor(tx, yS); matrix->print(sStr);

    } else {
        // ====================================================================
        // Landscape / Widescreen Layout ("HH:MM:SS" or configured format)
        // ====================================================================
        String fmt = engineConfig ? engineConfig->getString("clock_format", "%H:%M:%S") : "%H:%M:%S";
        if (fmt.isEmpty() && engineConfig) fmt = engineConfig->getString("format", "%H:%M:%S");
        if (fmt.isEmpty()) fmt = "%H:%M:%S";

        char timeStr[32];
        struct tm timeinfo;
        timeinfo.tm_hour = storedTime.hours;
        timeinfo.tm_min = storedTime.minutes;
        timeinfo.tm_sec = storedTime.seconds;
        timeinfo.tm_wday = 0;
        timeinfo.tm_mday = 1;
        timeinfo.tm_mon = 0;
        timeinfo.tm_year = 126;
        timeinfo.tm_isdst = -1;
        strftime(timeStr, sizeof(timeStr), fmt.c_str(), &timeinfo);

        matrix->setTextSize(1);
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0 || bh == 0) { bw = strlen(timeStr) * 6; bh = 7; }

        int maxScaleW = (w - 8) / bw;
        int maxScaleH = (h - 6) / bh;
        int sMax = min(maxScaleW, maxScaleH);
        if (sMax < 1) sMax = 1;

        int gfxSize = min(logicalSize, sMax);
        matrix->setTextSize(gfxSize);
        matrix->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);

        int textW = strlen(timeStr) * 6 * gfxSize - gfxSize;
        int textH = 8 * gfxSize;
        int x = (w - textW) / 2 + offX;
        int y = (h - textH) / 2 + offY;

        // Outer Neon Glow
        matrix->setTextColor(glowGreen);
        matrix->setCursor(x - 1, y); matrix->print(timeStr);
        matrix->setCursor(x + 1, y); matrix->print(timeStr);
        matrix->setCursor(x, y - 1); matrix->print(timeStr);
        matrix->setCursor(x, y + 1); matrix->print(timeStr);

        // Ultra-bright Core Mint
        matrix->setTextColor(coreMint);
        matrix->setCursor(x, y);
        matrix->print(timeStr);
    }
}

void MatrixRainClock::update() {
    int w = matrix->width();
    int h = matrix->height();

    // Spacing: 6px pitch gives dense, authentic matrix code columns
    const int colPitch = (w <= 32) ? 6 : (w <= 64 ? 6 : 7);
    const int rowPitch = 8;

    int cols = w / colPitch;
    if (cols > MAX_COLUMNS) cols = MAX_COLUMNS;
    if (cols < 1) cols = 1;

    int rows = h / rowPitch;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (rows < 1) rows = 1;

    if (!initialized || numColumns != cols || numRows != rows) {
        numColumns = cols;
        numRows = rows;
        for (int c = 0; c < numColumns; c++) {
            colHead[c] = rand() % numRows;
            colHead2[c] = (colHead[c] + numRows / 2 + (rand() % 4)) % (numRows + 10);
            colSpeedDivider[c] = 1 + (rand() % 2);
            colTick[c] = 0;
            for (int r = 0; r < numRows; r++) {
                colGlyphs[c][r] = randomGlyph();
            }
        }
        initialized = true;
    }

    // Advance falling stream drops
    for (int c = 0; c < numColumns; c++) {
        colTick[c]++;
        if (colTick[c] >= colSpeedDivider[c]) {
            colTick[c] = 0;
            colHead[c]++;
            colHead2[c]++;

            // Head 1 wrap
            if (colHead[c] >= numRows + 12) {
                colHead[c] = -(rand() % 6);
                colSpeedDivider[c] = 1 + (rand() % 2);
            }
            // Head 2 wrap
            if (colHead2[c] >= numRows + 12) {
                colHead2[c] = -(rand() % 6);
            }

            // Flip glyph at head position
            if (colHead[c] >= 0 && colHead[c] < numRows) {
                colGlyphs[c][colHead[c]] = randomGlyph();
            }
            if (colHead2[c] >= 0 && colHead2[c] < numRows) {
                colGlyphs[c][colHead2[c]] = randomGlyph();
            }
        }

        // Ambient random flip (living digital code mutation)
        if ((rand() % 3) == 0) {
            int randomR = rand() % numRows;
            colGlyphs[c][randomR] = randomGlyph();
        }
    }

    matrix->fillScreen(0);

    const int trailLength = 8;

    // Render Full Matrix Grid across every cell
    for (int c = 0; c < numColumns; c++) {
        int x = c * colPitch;

        for (int r = 0; r < numRows; r++) {
            int y = r * rowPitch;
            uint8_t glyphIndex = colGlyphs[c][r];

            int d1 = (colHead[c] >= r) ? (colHead[c] - r) : 999;
            int d2 = (colHead2[c] >= r) ? (colHead2[c] - r) : 999;
            int minDist = min(d1, d2);

            uint16_t color;
            if (minDist == 0) {
                // Brilliant White-Mint Head
                color = matrix->color565(240, 255, 240);
            } else if (minDist <= 2) {
                // Intense Neon Green
                color = matrix->color565(0, 255, 70);
            } else if (minDist <= 5) {
                // Vibrant Emerald Green
                color = matrix->color565(0, 190, 45);
            } else if (minDist <= trailLength) {
                // Fading Phosphor Green
                int g = 140 - (minDist - 5) * 25;
                if (g < 40) g = 40;
                color = matrix->color565(0, g, 15);
            } else {
                // Ambient Living Matrix Background Grid
                int bgG = 24 + ((c * 3 + r * 5 + (millis() / 200)) % 18);
                color = matrix->color565(0, bgG, 0);
            }

            matrix->drawBitmap(x, y, MATRIX_RAIN_GLYPHS[glyphIndex], 8, 8, color);
        }
    }

    // Render time on top with high-contrast halo
    drawTime();
}

void MatrixRainClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    int w = geometry.width;
    int h = geometry.height;
    const int colPitch = (w <= 32) ? 6 : (w <= 64 ? 6 : 7);
    const int rowPitch = 8;

    int newCols = w / colPitch;
    if (newCols > MAX_COLUMNS) newCols = MAX_COLUMNS;
    int newRows = h / rowPitch;
    if (newRows > MAX_ROWS) newRows = MAX_ROWS;

    numColumns = newCols;
    numRows = newRows;
    initialized = false; // Re-seed matrix grid with fresh living code
}
