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

ClockEngine::ClockEngine() : matrixDisplay(nullptr), activeFace(nullptr), currentTheme(THEME_NONE) {
    currentTime = {10, 42, 00};
}

ClockEngine::ClockEngine(MatrixPanel_I2S_DMA* display) : matrixDisplay(display), activeFace(nullptr), currentTheme(THEME_NONE) {
    currentTime = {10, 42, 00};
}

ClockEngine::~ClockEngine() {
    if (activeFace) delete activeFace;
}

void ClockEngine::setTheme(PublisherTheme theme, bool forceReload, const EngineConfig* config) {
    if (!forceReload && currentTheme == theme && activeFace != nullptr) {
        return;
    }
    
    if (activeFace) {
        delete activeFace;
        activeFace = nullptr;
    }
    
    currentTheme = theme;
    
    if (theme == THEME_CYBERPUNK) {
        activeFace = new CyberpunkClock(matrixDisplay, config);
    } else if (theme == THEME_FLIP) {
        activeFace = new FlipClock(matrixDisplay, config);
    } else if (theme == 22) {
        activeFace = new PongClock(matrixDisplay, config);
    } else if (theme == 23) {
        activeFace = new TetrisClock(matrixDisplay, false, config); // Normal Tetris
    } else if (theme == 29) {
        activeFace = new TetrisClock(matrixDisplay, true, config); // Gameboy Tetris
    } else if (theme == 24) {
        activeFace = new WordClock(matrixDisplay, config);
    } else if (theme == 25) {
        activeFace = new BinaryClock(matrixDisplay, config);
    } else if (theme == 26) {
        activeFace = new PacmanClock(matrixDisplay, config);
    } else if (theme == 27) {
        activeFace = new VersusClock(matrixDisplay, config);
    } else if (theme == THEME_MATRIX_RAIN) {
        activeFace = new MatrixRainClock(matrixDisplay, config);
    } else if (theme == 28) {
        activeFace = new SlotMachineClock(matrixDisplay, config);
    } else {
        ArcadeClock* arcade = new ArcadeClock(matrixDisplay, config);
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
    matrixDisplay = context->getMatrix();
    currentConfig = config;
    int theme = config ? config->getInt("clock_theme", config->getInt("theme", 0)) : 0;
    setTheme(static_cast<PublisherTheme>(theme), true, config);
    return EngineError::OK;
}

void ClockEngine::activate() {
    // Clock is active, maybe reset time fetcher
}

void ClockEngine::update(EngineContext* context) {
    if (configDirty) {
        configDirty = false;
        if (currentConfig) {
            int theme = currentConfig->getInt("clock_theme", currentConfig->getInt("theme", 0));
            setTheme(static_cast<PublisherTheme>(theme), true, currentConfig);
        }
    }
    
    if (context) {
        struct tm timeinfo;
        context->getSystemTime(&timeinfo);
        currentTime.hours = timeinfo.tm_hour;
        currentTime.minutes = timeinfo.tm_min;
        currentTime.seconds = timeinfo.tm_sec;
    }
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

void ClockEngine::onConfigChanged(const EngineConfig* config) {
    if (config) {
        currentConfig = config;
        configDirty = true;
        int theme = config->getInt("clock_theme", config->getInt("theme", 0));
        setTheme(static_cast<PublisherTheme>(theme), true, config);
    }
}

EngineDescriptor ClockEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor clockDesc;
    clockDesc.metadata = {"clock", "Clock", "info", FIRMWARE_VERSION};
    clockDesc.capabilities.realtime = true;
    clockDesc.requirements.needsAudio = false;
    clockDesc.schema.fields = {
        ConfigField("clock_theme", ConfigType::ENUM, "Clock Theme", "Visual theme / clockface", "0", false, "", "", "", "", "/api/themes", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("clock_format", ConfigType::STRING, "Time Format", "POSIX strftime format", "%H:%M:%S", false, "", "", "", "%H:%M:%S,%H:%M,%I:%M:%S %p,%I:%M %p", "", false, "", ValidationPolicy::Ignore),
        ConfigField("clock_font", ConfigType::ENUM, "Font", "Display typeface", "PressStart2P.ttf", false, "", "", "", "", "/api/fonts", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("timezone", ConfigType::ENUM, "Timezone", "Select timezone or region", "Europe/Paris", false, "", "", "", "", "/api/timezones", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("clock_size", ConfigType::INTEGER, "Font Size", "Text scaling multiplier", "2", false, "1", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("clock_color_1", ConfigType::COLOR, "Primary Color", "Custom gradient top color", "#ffffff", false, "", "", "", "", "", false, "clock_theme=20", ValidationPolicy::Ignore),
        ConfigField("clock_color_2", ConfigType::COLOR, "Secondary Color", "Custom gradient bottom color", "#ff00ff", false, "", "", "", "", "", false, "clock_theme=20", ValidationPolicy::Ignore),
        ConfigField("clock_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("clock_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    clockDesc.factory = []() { return std::unique_ptr<IEngine>(new ClockEngine()); };
    return clockDesc;
}

