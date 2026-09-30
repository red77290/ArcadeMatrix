#ifndef WORDSCLOCKFACE_H
#define WORDSCLOCKFACE_H

#include "../ClockEngine.h"

/**
 * Time in words, adapted from the 64x64 "Clockwise" clockface cw-cf-0x02.
 *
 * The hour on one line and the minutes under it, in that face's two fonts, with a rule beneath and
 * the date and weekday below that. The original wraps its words inside 64 px; on a wide sign the
 * lines are laid across the full width and centred instead, which is the same picture with room to
 * breathe rather than a square block stranded in the middle.
 *
 * Wording follows the original: "noon", "midnight", "o'clock", "a half", and "oh five" for the
 * single minutes.
 */
#include "ClockFaceFont.h"

class WordsClockFace : public ClockFace {
public:
    WordsClockFace(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;
    bool wantsClear() const override { return false; }
    bool hasNewFrame() const override { return m_hasFrame; }
    void onActivated() override { m_dirty = 2; m_hasFrame = true; }

private:
    ClockFaceFont::Glow glow;   ///< resolved once at build time, never on the draw path
    TimeData storedTime;
    int lastMinute = -1;
    uint8_t m_dirty = 2;
    bool m_hasFrame = true;

    void timeInWords(int h, int m, char* hourWords, size_t hn, char* minuteWords, size_t mn) const;
    void drawCentred(const char* text, int centreY, const GFXfont* font, uint16_t color, int panelW);
};

#endif
