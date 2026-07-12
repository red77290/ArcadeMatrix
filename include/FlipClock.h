#pragma once
#include "ClockEngine.h"

class FlipClock : public ClockFace {
public:
    FlipClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;

private:
    int prevDigits[6];
    bool flippingPanels[6];
    TimeData storedTime;
    
    // Animation state
    bool isFlipping;
    int flipFrame;
    unsigned long lastFrameTime;
    
    void drawStaticTime();
    void drawFlappingTime();
    void drawPanel(int x, int y, int w, int h, const char* text, uint16_t bgColor, uint16_t textColor, bool isFlippingPanel, int flipOffset);
};
