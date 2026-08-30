#ifndef TETRISCLOCK_H
#define TETRISCLOCK_H

#include "../ClockEngine.h"
#include <list>

struct TetrisBlock {
    int charIndex;
    float x, y;
    float tx, ty;
    float dy;
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
    
    void buildTargets(const char* timeStr, const std::vector<int>& targetIndices);
};

#endif
