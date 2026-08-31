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
  for (size_t i = 0; i < MAX_ACTIVE_ENGINES; ++i) {
      activeEngines[i].engine.reset();
      activeEngines[i].instanceId[0] = '\0';
  }
  currentActiveInstanceId = "";
  queueAction(RotationAction::RESET_ROTATION);
}

void RotationManager::queueAction(RotationAction action, const String& instanceId) {
    std::lock_guard<std::mutex> lock(actionMutex);
    pendingActions.push_back({action, instanceId});
}

void RotationManager::processPendingActions() {
    std::vector<std::pair<RotationAction, String>> actionsToProcess;
    {
        std::lock_guard<std::mutex> lock(actionMutex);
        actionsToProcess = std::move(pendingActions);
        pendingActions.clear();
    }
    
    for (const auto& p : actionsToProcess) {
        if (p.first == RotationAction::NOTIFY_CONFIG_CHANGED) {
            extern ConfigLoader config;
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            IEngine* eng = findActiveEngine(p.second.c_str());
            if (eng) {
                for (const auto& inst : guard->instances) {
                    if (inst.instance_id == p.second) {
                        eng->onConfigChanged(&inst.config);
                        break;
                    }
                }
            }
        } else if (p.first == RotationAction::RECREATE_INSTANCE) {
            for (size_t i = 0; i < MAX_ACTIVE_ENGINES; ++i) {
                if (activeEngines[i].engine && strncmp(activeEngines[i].instanceId, p.second.c_str(), sizeof(activeEngines[i].instanceId)) == 0) {
                    if (currentActiveInstanceId == p.second) {
                        activeEngines[i].engine->deactivate();
                    }
                    activeEngines[i].engine.reset();
                    activeEngines[i].instanceId[0] = '\0';
                    LOGI("RotationManager", "Instance %s destroyed for structural re-instantiation", p.second.c_str());
                    break;
                }
            }
        } else if (p.first == RotationAction::RESET_ROTATION) {
            currentIndex = 0;
            switchToModule(currentIndex);
        }
    }
}

void RotationManager::notifyConfigChanged(const String& instanceId) {
    queueAction(RotationAction::NOTIFY_CONFIG_CHANGED, instanceId);
}

void RotationManager::recreateInstance(const String& instanceId) {
    queueAction(RotationAction::RECREATE_INSTANCE, instanceId);
}

IEngine* RotationManager::findActiveEngine(const char* instanceId) const {
    if (!instanceId || instanceId[0] == '\0') return nullptr;
    for (size_t i = 0; i < MAX_ACTIVE_ENGINES; ++i) {
        if (activeEngines[i].engine && strncmp(activeEngines[i].instanceId, instanceId, sizeof(activeEngines[i].instanceId)) == 0) {
            return activeEngines[i].engine.get();
        }
    }
    return nullptr;
}

IEngine* RotationManager::getOrCreateEngine(const char* instanceId) {
    if (!instanceId || instanceId[0] == '\0') return nullptr;
    if (strlen(instanceId) >= 32) {
        LOGE("RotationManager", "Instance ID '%s' exceeds max length of 31 chars", instanceId);
        return nullptr;
    }
    IEngine* existing = findActiveEngine(instanceId);
    if (existing) return existing;

    LOGI("RotationManager", "getOrCreateEngine: lazy-loading instance '%s'", instanceId);

    // Lazy initialization
    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    for (const auto& inst : guard->instances) {
        if (inst.instance_id == instanceId) {
            auto desc = EngineRegistry::getDescriptor(inst.engine_id.c_str());
            if (desc && desc->factory) {
                auto engine = desc->factory();
                if (engine) {
                    LOGI("RotationManager", "Initializing engine '%s' for instance '%s'...", inst.engine_id.c_str(), instanceId);
                    engine->initialize(m_ctx, &inst.config);
                    IEngine* ptr = engine.get();
                    
                    for (size_t i = 0; i < MAX_ACTIVE_ENGINES; ++i) {
                        if (!activeEngines[i].engine) {
                            strncpy(activeEngines[i].instanceId, instanceId, sizeof(activeEngines[i].instanceId) - 1);
                            activeEngines[i].instanceId[sizeof(activeEngines[i].instanceId) - 1] = '\0';
                            activeEngines[i].engine = std::move(engine);
                            LOGI("RotationManager", "Instantiated engine '%s' for instance '%s' in slot %u", inst.engine_id.c_str(), instanceId, (unsigned)i);
                            return ptr;
                        }
                    }
                    LOGE("RotationManager", "Active engine capacity reached (MAX_ACTIVE_ENGINES=%u)", (unsigned)MAX_ACTIVE_ENGINES);
                    return nullptr;
                }
            } else {
                LOGE("RotationManager", "No descriptor or factory for engine '%s'", inst.engine_id.c_str());
            }
        }
    }
    LOGW("RotationManager", "Instance '%s' not found in config instances (count: %d)", instanceId, (int)guard->instances.size());
    return nullptr;
}

void RotationManager::resetRotation() {
    queueAction(RotationAction::RESET_ROTATION);
}

