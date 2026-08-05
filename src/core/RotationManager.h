#pragma once
#include <Arduino.h>
#include <vector>
#include "ConfigLoader.h"
#include "../engines/ClockEngine.h"
#include "../engines/DateEngine.h"
#include "../engines/WeatherEngine.h"
#include "../engines/GifEngine.h"
#include "../engines/FighterEngine.h"

enum RotationModule {
    MODULE_CLOCK,
    MODULE_DATE,
    MODULE_WEATHER,
    MODULE_GIFS
};

class RotationManager {
public:
    RotationManager(ClockEngine* c, DateEngine* d, WeatherEngine* w, GifEngine* g, FighterEngine* f);
    
    void begin(const ConfigLoader& cfg);
    void loop();
    
    // Reset to start of rotation (e.g. after manual interruption)
    void resetRotation();
    RotationModule getCurrentModule() const { return sequence.empty() ? MODULE_CLOCK : sequence[currentIndex]; }

private:
    ClockEngine* clockEngine;
    DateEngine* dateEngine;
    WeatherEngine* weatherEngine;
    GifEngine* gifEngine;
    FighterEngine* fighterEngine;
    
    
    std::vector<RotationModule> sequence;
    int currentIndex;
    
    uint32_t moduleStartTime;
    
    void parseRotationString(const String& rotStr);
    void switchToModule(int index);
    void updateBackgroundSprites();
  };
