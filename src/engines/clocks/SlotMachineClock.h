#ifndef SLOTMACHINECLOCK_H
#define SLOTMACHINECLOCK_H

#include "ClockEngine.h"

struct SlotDigit {
    char targetChar;
    char currentChar;
    float yOffset;
    float speed;
    bool spinning;
};

class SlotMachineClock : public ClockFace {
public:
    SlotMachineClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    char lastTimeStr[12];
    SlotDigit digits[12];
    int numDigits;
    uint32_t lastFrameTime;
};

#endif