void RotationManager::switchToModule(int index) {
  extern ConfigLoader config;
  ConfigSnapshotGuard guard = config.acquireSnapshot();
  LOGI("RotationManager", "switchToModule(index=%d), total rotation entries: %d", index, (int)guard->rotation.size());
  if (guard->rotation.empty()) {
    LOGW("RotationManager", "switchToModule: rotation is empty!");
    if (!currentActiveInstanceId.isEmpty()) {
      IEngine* oldEngine = findActiveEngine(currentActiveInstanceId.c_str());
      if (oldEngine) {
        oldEngine->deactivate();
      }
      currentActiveInstanceId = "";
    }
    return;
  }

  static int switchDepth = 0;
  if (switchDepth > (int)guard->rotation.size()) {
    switchDepth = 0;
    return; // Infinite skip loop protection
  }
  switchDepth++;

  moduleStartTime = millis();
  String newInstanceId = guard->rotation[index].instance_id;
  uint32_t dur = guard->rotation[index].duration_sec;
  
  String mod = newInstanceId; // Default to instance_id for legacy compatibility
  for (const auto& inst : guard->instances) {
      if (inst.instance_id == newInstanceId) {
          mod = inst.engine_id;
          break;
      }
  }

  // Deactivate old engine
  if (!currentActiveInstanceId.isEmpty() && currentActiveInstanceId != newInstanceId) {
      IEngine* oldEngine = findActiveEngine(currentActiveInstanceId.c_str());
      if (oldEngine) {
          oldEngine->deactivate();
      }
  }

  // Activate new engine
  IEngine* newEngine = getOrCreateEngine(newInstanceId.c_str());
  if (newEngine) {
      if (newEngine->selfPaced()) {
          newEngine->setRotationBudget(dur);
      }
      if (currentActiveInstanceId != newInstanceId) {
          newEngine->activate();
      }
  }
  
  currentActiveInstanceId = newInstanceId;
  
  LOGI("RotationManager", "Switched to engine %s | Heap: Free=%u, MinFree=%u, MaxAlloc=%u", 
      mod.c_str(), ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  switchDepth = 0;
}

bool RotationManager::isCurrentRealtime() const {
    if (currentActiveInstanceId.isEmpty()) return false;
    IEngine* engine = findActiveEngine(currentActiveInstanceId.c_str());
    return engine ? engine->isRealtime() : false;
}

OverlayConfig RotationManager::getCurrentOverlays() const {
    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    if (guard->rotation.empty() || currentIndex >= guard->rotation.size()) {
        return OverlayConfig{};
    }
    return guard->rotation[currentIndex].overlays;
}

IEngine* RotationManager::getCurrentActiveEngine() const {
    if (currentActiveInstanceId.isEmpty()) return nullptr;
    return findActiveEngine(currentActiveInstanceId.c_str());
}

void RotationManager::notifyGeometryChanged(const DisplayGeometry& geometry) {
    IEngine* engine = getCurrentActiveEngine();
    if (engine) {
        engine->onDisplayGeometryChanged(geometry);
    }
}

void RotationManager::setSuspended(bool susp) {
    if (susp == suspended) return;
    suspended = susp;
    
    if (suspended) {
        if (!currentActiveInstanceId.isEmpty()) {
            IEngine* engine = findActiveEngine(currentActiveInstanceId.c_str());
            if (engine) engine->deactivate();
        }
        LOGI("RotationManager", "Rotation Manager SUSPENDED.");
    } else {
        LOGI("RotationManager", "Rotation Manager RESUMED.");
        if (!currentActiveInstanceId.isEmpty()) {
            IEngine* engine = findActiveEngine(currentActiveInstanceId.c_str());
            if (engine) engine->activate();
        } else {
            resetRotation();
        }
    }
}

bool RotationManager::loop() {
    processPendingActions();

    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();

    if (suspended || guard->rotation.empty()) {
        if (!currentActiveInstanceId.isEmpty()) {
            IEngine* oldEngine = findActiveEngine(currentActiveInstanceId.c_str());
            if (oldEngine) {
                oldEngine->deactivate();
            }
            currentActiveInstanceId = "";
        }
        return true;
    }

    uint32_t now = millis();
    const char* inst_id = guard->rotation[currentIndex].instance_id.c_str();
    uint32_t dur = guard->rotation[currentIndex].duration_sec;
    
    bool advance = false;
    bool isSoloMode = (guard->rotation.size() == 1);

    IEngine* activeEngine = findActiveEngine(inst_id);
    if (!activeEngine) {
        activeEngine = getOrCreateEngine(inst_id);
    }
    
    bool shouldFlip = true;
    if (activeEngine) {
        if (activeEngine->needsClear() && m_ctx && m_ctx->getMatrix()) {
            m_ctx->getMatrix()->fillScreen(0);
        }
        activeEngine->update(m_ctx);
        activeEngine->render(m_ctx);
        shouldFlip = activeEngine->hasNewFrame();
        
        if (!isSoloMode) {
            if (activeEngine->selfPaced()) {
                if (activeEngine->isFinished()) {
                    advance = true;
                }
            } else {
                if (activeEngine->isFinished() || (now - moduleStartTime >= dur * 1000UL)) {
                    advance = true;
                }
            }
        }
    } else {
        // Fallback if engine fails to load or id is invalid
        if (!isSoloMode && (now - moduleStartTime >= dur * 1000UL)) {
            advance = true;
        }
    }

    if (advance && !isSoloMode) {
        currentIndex = (currentIndex + 1) % guard->rotation.size();
        switchToModule(currentIndex);
    }
    return shouldFlip;
}

String RotationManager::getCurrentInstanceId() const {
    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    return guard->rotation.empty() ? "" : guard->rotation[currentIndex].instance_id;
}

String RotationManager::getCurrentEngineId() const {
    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    if (guard->rotation.empty() || currentIndex >= guard->rotation.size()) return "";
    String inst_id = guard->rotation[currentIndex].instance_id;
    const auto* inst = guard->getInstance(inst_id);
    return inst ? inst->engine_id : "";
}
