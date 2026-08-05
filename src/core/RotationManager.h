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

enum RotationModule {
    MODULE_CLOCK,
    MODULE_DATE,
    MODULE_WEATHER,
    MODULE_GIFS,
    MODULE_CRYPTO,
    MODULE_STOCKS
};

class RotationManager {
public:
    RotationManager(ClockEngine* c, DateEngine* d, WeatherEngine* w, GifEngine* g, FighterEngine* f, CryptoEngine* cr = nullptr, StockEngine* st = nullptr);
    
    void begin(const ConfigLoader& cfg);
    bool loop();
    
    // Reset to start of rotation (e.g. after manual interruption)
    void resetRotation();
    RotationModule getCurrentModule() const { return sequence.empty() ? MODULE_CLOCK : sequence[currentIndex]; }

private:
    ClockEngine* clockEngine;
    DateEngine* dateEngine;
    WeatherEngine* weatherEngine;
    GifEngine* gifEngine;
    FighterEngine* fighterEngine;
    CryptoEngine* cryptoEngine;
    StockEngine* stockEngine;
    
    
    std::vector<RotationModule> sequence;
    int currentIndex;
    
    uint32_t moduleStartTime;
    
    void parseRotationString(const String& rotStr);
    void switchToModule(int index);
    void updateBackgroundSprites();
  };
