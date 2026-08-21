#ifndef MATRIXRAINCLOCK_H
#define MATRIXRAINCLOCK_H

#include "../ClockEngine.h"

// Character-based "digital rain" clock face, mirroring ArcadeMatrix_RPi's TrueMatrixRenderer
// (theme ID 21). Unlike CyberpunkClock (theme 18), which only animates single falling pixels,
// this draws actual falling glyphs using the built-in Adafruit GFX font, closer to the classic
// "Matrix" look, with the current time overlaid in the center.
class MatrixRainClock : public ClockFace {
public:
    MatrixRainClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    static const int MAX_COLUMNS = 32; // Covers panels up to 256px wide (256 / 8px per glyph column)
    int8_t colHead[MAX_COLUMNS];   // Head row (in glyph rows, not pixels) of each column's drop, may be negative (off-screen, not yet visible)
    int8_t colSpeedDivider[MAX_COLUMNS]; // Larger = slower (drop advances one row every N update() calls)
    uint8_t colTick[MAX_COLUMNS];
    uint8_t colGlyphs[MAX_COLUMNS][16]; // Cached glyph index per column/row so the trail doesn't flicker every frame
    int numColumns;
    bool initialized;
    TimeData storedTime;
    unsigned long lastFrameTime;

    void drawTime();
};

#endif
