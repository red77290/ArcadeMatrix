#pragma once
#include <Arduino.h>
#include <vector>
#include "ConfigLoader.h"
#include "../engines/ClockEngine.h"
#include "../engines/DateEngine.h"
#include "../engines/WeatherEngine.h"
#include "../engines/GifEngine.h"
#include "../engines/FighterEngine.h"

#include "../engines/CryptoEngine.h"
#include "../engines/StockEngine.h"
#include "../engines/TempEngine.h"
#include "../engines/DecibelEngine.h"

enum RotationModule {
    MODULE_CLOCK,
    MODULE_DATE,
    MODULE_WEATHER,
    MODULE_GIFS,
    MODULE_CRYPTO,
    MODULE_STOCKS,
    MODULE_TEMP,
    MODULE_DECIBEL
};

class RotationManager {
public:
    RotationManager(ClockEngine* c, DateEngine* d, WeatherEngine* w, GifEngine* g, FighterEngine* f, CryptoEngine* cr = nullptr, StockEngine* st = nullptr, TempEngine* t = nullptr, DecibelEngine* db = nullptr);
    
    void begin(const ConfigLoader& cfg);
    bool loop();
    
    // Reset to start of rotation (e.g. after manual interruption)
    void resetRotation();
    void parseRotationString(const String& rotStr);
    const std::vector<RotationModule>& getSequence() const { return sequence; }
    RotationModule getCurrentModule() const { return sequence.empty() ? MODULE_CLOCK : sequence[currentIndex]; }
    void setSuspended(bool suspended);
    bool isSuspended() const { return suspended; }

    // Helper: count valid non-empty comma-separated symbols
    static size_t countSymbols(const String& symbols);

private:
    ClockEngine* clockEngine;
    DateEngine* dateEngine;
    WeatherEngine* weatherEngine;
    GifEngine* gifEngine;
    FighterEngine* fighterEngine;
    CryptoEngine* cryptoEngine;
    StockEngine* stockEngine;
    TempEngine* tempEngine;
    DecibelEngine* decibelEngine;
    
    ConfigLoader config;
    std::vector<RotationModule> sequence;
    size_t currentIndex;
    uint32_t moduleStartTime;
    uint8_t switchDepth;
    bool suspended = false;

    void switchToModule(int index);
    void updateBackgroundSprites();
};
