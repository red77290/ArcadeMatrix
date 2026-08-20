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

ClockEngine::ClockEngine() : legacy_matrix(nullptr), activeFace(nullptr), currentTheme(THEME_NONE) {
    currentTime = {10, 42, 00};
}

ClockEngine::ClockEngine(MatrixPanel_I2S_DMA* display) : legacy_matrix(display), activeFace(nullptr), currentTheme(THEME_NONE) {
    currentTime = {10, 42, 00};
}

ClockEngine::~ClockEngine() {
    if (activeFace) delete activeFace;
}

void ClockEngine::setTheme(PublisherTheme theme, bool forceReload) {
    if (!forceReload && currentTheme == theme && activeFace != nullptr) {
        return;
    }
    
    if (activeFace) {
        delete activeFace;
        activeFace = nullptr;
    }
    
    currentTheme = theme;
    
    if (theme == THEME_CYBERPUNK) {
        activeFace = new CyberpunkClock(legacy_matrix);
    } else if (theme == THEME_FLIP) {
        activeFace = new FlipClock(legacy_matrix);
    } else if (theme == 22) {
        activeFace = new PongClock(legacy_matrix);
    } else if (theme == 23) {
        activeFace = new TetrisClock(legacy_matrix, false); // Normal Tetris
    } else if (theme == 29) {
        activeFace = new TetrisClock(legacy_matrix, true); // Gameboy Tetris
    } else if (theme == 24) {
        activeFace = new WordClock(legacy_matrix);
    } else if (theme == 25) {
        activeFace = new BinaryClock(legacy_matrix);
    } else if (theme == 26) {
        activeFace = new PacmanClock(legacy_matrix);
    } else if (theme == 27) {
        activeFace = new VersusClock(legacy_matrix);
    } else if (theme == THEME_MATRIX_RAIN) {
        activeFace = new MatrixRainClock(legacy_matrix);
    } else if (theme == 28) {
        activeFace = new SlotMachineClock(legacy_matrix);
    } else {
        // Theme 0-17 (Publisher + Characters) are handled by ArcadeClock
        ArcadeClock* arcade = new ArcadeClock(legacy_matrix);
        arcade->setTheme(theme);
        activeFace = arcade;
    }
    
    if (activeFace) {
        activeFace->draw(currentTime);
    }
}

void ClockEngine::updateTime(const TimeData& t) {
    currentTime = t;
    if (activeFace) {
        activeFace->draw(t);
    }
}

bool ClockEngine::loop() {
    if (activeFace) {
        activeFace->update();
    }
    return true;
}

// =========================================================
// IEngine Implementation
// =========================================================

EngineError ClockEngine::initialize(EngineContext* context, const EngineConfig* config) {
    legacy_matrix = context->getMatrix();
    // Example: parse theme from config
    int theme = config->getInt("theme", 0);
    setTheme(static_cast<PublisherTheme>(theme), true);
    return EngineError::OK;
}

void ClockEngine::activate() {
    // Clock is active, maybe reset time fetcher
}

void ClockEngine::update(EngineContext* context) {
    if (activeFace) {
        activeFace->update();
    }
}

void ClockEngine::render(EngineContext* context) {
    if (activeFace) {
        activeFace->draw(currentTime);
    }
}

void ClockEngine::deactivate() {
    // Cleanup if needed
}
