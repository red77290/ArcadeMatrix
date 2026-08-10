#include "RotationManager.h"
#include "ConfigLoader.h"
#include "Logger.h"
#include <WiFi.h>

extern ConfigLoader config;

RotationManager::RotationManager(ClockEngine *c, DateEngine *d,
                                 WeatherEngine *w, GifEngine *g,
                                 FighterEngine *f, CryptoEngine *cr,
                                 StockEngine *st)
    : clockEngine(c), dateEngine(d), weatherEngine(w), gifEngine(g),
      fighterEngine(f), cryptoEngine(cr), stockEngine(st) {
  currentIndex = 0;
  moduleStartTime = 0;
}

void RotationManager::parseRotationString(const String &rotStr) {
  sequence.clear();
  String s = rotStr;
  s.toLowerCase();

  int start = 0;
  int end = s.indexOf(',');
  while (end != -1) {
    String mod = s.substring(start, end);
    mod.trim();
    if (mod == "clock")
      sequence.push_back(MODULE_CLOCK);
    else if (mod == "date")
      sequence.push_back(MODULE_DATE);
    else if (mod == "weather")
      sequence.push_back(MODULE_WEATHER);
    else if (mod == "gifs")
      sequence.push_back(MODULE_GIFS);
    else if (mod == "crypto")
      sequence.push_back(MODULE_CRYPTO);
    else if (mod == "stock" || mod == "stocks")
      sequence.push_back(MODULE_STOCKS);

    start = end + 1;
    end = s.indexOf(',', start);
  }
  // Last one
  String mod = s.substring(start);
  mod.trim();
  if (mod == "clock")
    sequence.push_back(MODULE_CLOCK);
  else if (mod == "date")
    sequence.push_back(MODULE_DATE);
  else if (mod == "weather")
    sequence.push_back(MODULE_WEATHER);
  else if (mod == "gifs")
    sequence.push_back(MODULE_GIFS);
  else if (mod == "crypto")
    sequence.push_back(MODULE_CRYPTO);
  else if (mod == "stock" || mod == "stocks")
    sequence.push_back(MODULE_STOCKS);

  if (sequence.empty()) {
    sequence.push_back(MODULE_CLOCK); // Fallback
  }
}

void RotationManager::begin(const ConfigLoader &cfg) {
  config = cfg;
  parseRotationString(cfg.idle.rotation);
  resetRotation();
}

void RotationManager::resetRotation() {
  currentIndex = 0;
  switchToModule(currentIndex);
}

void RotationManager::updateBackgroundSprites() {
  if (config.idle.fighter_enabled && !fighterEngine->isActive()) {
    fighterEngine->startFight();
  }
}

void RotationManager::switchToModule(int index) {
  if (sequence.empty())
    return;

  static int switchDepth = 0;
  if (switchDepth > sequence.size()) {
    // Infinite skip loop detected (e.g. no valid data for any module).
    // Fallback to clock to prevent stack overflow and WDT crash.
    switchDepth = 0;
    sequence.clear();
    sequence.push_back(MODULE_CLOCK);
    switchToModule(0);
    return;
  }
  switchDepth++;

  moduleStartTime = millis();
  RotationModule mod = sequence[index];

  // Stop any playing GIFs if we are leaving GIF mode
  if (mod != MODULE_GIFS && gifEngine->isActive()) {
    gifEngine->stop();
  }

  if (mod == MODULE_WEATHER) {
    // Only attempt weather if we have internet and valid data
    weatherEngine->update(config.weather.api_key,
                          config.weather.city);
    if (!weatherEngine->hasValidData() || WiFi.status() != WL_CONNECTED) {
      // Skip weather
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
  } else if (mod == MODULE_GIFS) {
    if (config.idle.gifs_count > 0 && gifEngine->hasDefaultPlaylists()) {
      gifEngine->playDefaultPlaylists(config.idle.gifs_count);
      fighterEngine->stop(); // Turn off background fighters during full-screen GIFs
    } else {
      // Skip GIFs if none configured
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
  } else if (mod == MODULE_CRYPTO) {
    if (cryptoEngine) {
      cryptoEngine->updateConfig(config.crypto);
      cryptoEngine->onDisplayStart();
    } else {
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
  } else if (mod == MODULE_STOCKS) {
    if (stockEngine) {
      stockEngine->updateConfig(config.stock);
      stockEngine->onDisplayStart();
    } else {
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
  }

  if (mod == MODULE_CLOCK || mod == MODULE_DATE || mod == MODULE_WEATHER) {
    updateBackgroundSprites();
  }
  
  const char* modNames[] = {"CLOCK", "DATE", "WEATHER", "GIFS", "CRYPTO", "STOCKS"};
  LOGI("RotationManager", "Switched to %s", modNames[mod]);
  
  switchDepth = 0;
}

bool RotationManager::loop() {
  if (sequence.empty())
    return true;

  uint32_t now = millis();
  RotationModule currentMod = sequence[currentIndex];
  bool advance = false;

  if (currentMod == MODULE_GIFS) {
    bool drewFrame = gifEngine->loop();
    // If GIF engine stopped naturally because it played all requested GIFs
    if (!gifEngine->isActive()) {
      advance = true;
    }
    
    if (advance) {
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
    }
    return drewFrame;
  } else {
    // Draw the main module first (background)
    if (currentMod == MODULE_CLOCK) {
      clockEngine->loop();
      if (now - moduleStartTime >=
          config.idle.clock_duration_sec * 1000UL)
        advance = true;
    } else if (currentMod == MODULE_DATE) {
      dateEngine->loop();
      if (now - moduleStartTime >=
          config.idle.date_duration_sec * 1000UL)
        advance = true;
    } else if (currentMod == MODULE_WEATHER) {
      weatherEngine->loop();
      if (now - moduleStartTime >=
          config.idle.weather_duration_sec * 1000UL)
        advance = true;
    } else if (currentMod == MODULE_CRYPTO) {
      if (cryptoEngine) cryptoEngine->loop();
      size_t symbolCount = 0;
      if (!config.crypto.symbols.isEmpty()) {
        symbolCount = 1;
        for (unsigned int i = 0; i < config.crypto.symbols.length(); i++) {
          if (config.crypto.symbols.charAt(i) == ',') symbolCount++;
        }
      }
      if (symbolCount == 0) symbolCount = 1;
      uint32_t perSymbolSec = config.crypto.duration_sec > 0 ? config.crypto.duration_sec : 5;
      if (now - moduleStartTime >= (perSymbolSec * symbolCount * 1000UL))
        advance = true;
    } else if (currentMod == MODULE_STOCKS) {
      if (stockEngine) stockEngine->loop();
      size_t symbolCount = 0;
      if (!config.stock.symbols.isEmpty()) {
        symbolCount = 1;
        for (unsigned int i = 0; i < config.stock.symbols.length(); i++) {
          if (config.stock.symbols.charAt(i) == ',') symbolCount++;
        }
      }
      if (symbolCount == 0) symbolCount = 1;
      uint32_t perSymbolSec = config.stock.duration_sec > 0 ? config.stock.duration_sec : 5;
      if (now - moduleStartTime >= (perSymbolSec * symbolCount * 1000UL))
        advance = true;
    }

    // Draw fighters on top of clock/date/weather
    if (config.idle.fighter_enabled) {
      fighterEngine->loop();
      fighterEngine->draw();
    }
  }

  if (advance) {
    currentIndex = (currentIndex + 1) % sequence.size();
    switchToModule(currentIndex);
  }
  return true;
}
