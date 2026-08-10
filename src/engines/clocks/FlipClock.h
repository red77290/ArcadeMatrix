#pragma once
#include "../ClockEngine.h"

class FlipClock : public ClockFace {
public:
    FlipClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;

private:
    int prevDigits[6];
    int oldDigits[6];
    int flipFrame[6];
    TimeData storedTime;
    
    unsigned long lastFrameTime;
    
    void drawTime();
    void drawPanel(int x, int y, int w, int h, const char* curText, const char* oldText, uint16_t bgColor, uint16_t textColor, int frame);
};
