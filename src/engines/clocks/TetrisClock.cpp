#include "TetrisClock.h"
#include "../../core/ConfigLoader.h"
#include <Adafruit_GFX.h>
#include <stdlib.h>

const uint16_t tetrisColors[7] = {
    0xF800, // Red
    0x07E0, // Green
    0x001F, // Blue
    0xFFE0, // Yellow
    0xFD20, // Orange
    0x07FF, // Cyan
    0xF81F  // Magenta
};

const uint16_t gameboyColors[4] = {
    0x0AD1, // Darkest green
    0x3306,
    0x8D61,
    0x9DE1  // Lightest green
};

TetrisClock::TetrisClock(IDrawingSurface* display, bool gameboyMode, const EngineConfig* config)
    : ClockFace(display, config), isGameboy(gameboyMode), numBlocks(0), lastFrameTime(0), canvas(nullptr) {
    storedTime = {0, 0, 0};
    strcpy(lastTimeStr, "");
    blockSize = max(1, (int)(display->height() / 16));
    faceFont.load(config);
    canvas = new (std::nothrow) GFXcanvas1(128, 64);
}

TetrisClock::~TetrisClock() {
    delete canvas;
    canvas = nullptr;
}

void TetrisClock::addBlock(const TetrisBlock& b) {
    if (numBlocks < MAX_BLOCKS) {
        blocks[numBlocks++] = b;
    }
}

void TetrisClock::draw(const TimeData& t) {
    storedTime = t;
}

// Rasterise ONE character of `str` into a 1-bit canvas at the cursor position it occupies in the full
// string, and turn every lit pixel into a falling block. Drawing the glyph alone (instead of masking a
// full-string render against a copy with the character blanked out) makes the result independent of
// the space glyph's advance and bitmap, so proportional fonts work as well as monospaced ones.
// `bx/by/bw/bh` are the bounds of the reference string so all characters share one canvas geometry.
void TetrisClock::emitBlocksFor(const char* str, int charIdx, int labelIdx, const GFXfont* font, int16_t bx, int16_t by,
                                uint16_t bw, uint16_t bh, int originX, int originY, int fallFrom, int fallJitter, uint32_t landMs) {
    int cursorX = 0;
    for (int i = 0; i < charIdx; i++) cursorX += ClockFaceFont::advance(font, str[i]);

    if (!canvas || !canvas->getBuffer()) return;
    canvas->fillScreen(0);
    canvas->setFont(font);
    canvas->setTextSize(1);
    canvas->setTextWrap(false);
    canvas->setTextColor(1);
    // the string's bounding box is normalised to canvas (2, 2); custom fonts take the cursor as baseline
    canvas->setCursor(2 - bx + cursorX, 2 - by);
    canvas->write((uint8_t)str[charIdx]);

    uint16_t maxW = min((uint16_t)(bw + 4), (uint16_t)canvas->width());
    uint16_t maxH = min((uint16_t)(bh + 4), (uint16_t)canvas->height());

    for (int py = 0; py < maxH; py++) {
        for (int px = 0; px < maxW; px++) {
            if (!canvas->getPixel(px, py)) continue;
            TetrisBlock b;
            b.charIndex = labelIdx;   // index in the full time string (colour + change tracking)
            b.x = originX + (px - 2) * blockSize;
            b.ty = originY + (py - 2) * blockSize;
            b.startY = b.ty - fallFrom - (rand() % (fallJitter + 1));
            b.y = b.startY;
            // Time-based descent: every block lands within landMs (plus a little stagger so the digit
            // cascades in) no matter how many frames the panel manages per second. The old per-frame
            // step assumed 60 fps; at the ~10-13 updates/s a 256x64 panel gets, the seconds digit never
            // finished landing before the next second tore it down again.
            b.spawnMs = millis();
            b.durationMs = landMs + (rand() % (landMs / 2 + 1));
            b.dy = 0.0f;
            b.color = isGameboy ? gameboyColors[labelIdx % 4] : tetrisColors[labelIdx % 7];
            b.state = 0; // IN
            addBlock(b);
        }
    }
}

