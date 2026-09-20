#ifndef PACMANCLOCK_H
#define PACMANCLOCK_H

#include "../ClockEngine.h"
#include "ClockFaceFont.h"

/**
 * Pac-Man clock face, modelled on the arcade desk clock.
 *
 * Shows the time (HH:MM, or HH:MM:SS when the format has seconds) in the configured font with a
 * blinking colon. Each time the face comes on screen a full-height Pac-Man followed by the four ghosts
 * parades across the panel over it, then the ghosts flee back frightened and blue with Pac-Man chasing
 * (and again on each minute change if it stays up). Sprites are classic 13x13 / 14x14 pixel art scaled by an integer factor to the panel; the
 * parade is clocked on millis() so it runs at the same speed at any frame rate, and `clock_speed`
 * scales it.
 */
class PacmanClock : public ClockFace {
public:
    PacmanClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    TimeData storedTime;
    char oldTimeStr[12];
    char newTimeStr[12];
    bool primed;                ///< first time string captured
    uint32_t lastUpdateMs;      ///< last update(); a gap means the face was off screen
    uint32_t paradeDueMs;       ///< scheduled parade start (0 = none)
    int lastMinute;
    bool transitioning;
    uint32_t transStartMs;      ///< millis() when the current parade started
    float pacX;                 ///< distance travelled along the parade path, in pixels
    uint16_t ghostColors[4];
    ClockFaceFont faceFont;

    void formatTime(char* out, size_t n) const;
    static void splitTime(const char* str, char* hours, char* minutes);
    void printTime(const char* str, int centreX, int centreY, int scale, const GFXfont* font,
                   uint16_t digitColor, uint16_t colonColor, int minX = -1000, int maxX = 10000);
    void blit(const uint16_t* rows, int nRows, int nCols, int left, int top, int s, uint16_t color, bool mirror);
    void drawPacman(int cx, int cy, int s, int frame, bool facingRight);
    void drawGhost(int cx, int cy, int s, uint16_t color, int skirtFrame, bool lookRight, bool frightened);
};

#endif
