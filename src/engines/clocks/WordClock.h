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

#include "ClockFaceFont.h"

class WordClock : public ClockFace {
public:
    WordClock(IDrawingSurface* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;
    bool wantsClear() const override { return false; }
    bool hasNewFrame() const override { return m_hasFrame; }
    void onActivated() override { _dirty = 2; m_hasFrame = true; }

private:
    ClockFaceFont::Glow glow;   ///< resolved once at build time, never on the draw path
    TimeData storedTime;
    BitmapFontLoader customFont;
    
    // Cached layout to eliminate per-frame allocations on Core 1 (Invariant 1)
    WordClockLine _cachedLines[8];
    uint8_t _cachedLineCount = 0;
    const GFXfont* _cachedFont = nullptr;
    int _cachedGfxSize = 1;
    int _lastHours = -1;
    int _lastMinutes = -1;
    uint8_t _dirty = 2;
    bool m_hasFrame = true;

    void recomputeLines();
};

#endif
