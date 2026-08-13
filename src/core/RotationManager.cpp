#include "RotationManager.h"
#include "ConfigLoader.h"
#include "Logger.h"
#include <WiFi.h>

extern ConfigLoader config;

RotationManager::RotationManager(ClockEngine *c, DateEngine *d,
                                 WeatherEngine *w, GifEngine *g,
                                 FighterEngine *f, CryptoEngine *cr,
                                 StockEngine *st, TempEngine *t,
                                 DecibelEngine *db)
    : clockEngine(c), dateEngine(d), weatherEngine(w), gifEngine(g),
      fighterEngine(f), cryptoEngine(cr), stockEngine(st),
      tempEngine(t), decibelEngine(db) {
  currentIndex = 0;
  moduleStartTime = 0;
}
size_t RotationManager::countSymbols(const String& symbols) {
  if (symbols.length() == 0) return 0;
  size_t count = 0;
  int start = 0;
  int len = symbols.length();
  while (start < len) {
    int comma = symbols.indexOf(',', start);
    String token;
    if (comma == -1) {
      token = symbols.substring(start);
      start = len;
    } else {
      token = symbols.substring(start, comma);
      start = comma + 1;
    }
    token.trim();
    if (token.length() > 0) {
      count++;
    }
  }
  return count;
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
    else if (mod == "temp" || mod == "temperature")
      sequence.push_back(MODULE_TEMP);
    else if (mod == "decibel" || mod == "db")
      sequence.push_back(MODULE_DECIBEL);

    start = end + 1;
    end = s.indexOf(',', start);
  }
  // Last item
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
  else if (mod == "temp" || mod == "temperature")
    sequence.push_back(MODULE_TEMP);
  else if (mod == "decibel" || mod == "db")
    sequence.push_back(MODULE_DECIBEL);

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
  if (switchDepth > (int)sequence.size()) {
    // Infinite skip loop protection
    switchDepth = 0;
    sequence.clear();
    sequence.push_back(MODULE_CLOCK);
    switchToModule(0);
    return;
  }
  switchDepth++;

  moduleStartTime = millis();
  RotationModule mod = sequence[index];

  // Deactivate Decibel audio sampling if leaving Decibel mode
  if (mod != MODULE_DECIBEL && decibelEngine && decibelEngine->isActive()) {
    decibelEngine->onDeactivate();
  }

  // Stop any playing GIFs if leaving GIF mode
  if (mod != MODULE_GIFS && gifEngine->isActive()) {
    gifEngine->stop();
  }

  if (mod == MODULE_WEATHER) {
    weatherEngine->update(config.weather.api_key, config.weather.city);
    if (!weatherEngine->hasValidData() || WiFi.status() != WL_CONNECTED) {
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
  } else if (mod == MODULE_GIFS) {
    fighterEngine->stop();
    if (config.idle.gifs_count > 0 && gifEngine->hasDefaultPlaylists()) {
      gifEngine->playDefaultPlaylists(config.idle.gifs_count);
    } else {
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
  } else if (mod == MODULE_CRYPTO) {
    if (!config.crypto.enabled || countSymbols(config.crypto.symbols) == 0 || !cryptoEngine) {
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
    cryptoEngine->updateConfig(config.crypto);
    cryptoEngine->onDisplayStart();
  } else if (mod == MODULE_STOCKS) {
    if (!config.stock.enabled || countSymbols(config.stock.symbols) == 0 || !stockEngine) {
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
    stockEngine->updateConfig(config.stock);
    stockEngine->onDisplayStart();
  } else if (mod == MODULE_TEMP) {
    if (!hardwareHAL.isTempSensorAvailable()) {
      // Auto-skip Temp module if physical sensor is missing
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
  } else if (mod == MODULE_DECIBEL) {
    if (!hardwareHAL.isAudioAvailable()) {
      // Auto-skip Decibel module if audio input is missing
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
      return;
    }
    if (decibelEngine) {
      decibelEngine->onActivate();
    }
  }

  if (mod == MODULE_CLOCK || mod == MODULE_DATE || mod == MODULE_WEATHER || mod == MODULE_TEMP || mod == MODULE_DECIBEL) {
    updateBackgroundSprites();
  }
  
  const char* modNames[] = {"CLOCK", "DATE", "WEATHER", "GIFS", "CRYPTO", "STOCKS", "TEMP", "DECIBEL"};
  LOGI("RotationManager", "Switched to %s", modNames[mod]);
  
  switchDepth = 0;
}

bool RotationManager::loop() {
  if (sequence.empty())
    return true;

  uint32_t now = millis();
  RotationModule currentMod = sequence[currentIndex];
  bool advance = false;

  // Single module in rotation sequence (Solo mode): do NOT advance timer
  bool isSoloMode = (sequence.size() == 1);

  if (currentMod == MODULE_GIFS) {
    bool drewFrame = gifEngine->loop();
    if (!gifEngine->isActive()) {
      advance = true;
    }
    if (advance && !isSoloMode) {
      currentIndex = (currentIndex + 1) % sequence.size();
      switchToModule(currentIndex);
    }
    return drewFrame;
  } else {
    if (currentMod == MODULE_CLOCK) {
      clockEngine->loop();
      if (!isSoloMode && (now - moduleStartTime >= config.idle.clock_duration_sec * 1000UL))
        advance = true;
    } else if (currentMod == MODULE_DATE) {
      dateEngine->loop();
      if (!isSoloMode && (now - moduleStartTime >= config.idle.date_duration_sec * 1000UL))
        advance = true;
    } else if (currentMod == MODULE_WEATHER) {
      weatherEngine->loop();
      if (!isSoloMode && (now - moduleStartTime >= config.idle.weather_duration_sec * 1000UL))
        advance = true;
    } else if (currentMod == MODULE_CRYPTO) {
      if (cryptoEngine) cryptoEngine->loop();
      size_t symbolCount = countSymbols(config.crypto.symbols);
      uint32_t perSymbolSec = config.crypto.duration_sec > 0 ? config.crypto.duration_sec : 5;
      uint32_t totalDurationMs = perSymbolSec * symbolCount * 1000UL;
      if (!isSoloMode && (symbolCount == 0 || now - moduleStartTime >= totalDurationMs))
        advance = true;
    } else if (currentMod == MODULE_STOCKS) {
      if (stockEngine) stockEngine->loop();
      size_t symbolCount = countSymbols(config.stock.symbols);
      uint32_t perSymbolSec = config.stock.duration_sec > 0 ? config.stock.duration_sec : 5;
      uint32_t totalDurationMs = perSymbolSec * symbolCount * 1000UL;
      if (!isSoloMode && (symbolCount == 0 || now - moduleStartTime >= totalDurationMs))
        advance = true;
    } else if (currentMod == MODULE_TEMP) {
      if (tempEngine) tempEngine->loop();
      uint32_t dur = config.idle.temp_duration_sec >= 3 ? config.idle.temp_duration_sec : 8;
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL))
        advance = true;
    } else if (currentMod == MODULE_DECIBEL) {
      if (decibelEngine) decibelEngine->loop();
      uint32_t dur = config.idle.decibel_duration_sec >= 3 ? config.idle.decibel_duration_sec : 10;
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL))
        advance = true;
    }

    if (config.idle.fighter_enabled) {
      fighterEngine->loop();
      fighterEngine->draw();
    }
  }

  if (advance && !isSoloMode) {
    currentIndex = (currentIndex + 1) % sequence.size();
    switchToModule(currentIndex);
  }
  return true;
}
