#ifndef WORDCLOCK_H
#define WORDCLOCK_H

#include "../ClockEngine.h"
#include "../../core/BitmapFontLoader.h"
#include <vector>

struct WordClockLine {
    char text[32];
    int16_t x;
    int16_t y;
    uint16_t color;
};

class WordClock : public ClockFace {
public:
    WordClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    BitmapFontLoader customFont;
    
    // Cached layout to eliminate per-frame allocations on Core 1 (Invariant 1)
    WordClockLine _cachedLines[8];
    uint8_t _cachedLineCount = 0;
    const GFXfont* _cachedFont = nullptr;
    int _cachedGfxSize = 1;
    int _lastHours = -1;
    int _lastMinutes = -1;
    bool _dirty = true;

    void recomputeLines();
};

#endif