void TetrisClock::buildTargets(const char* timeStr, const int* targetIndices, size_t targetCount) {
    int w = display->width();
    int h = display->height();
    bool isTate = (w < 48 || h > (w * 3) / 2);

    int logicalSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (logicalSize < 1) logicalSize = 1;

    int offX = engineConfig ? engineConfig->getInt("clock_offset_x", 0) : 0;
    int offY = engineConfig ? engineConfig->getInt("clock_offset_y", 0) : 0;

    // clock_speed is a percentage: 100 lands a digit in ~350 ms, 50 takes twice as long, 200 half.
    int speedPct = engineConfig ? engineConfig->getInt("clock_speed", 100) : 100;
    if (speedPct < 25) speedPct = 25;
    if (speedPct > 300) speedPct = 300;
    const uint32_t landMs = (uint32_t)(350UL * 100UL / (uint32_t)speedPct);

    int16_t bx, by;
    uint16_t bw, bh;
    GFXcanvas1 measure(8, 8);   // getTextBounds only needs a GFX context, not a real surface
    measure.setTextWrap(false); // otherwise bounds of a string wider than 8 px would be computed as wrapped lines

    if (isTate) {
        // Stacked Portrait Layout: 3 tiers (HH, MM, SS), each 2 characters
        char tierStrs[3][4];
        tierStrs[0][0] = timeStr[0]; tierStrs[0][1] = timeStr[1]; tierStrs[0][2] = '\0';
        tierStrs[1][0] = timeStr[3]; tierStrs[1][1] = timeStr[4]; tierStrs[1][2] = '\0';
        tierStrs[2][0] = timeStr[6]; tierStrs[2][1] = timeStr[7]; tierStrs[2][2] = '\0';

        // the configured font, unless "88" cannot fit one tier even at block size 1
        const GFXfont* font = faceFont.apply(measure, 1, "88", w, h / 3);
        measure.getTextBounds("88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0 || bh == 0) { bw = 11; bh = 7; }

        int sMax = min((int)(w / bw), (int)((h / 3) / bh));
        if (sMax < 1) sMax = 1;
        blockSize = max(1, min(logicalSize, sMax));

        int scaledW = bw * blockSize;
        int scaledH = bh * blockSize;
        int tierStartX = (w - scaledW) / 2 + offX;
        int tierY[3] = {
            (h / 6) - (scaledH / 2) + offY,
            (h / 2) - (scaledH / 2) + offY,
            (5 * h / 6) - (scaledH / 2) + offY
        };

        for (size_t k = 0; k < targetCount; k++) {
            int charIdx = targetIndices[k];
            if (charIdx == 2 || charIdx == 5) continue; // no colons in stacked mode
            int tier = (charIdx < 2) ? 0 : (charIdx < 5 ? 1 : 2);
            int charInTier = (charIdx < 2) ? charIdx : (charIdx < 5 ? charIdx - 3 : charIdx - 6);
            // drawn from the 2-character tier string, labelled with the full-string index
            emitBlocksFor(tierStrs[tier], charInTier, charIdx, font, bx, by, bw, bh, tierStartX, tierY[tier], h / 3, h / 4, landMs);
        }
    } else {
        // Landscape / Widescreen Layout (single row "HH:MM:SS")
        const GFXfont* font = faceFont.apply(measure, 1, timeStr, w, h);
        measure.getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0 || bh == 0) { bw = 48; bh = 7; }

        int sMax = min((int)(w / bw), (int)(h / bh));
        if (sMax < 1) sMax = 1;
        blockSize = max(1, min(logicalSize, sMax));

        int scaledW = bw * blockSize;
        int scaledH = bh * blockSize;
        int startX = (w - scaledW) / 2 + offX;
        int startY = (h - scaledH) / 2 + offY;

        for (size_t k = 0; k < targetCount; k++) {
            int charIdx = targetIndices[k];
            emitBlocksFor(timeStr, charIdx, charIdx, font, bx, by, bw, bh, startX, startY, h, h / 2, landMs);
        }
    }
}

void TetrisClock::update() {
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    
    if (strcmp(timeStr, lastTimeStr) != 0) {
        if (strlen(timeStr) != strlen(lastTimeStr) || numBlocks == 0) {
            for (size_t i = 0; i < numBlocks; i++) {
                TetrisBlock& b = blocks[i];
                b.state = 2; // OUT
                float base_dy = max(1.0f, display->height() / 40.0f);
                b.dy = base_dy * 0.5f + (((float)rand() / RAND_MAX) * base_dy * 0.5f);
            }
            int allIndices[10];
            int len = strlen(timeStr);
            for (int i = 0; i < len; i++) allIndices[i] = i;
            buildTargets(timeStr, allIndices, len);
        } else {
            int changedIndices[10];
            size_t changedCount = 0;
            int len = strlen(timeStr);
            for (int i = 0; i < len; i++) {
                if (timeStr[i] != lastTimeStr[i]) {
                    changedIndices[changedCount++] = i;
                }
            }
            if (changedCount > 0) {
                for (size_t i = 0; i < numBlocks; i++) {
                    TetrisBlock& b = blocks[i];
                    if (b.state != 2) {
                        for (size_t c = 0; c < changedCount; c++) {
                            if (b.charIndex == changedIndices[c]) {
                                b.state = 2; // OUT
                                float base_dy = max(1.5f, display->height() / 15.0f);
                                b.dy = base_dy * 0.5f + (((float)rand() / RAND_MAX) * base_dy * 0.5f);
                                break;
                            }
                        }
                    }
                }
                buildTargets(timeStr, changedIndices, changedCount);
            }
        }
        strcpy(lastTimeStr, timeStr);
    }
    
    // Frame-rate independent physics
    unsigned long currentMillis = millis();
    float dt = (currentMillis - lastFrameTime);
    if (dt > 100) dt = 100.0f; // Cap at 100ms to avoid huge jumps, but don't reset to 16ms!
    lastFrameTime = currentMillis;
    float timeScale = dt / 16.0f; // Scale relative to 60fps
        
    size_t writeIdx = 0;
    for (size_t i = 0; i < numBlocks; i++) {
        TetrisBlock& b = blocks[i];
        if (b.state == 0) { // IN: position is a function of elapsed time, not of frames rendered
            float p = b.durationMs ? (float)(currentMillis - b.spawnMs) / (float)b.durationMs : 1.0f;
            if (p >= 1.0f) {
                b.y = b.ty;
                b.state = 1; // FIXED
            } else {
                b.y = b.startY + (b.ty - b.startY) * (p * p);   // accelerating, like gravity
            }
            blocks[writeIdx++] = b;
        } else if (b.state == 2) { // OUT
            b.y += b.dy * timeScale;
            b.dy += 0.4f * timeScale; // Gravity matches v3.0.0
            if (b.y <= display->height()) {
                blocks[writeIdx++] = b;
            }
        } else {
            blocks[writeIdx++] = b; // FIXED
        }
    }
    numBlocks = writeIdx;

    // Clear and draw
    if (display) {
        display->fillScreen(0);
        for (size_t i = 0; i < numBlocks; i++) {
            const TetrisBlock& b = blocks[i];
            display->fillRect((int)b.x, (int)b.y, blockSize, blockSize, b.color);
        }
    }
}

void TetrisClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    (void)geometry;
    numBlocks = 0;
    strcpy(lastTimeStr, "");
    if (display) display->fillScreen(0);
}

