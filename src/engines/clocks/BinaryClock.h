#ifndef BINARYCLOCK_H
#define BINARYCLOCK_H

#include "../ClockEngine.h"

class BinaryClock : public ClockFace {
public:
    BinaryClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
};

#endif
