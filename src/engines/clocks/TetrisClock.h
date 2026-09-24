#ifndef TETRISCLOCK_H
#define TETRISCLOCK_H

#include "../ClockEngine.h"
#include "ClockFaceFont.h"
#include <list>

struct TetrisBlock {
    int charIndex;
    float x, y;
    float tx, ty;
    float dy;              // OUT: fall speed (px per 60 fps frame)
    float startY;          // IN: where the block spawned above its target
    uint32_t spawnMs;      // IN: when it spawned
    uint32_t durationMs;   // IN: how long it takes to land, whatever the frame rate
    uint16_t color;
    int state; // 0=in, 1=fixed, 2=out
};

class TetrisClock : public ClockFace {
public:
    TetrisClock(MatrixPanel_I2S_DMA* display, bool gameboyMode = false, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

private:
    bool isGameboy;
    TimeData storedTime;
    std::list<TetrisBlock> blocks;
    char lastTimeStr[12];
    uint32_t lastFrameTime;
    int blockSize;
    ClockFaceFont faceFont;   ///< configured clock_font; the block digits are shaped from its glyphs

    void buildTargets(const char* timeStr, const int* targetIndices, size_t targetCount);
    void emitBlocksFor(const char* str, int charIdx, int labelIdx, const GFXfont* font, int16_t bx, int16_t by,
                       uint16_t bw, uint16_t bh, int originX, int originY, int fallFrom, int fallJitter, uint32_t landMs);
};

#endif
