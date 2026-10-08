#ifndef BINARYCLOCK_H
#define BINARYCLOCK_H

#include "../ClockEngine.h"

class BinaryClock : public ClockFace {
public:
    BinaryClock(IDrawingSurface* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
};

#endif
