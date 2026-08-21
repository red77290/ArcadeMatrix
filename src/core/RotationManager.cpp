#include "../../include/core/EngineRegistry.h"
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
  activeEngines.clear();
  currentActiveInstanceId = "";
  resetRotation();
}

void RotationManager::notifyConfigChanged(const String& instanceId) {
    extern ConfigLoader config;
    auto it = activeEngines.find(instanceId);
    if (it != activeEngines.end()) {
        for (auto& inst : config.instances) {
            if (inst.instance_id == instanceId) {
                it->second->onConfigChanged(&inst.config);
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
    
    // Lazy initialization
    extern ConfigLoader config;
    for (const auto& inst : config.instances) {
        if (inst.instance_id == instanceId) {
            auto desc = EngineRegistry::getDescriptor(inst.engine_id.c_str());
            if (desc && desc->factory) {
                auto engine = desc->factory();
                if (engine) {
                    engine->initialize(m_ctx, &inst.config);
                    IEngine* ptr = engine.get();
                    activeEngines[instanceId] = std::move(engine);
                    return ptr;
                }
            }
        }
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
  String newInstanceId = config.rotation[index].instance_id;
  
  String mod = newInstanceId; // Default to instance_id for legacy compatibility
  for (const auto& inst : config.instances) {
      if (inst.instance_id == newInstanceId) {
          mod = inst.engine_id;
          break;
      }
  }

  // Deactivate old engine
  if (currentActiveInstanceId != "" && currentActiveInstanceId != newInstanceId) {
      IEngine* oldEngine = getActiveEngine(currentActiveInstanceId);
      if (oldEngine) {
          oldEngine->deactivate();
      }
  }

  // Activate new engine
  IEngine* newEngine = getActiveEngine(newInstanceId);
  if (newEngine && currentActiveInstanceId != newInstanceId) {
      newEngine->activate();
  }
  
  currentActiveInstanceId = newInstanceId;

  if (mod == "clock" || mod == "date" || mod == "weather" || mod == "temp") {
    updateBackgroundSprites();
  }
  
  LOGI("RotationManager", "Switched to engine %s | Heap: Free=%u, MinFree=%u, MaxAlloc=%u", 
      mod.c_str(), ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  switchDepth = 0;
}

void RotationManager::setSuspended(bool susp) {
    if (susp == suspended) return;
    suspended = susp;
    
    if (suspended) {
        if (currentActiveInstanceId != "") {
            IEngine* engine = getActiveEngine(currentActiveInstanceId);
            if (engine) engine->deactivate();
        }
        LOGI("RotationManager", "Rotation Manager SUSPENDED.");
    } else {
        LOGI("RotationManager", "Rotation Manager RESUMED.");
        if (currentActiveInstanceId != "") {
            IEngine* engine = getActiveEngine(currentActiveInstanceId);
            if (engine) engine->activate();
        } else {
            resetRotation();
        }
    }
}

bool RotationManager::loop() {
    if (suspended || config.rotation.empty())
        return true;

  uint32_t now = millis();
  String inst_id = config.rotation[currentIndex].instance_id;
  uint32_t dur = config.rotation[currentIndex].duration_sec;
  
  bool advance = false;
  bool isSoloMode = (config.rotation.size() == 1);

  IEngine* activeEngine = getActiveEngine(inst_id);
  if (activeEngine) {
      activeEngine->update(m_ctx);
      activeEngine->render(m_ctx);
      
      if (!isSoloMode) {
          if (activeEngine->isFinished() || (now - moduleStartTime >= dur * 1000UL)) {
              advance = true;
          }
      }
  } else {
      // Fallback if engine fails to load or id is invalid
      if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) {
          advance = true;
      }
  }

  if (advance && !isSoloMode) {
    currentIndex = (currentIndex + 1) % config.rotation.size();
    switchToModule(currentIndex);
  }
  return true;
}

String RotationManager::getCurrentInstanceId() const {
    extern ConfigLoader config;
    return config.rotation.empty() ? "" : config.rotation[currentIndex].instance_id;
}
