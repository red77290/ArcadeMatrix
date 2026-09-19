#pragma once
#include "../ClockEngine.h"
#include "ClockFaceFont.h"

class CyberpunkClock : public ClockFace {
public:
    CyberpunkClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    ClockFaceFont faceFont;   ///< configured clock_font (shared resolver, see ClockFaceFont.h)
    TimeData storedTime;
    unsigned long lastFrameTime;
    int lineY;
    
    void drawTime();
};
