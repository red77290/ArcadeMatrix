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
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

private:
    static const int MAX_COLUMNS = 48; // Covers up to 256px wide
    static const int MAX_ROWS = 32;    // Covers up to 256px tall
    int8_t colHead[MAX_COLUMNS];       // Primary drop head
    int8_t colHead2[MAX_COLUMNS];      // Secondary staggered drop head
    int8_t colSpeedDivider[MAX_COLUMNS]; // Speed divider
    uint8_t colTick[MAX_COLUMNS];
    uint8_t colGlyphs[MAX_COLUMNS][MAX_ROWS]; // Living glyph per cell
    int numColumns;
    int numRows;
    bool initialized;
    TimeData storedTime;
    unsigned long lastFrameTime;

    void drawTime();
};

#endif
