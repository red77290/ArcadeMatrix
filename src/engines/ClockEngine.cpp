#include "ClockEngine.h"
#include "clocks/ArcadeClock.h"
#include "clocks/CyberpunkClock.h"
#include "clocks/FlipClock.h"
#include "clocks/PongClock.h"
#include "clocks/TetrisClock.h"
#include "clocks/WordClock.h"
#include "clocks/BinaryClock.h"
#include "clocks/PacmanClock.h"
#include "clocks/VersusClock.h"
#include "clocks/SlotMachineClock.h"
#include "clocks/MatrixRainClock.h"

ClockEngine::ClockEngine(MatrixPanel_I2S_DMA* display) : matrix(display), activeFace(nullptr), currentTheme(THEME_NONE) {
    currentTime = {10, 42, 00};
}

ClockEngine::~ClockEngine() {
    if (activeFace) delete activeFace;
}

void ClockEngine::setTheme(PublisherTheme theme) {
    if (activeFace) {
        delete activeFace;
        activeFace = nullptr;
    }
    
    currentTheme = theme;
    
    if (theme == THEME_CYBERPUNK) {
        activeFace = new CyberpunkClock(matrix);
    } else if (theme == THEME_FLIP) {
        activeFace = new FlipClock(matrix);
    } else if (theme == 22) {
        activeFace = new PongClock(matrix);
    } else if (theme == 23) {
        activeFace = new TetrisClock(matrix, false); // Normal Tetris
    } else if (theme == 29) {
        activeFace = new TetrisClock(matrix, true); // Gameboy Tetris
    } else if (theme == 24) {
        activeFace = new WordClock(matrix);
    } else if (theme == 25) {
        activeFace = new BinaryClock(matrix);
    } else if (theme == 26) {
        activeFace = new PacmanClock(matrix);
    } else if (theme == 27) {
        activeFace = new VersusClock(matrix);
    } else if (theme == THEME_MATRIX_RAIN) {
        activeFace = new MatrixRainClock(matrix);
    } else if (theme == 28) {
        activeFace = new SlotMachineClock(matrix);
    } else {
        // Theme 0-17 (Publisher + Characters) are handled by ArcadeClock
        ArcadeClock* arcade = new ArcadeClock(matrix);
        arcade->setTheme(theme);
        activeFace = arcade;
    }
}

void ClockEngine::updateTime(const TimeData& t) {
    currentTime = t;
    if (activeFace) {
        activeFace->draw(t);
    }
}

void ClockEngine::loop() {
    if (activeFace) {
        activeFace->update();
    }
}
