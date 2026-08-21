#pragma once
#include "../ClockEngine.h"

class CyberpunkClock : public ClockFace {
public:
    CyberpunkClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    unsigned long lastFrameTime;
    int lineY;
    
    void drawTime();
};
