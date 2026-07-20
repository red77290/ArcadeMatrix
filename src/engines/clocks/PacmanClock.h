#ifndef PACMANCLOCK_H
#define PACMANCLOCK_H

#include "ClockEngine.h"

class PacmanClock : public ClockFace {
public:
    PacmanClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    char oldTimeStr[12];
    char newTimeStr[12];
    
    int lastMinute;
    bool transitioning;
    float pacX;
    uint32_t animFrame;
    uint32_t lastFrameTime;
    
    uint16_t ghostColors[4];
    
    void drawPacman(int x, int y, int radius, int mouthAngle, bool facingRight);
    void drawGhost(int x, int y, int radius, uint16_t color, int tickCount);
};

#endif
