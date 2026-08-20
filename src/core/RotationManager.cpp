#include "../../include/core/EngineRegistry.h"
#include "LegacyConfigAdapter.h"
#include "RotationManager.h"
#include "ConfigLoader.h"
#include "Logger.h"
#include <WiFi.h>

extern ConfigLoader config;

RotationManager::RotationManager() {
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
      if (inst.engine_id == "date" || inst.engine_id == "clock" || inst.engine_id == "weather") { // We are migrating DateEngine and clock engine
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

void RotationManager::notifyConfigChanged(const String& instanceId) {
    auto it = activeEngines.find(instanceId);
    if (it != activeEngines.end()) {
        for (const auto& inst : config.instances) {
            if (inst.instance_id == instanceId) {
                LegacyConfigAdapter adapter(config, inst.engine_id);
                it->second->onConfigChanged(&adapter);
                break;
            }
        }
    }
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
  // TODO(Architecture): Fighter engine background sprites were here but were tightly coupled.
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
  // Handled via IEngine deactivate()

  // Stop any playing GIFs if leaving GIF mode (Handled by IEngine deactivate)
  if (mod == "stock" || mod == "stocks") {
      // Logic handled via IEngine
  } else if (mod == "temp") {
      // Handled via IEngine
  } else if (mod == "decibel") {
      // Logic handled via IEngine
  }

  if (mod == "clock" || mod == "date" || mod == "weather" || mod == "temp") {
    updateBackgroundSprites();
  }
  
  LOGI("RotationManager", "Switched to engine %s", mod.c_str());
  switchDepth = 0;
}

void RotationManager::setSuspended(bool susp) {
    if (susp == suspended) return;
    suspended = susp;
    if (suspended) {
        // (GifEngine suspend handled by IEngine deactivate or architecture update needed)
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
    // TODO(Architecture): GifEngine loop handled by IEngine update/render.
    bool drewFrame = false;
    IEngine* activeEngine = getActiveEngine(inst_id);
    if (activeEngine) {
      activeEngine->update(m_ctx);
      activeEngine->render(m_ctx);
      drewFrame = true; // Assume drew frame for now
    }
    
    // Auto-advance is handled by GIF engine completion logic (TODO)
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
      // Fallback if not instantiated
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else if (mod == "date") {
      // Fallback if not instantiated
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;    } else if (mod == "weather") {
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else if (mod == "crypto") {
      // TODO(Architecture): Dynamic duration logic for Crypto/Stock was here.
      size_t symbolCount = countSymbols(config.crypto.symbols);
      uint32_t perSymbolSec = config.crypto.duration_sec > 0 ? config.crypto.duration_sec : 5;
      uint32_t totalDurationMs = perSymbolSec * symbolCount * 1000UL;
      if (!isSoloMode && (symbolCount == 0 || now - moduleStartTime >= totalDurationMs)) advance = true;
    } else if (mod == "stock" || mod == "stocks") {
      // TODO(Architecture): Dynamic duration logic for Stock was here.
      size_t symbolCount = countSymbols(config.stock.symbols);
      uint32_t perSymbolSec = config.stock.duration_sec > 0 ? config.stock.duration_sec : 5;
      uint32_t totalDurationMs = perSymbolSec * symbolCount * 1000UL;
      if (!isSoloMode && (symbolCount == 0 || now - moduleStartTime >= totalDurationMs)) advance = true;
    } else if (mod == "temp") {
      // Logic handled via IEngine
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else if (mod == "decibel") {
      // Handled via IEngine
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    } else {
      // Fallback for unknown engine ids mapped to a rotation entry
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) advance = true;
    }

    // TODO(Architecture): Fighter engine background sprites draw call was here.
  }

  if (advance && !isSoloMode) {
    currentIndex = (currentIndex + 1) % config.rotation.size();
    switchToModule(currentIndex);
  }
  return true;
}
