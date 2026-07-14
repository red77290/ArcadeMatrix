#ifndef TETRISCLOCK_H
#define TETRISCLOCK_H

#include "ClockEngine.h"
#include <vector>

struct TetrisBlock {
    float x, y;
    float tx, ty;
    float dy;
    uint16_t color;
    int state; // 0=in, 1=fixed, 2=out
};

class TetrisClock : public ClockFace {
public:
    TetrisClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    std::vector<TetrisBlock> blocks;
    char lastTimeStr[12];
    uint32_t lastFrameTime;
    int blockSize;
    
    void buildTargets(const char* timeStr);
};

#endif
