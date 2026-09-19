#ifndef SLOTMACHINECLOCK_H
#define SLOTMACHINECLOCK_H

#include "../ClockEngine.h"

class SlotMachineClock : public ClockFace {
public:
    SlotMachineClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    int lastMinute;
    uint32_t animFrame;
    bool spinning;
    float spinSpeed;
    float yOffset;
    unsigned long lastSpinMs = 0;   ///< time base for the frame-rate independent spin decay
    char currentTime[12];
    char targetTime[12];
    uint32_t lastFrameTime;
};

#endif
