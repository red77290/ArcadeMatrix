#ifndef WORDCLOCK_H
#define WORDCLOCK_H

#include "../ClockEngine.h"
#include <vector>

class WordClock : public ClockFace {
public:
    WordClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    
    void renderFR(int hours, int minutes, int gfxSize);
    void renderEN(int hours, int minutes, int gfxSize);
    void renderES(int hours, int minutes, int gfxSize);
    void drawLines(const std::vector<String>& lines, int gfxSize);
};

#endif
