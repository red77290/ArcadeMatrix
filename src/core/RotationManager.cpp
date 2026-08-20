#include "../../include/core/EngineRegistry.h"
#include "LegacyConfigAdapter.h"
#include "RotationManager.h"
#include "ConfigLoader.h"
#include "Logger.h"
#include <WiFi.h>

extern ConfigLoader config;

RotationManager::RotationManager(ClockEngine *c,
                                 WeatherEngine *w, GifEngine *g,
                                 FighterEngine *f, CryptoEngine *cr,
                                 StockEngine *st, TempEngine *t,
                                 DecibelEngine *db)
    : clockEngine(c), weatherEngine(w), gifEngine(g),
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


void RotationManager::begin(const ConfigLoader &cfg) {
  config = cfg;
  activeEngines.clear();
  
  for (const auto& inst : config.instances) {
      if (inst.engine_id == "date") { // We are migrating DateEngine first
          auto desc = EngineRegistry::getDescriptor(inst.engine_id.c_str());
          std::unique_ptr<IEngine> engine = nullptr;
          if (desc && desc->factory) {
              engine = desc->factory();
          }
          if (engine) {
              LegacyConfigAdapter adapter(config, inst.engine_id);
              engine->initialize(m_ctx, &adapter);
              activeEngines[inst.instance_id] = std::move(engine);
          }
      }
  }
  resetRotation();
}

IEngine* RotationManager::getActiveEngine(const String& instanceId) {
    auto it = activeEngines.find(instanceId);
    if (it != activeEngines.end()) {
        return it->second.get();
    }
    return nullptr;
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
  if (config.rotation.empty())
    return;

  static int switchDepth = 0;
  if (switchDepth > (int)config.rotation.size()) {
    switchDepth = 0;
    return; // Infinite skip loop protection
  }
  switchDepth++;

  moduleStartTime = millis();
  String inst_id = config.rotation[index].instance_id;
  String mod = inst_id; // Default to instance_id for legacy compatibility
  for (const auto& inst : config.instances) {
      if (inst.instance_id == inst_id) {
          mod = inst.engine_id;
          break;
      }
  }

  // Deactivate Decibel audio sampling if leaving Decibel mode
  if (mod != "decibel" && decibelEngine && decibelEngine->isActive()) {
    decibelEngine->onDeactivate();
  }

  // Stop any playing GIFs if leaving GIF mode
  if (mod != "gifs" && gifEngine->isActive()) {
    gifEngine->stop();
  }

  if (mod == "weather") {
    weatherEngine->update(config.weather.api_key, config.weather.city);
    if (!weatherEngine->hasValidData() || WiFi.status() != WL_CONNECTED) {
      currentIndex = (currentIndex + 1) % config.rotation.size();
      switchToModule(currentIndex);
      return;
    }
  } else if (mod == "gifs") {
    fighterEngine->stop();
    if (config.idle.gifs_count > 0 && gifEngine->hasDefaultPlaylists()) {
      gifEngine->playDefaultPlaylists(config.idle.gifs_count);
    } else {
      currentIndex = (currentIndex + 1) % config.rotation.size();
      switchToModule(currentIndex);
      return;
    }
  } else if (mod == "crypto") {
    if (!config.crypto.enabled || countSymbols(config.crypto.symbols) == 0 || !cryptoEngine) {
      currentIndex = (currentIndex + 1) % config.rotation.size();
      switchToModule(currentIndex);
      return;
    }
    cryptoEngine->updateConfig(config.crypto);
    cryptoEngine->onDisplayStart();
  } else if (mod == "stock" || mod == "stocks") {
    if (!config.stock.enabled || countSymbols(config.stock.symbols) == 0 || !stockEngine) {
      currentIndex = (currentIndex + 1) % config.rotation.size();
      switchToModule(currentIndex);
      return;
    }
    stockEngine->updateConfig(config.stock);
    stockEngine->onDisplayStart();
  } else if (mod == "temp") {
    // We assume temp sensor available checking is done inside the engine or here
    // If not available, we skip
  } else if (mod == "decibel") {
    if (decibelEngine) {
      decibelEngine->onActivate();
    }
  }

  if (mod == "clock" || mod == "date" || mod == "weather" || mod == "temp" || mod == "decibel") {
    updateBackgroundSprites();
  }
  
  LOGI("RotationManager", "Switched to engine %s", mod.c_str());
  switchDepth = 0;
}

void RotationManager::setSuspended(bool susp) {
    if (susp == suspended) return;
    suspended = susp;
    if (suspended) {
        if (gifEngine && gifEngine->isActive()) gifEngine->stop();
        if (decibelEngine && decibelEngine->isActive()) decibelEngine->onDeactivate();
        LOGI("RotationManager", "Rotation Manager SUSPENDED.");
    } else {
        LOGI("RotationManager", "Rotation Manager RESUMED.");
        resetRotation();
    }
}

bool RotationManager::loop() {
    if (suspended || config.rotation.empty())
        return true;

  uint32_t now = millis();
  String inst_id = config.rotation[currentIndex].instance_id;
  uint32_t dur = config.rotation[currentIndex].duration_sec;
  
  String mod = inst_id;
  for (const auto& inst : config.instances) {
      if (inst.instance_id == inst_id) {
          mod = inst.engine_id;
          break;
      }
  }

  bool advance = false;
  bool isSoloMode = (config.rotation.size() == 1);

  if (mod == "gifs") {
    bool drewFrame = gifEngine->loop();
    if (!gifEngine->isActive()) {
      advance = true;
    }
    if (advance) {
      currentIndex = (currentIndex + 1) % config.rotation.size();
      switchToModule(currentIndex);
    }
    return drewFrame;
  } else {
    IEngine* activeEngine = getActiveEngine(inst_id);
    if (activeEngine) {
        activeEngine->update(m_ctx);
        activeEngine->render(m_ctx);
        if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else if (mod == "clock") {
      clockEngine->loop();
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else if (mod == "date") {
      // Fallback if not instantiated
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else if (mod == "weather") {
      weatherEngine->loop();
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else if (mod == "crypto") {
      if (cryptoEngine) cryptoEngine->loop();
      size_t symbolCount = countSymbols(config.crypto.symbols);
      uint32_t perSymbolSec = config.crypto.duration_sec > 0 ? config.crypto.duration_sec : 5;
      uint32_t totalDurationMs = perSymbolSec * symbolCount * 1000UL;
      if (!isSoloMode && (symbolCount == 0 || now - moduleStartTime >= totalDurationMs)) advance = true;
    } else if (mod == "stock" || mod == "stocks") {
      if (stockEngine) stockEngine->loop();
      size_t symbolCount = countSymbols(config.stock.symbols);
      uint32_t perSymbolSec = config.stock.duration_sec > 0 ? config.stock.duration_sec : 5;
      uint32_t totalDurationMs = perSymbolSec * symbolCount * 1000UL;
      if (!isSoloMode && (symbolCount == 0 || now - moduleStartTime >= totalDurationMs)) advance = true;
    } else if (mod == "temp") {
      if (tempEngine) tempEngine->loop();
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else if (mod == "decibel") {
      if (decibelEngine) decibelEngine->loop();
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else {
      // Fallback for unknown engine ids mapped to a rotation entry
      clockEngine->loop();
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    }

    if (config.idle.fighter_enabled) {
      fighterEngine->loop();
      fighterEngine->draw();
    }
  }

  if (advance && !isSoloMode) {
    currentIndex = (currentIndex + 1) % config.rotation.size();
    switchToModule(currentIndex);
  }
  return true;
}
