#ifndef TETRISCLOCK_H
#define TETRISCLOCK_H

#include "../ClockEngine.h"
#include "ClockFaceFont.h"
#include <Adafruit_GFX.h>

struct TetrisBlock {
    float y;
    float dy;              // OUT: fall speed (px per 60 fps frame)
    uint32_t spawnMs;      // IN: when it spawned
    int16_t x;             // X position (fixed to tx)
    int16_t ty;            // target Y
    int16_t startY;        // IN: where the block spawned above its target
    uint16_t durationMs;   // IN: how long it takes to land, whatever the frame rate
    uint16_t color;
    int8_t charIndex;
    int8_t state;          // 0=in, 1=fixed, 2=out
};

class TetrisClock : public ClockFace {
public:
    TetrisClock(MatrixPanel_I2S_DMA* display, bool gameboyMode = false, const EngineConfig* config = nullptr);
    ~TetrisClock() override;

    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

private:
    static constexpr size_t MAX_BLOCKS = 240;

    bool isGameboy;
    TimeData storedTime;
    TetrisBlock blocks[MAX_BLOCKS];
    size_t numBlocks;
    char lastTimeStr[12];
    uint32_t lastFrameTime;
    int blockSize;
    ClockFaceFont faceFont;   ///< configured clock_font; the block digits are shaped from its glyphs
    GFXcanvas1* canvas;       ///< Pre-allocated 1-bit raster canvas

    void addBlock(const TetrisBlock& b);
    void buildTargets(const char* timeStr, const int* targetIndices, size_t targetCount);
    void emitBlocksFor(const char* str, int charIdx, int labelIdx, const GFXfont* font, int16_t bx, int16_t by,
                       uint16_t bw, uint16_t bh, int originX, int originY, int fallFrom, int fallJitter, uint32_t landMs);
};

#endif

