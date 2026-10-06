#ifndef TETRISCLOCK_H
#define TETRISCLOCK_H

#include "../ClockEngine.h"
#include "ClockFaceFont.h"
#include <Adafruit_GFX.h>

// 20 bytes per block (512 of them live in one contiguous allocation inside TetrisClock, which has to
// fit in the largest free heap block on a fragmented classic ESP32). Colour is derived from charIndex,
// and the IN-only landing fields share storage with the OUT-only fall speed: a block never needs both.
struct TetrisBlock {
    float y;
    uint32_t spawnMs;      // IN: when it spawned
    union {
        float dy;          // OUT: fall speed (px per 60 fps frame)
        struct {
            int16_t startY;      // IN: where the block spawned above its target
            uint16_t durationMs; // IN: how long it takes to land, whatever the frame rate
        };
    };
    int16_t x;             // X position (fixed to tx)
    int16_t ty;            // target Y
    int8_t charIndex;
    int8_t state; // 0=in, 1=fixed, 2=out
    uint8_t edges;
};

class TetrisClock : public ClockFace {
public:
    TetrisClock(IDrawingSurface* display, bool gameboyMode = false, const EngineConfig* config = nullptr);
    ~TetrisClock() override;

    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

private:
    static constexpr size_t MAX_BLOCKS = 512;

    bool isGameboy;
    TimeData storedTime;
    TetrisBlock blocks[MAX_BLOCKS];
    size_t numBlocks;
    char lastTimeStr[12];
    uint32_t lastFrameTime;
    int blockSize;
    ClockFaceFont faceFont;   ///< configured clock_font; the block digits are shaped from its glyphs
    ClockFaceFont::Glow glow; ///< resolved once at build time, never on the draw path

    /// Trace the outline of the digits the blocks are falling into, one pixel outside their cells.
    void drawOutline();
    GFXcanvas1* canvas;       ///< Pre-allocated 1-bit raster canvas

    void addBlock(const TetrisBlock& b);
    uint16_t blockColor(int8_t charIndex) const;
    void buildTargets(const char* timeStr, const int* targetIndices, size_t targetCount);
    void emitBlocksFor(const char* str, int charIdx, int labelIdx, const GFXfont* font, int16_t bx, int16_t by,
                       uint16_t bw, uint16_t bh, int originX, int originY, int fallFrom, int fallJitter, uint32_t landMs);
};

#endif

