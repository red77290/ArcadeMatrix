#ifndef VERSUSCLOCK_H
#define VERSUSCLOCK_H

#include "../ClockEngine.h"

class VersusClock : public ClockFace {
public:
    VersusClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override {}

private:
    TimeData storedTime;
    int lastMinute;
    bool animating;
    float currentP1HP;
    float targetP1HP;
    float currentP2HP;
    float targetP2HP;
    uint32_t lastFrameTime;
    uint32_t animFrame;
    
    void drawHealthBar(int x, int y, int width, int height, float hpPercent, bool isPlayer1);
    void drawKO(int x, int y);
};

#endif
