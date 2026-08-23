#ifndef PONGCLOCK_H
#define PONGCLOCK_H

#include "../ClockEngine.h"

class PongClock : public ClockFace {
public:
    PongClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    int lastMinute;
    int lastHour;
    
    float ball_x, ball_y;
    float ball_dx, ball_dy;
    int ball_size;
    
    int pad_w, pad_h;
    float p1_y, p2_y;
    
    bool forceMissLeft;
    bool forceMissRight;
    uint32_t lastFrameTime;
    
    void resetBall(bool leftServed);
    void drawScores();
};

#endif
