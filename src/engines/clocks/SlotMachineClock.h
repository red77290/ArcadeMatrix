#ifndef SLOTMACHINECLOCK_H
#define SLOTMACHINECLOCK_H

#include "../ClockEngine.h"
#include "ClockFaceFont.h"

class SlotMachineClock : public ClockFace {
public:
    SlotMachineClock(IDrawingSurface* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    ClockFaceFont faceFont;   ///< configured clock_font (shared resolver, see ClockFaceFont.h)
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
