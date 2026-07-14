#ifndef WORDCLOCK_H
#define WORDCLOCK_H

#include "ClockEngine.h"

class WordClock : public ClockFace {
public:
    WordClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    const char* numberToFrench(int n);
};

#endif
