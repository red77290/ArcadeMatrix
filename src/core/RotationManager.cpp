#include "../../include/core/EngineRegistry.h"
#include "RotationManager.h"
#include "DisplayRuntime.h"
#include "ConfigLoader.h"
#include "Core0Lifecycle.h"
#include "Logger.h"
#include "MatrixEngine.h"
#include <WiFi.h>

extern MatrixEngine matrixEngine;

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
  currentActiveInstanceId[0] = '\0';
  queueAction(RotationAction::RESET_ROTATION);
}

void RotationManager::queueAction(RotationAction action, const String& instanceId) {
    std::lock_guard<std::mutex> lock(actionMutex);
    pendingActions.push_back({action, instanceId});
}

void RotationManager::retireEngineSlot(size_t slotIndex) {
    if (slotIndex >= MAX_ACTIVE_ENGINES || !activeEngines[slotIndex].engine) return;

    IEngine* eng = activeEngines[slotIndex].engine.get();
    char instId[32];
    strncpy(instId, activeEngines[slotIndex].instanceId, sizeof(instId));
    instId[sizeof(instId) - 1] = '\0';

    // Release Barrier Step 1: stop / deactivate (state-only, non-blocking on Core 1)
    eng->deactivate();

    // Release Barrier Step 2: clear currentActiveInstanceId if matching
    if (instId[0] != '\0' && strcmp(currentActiveInstanceId, instId) == 0) {
        currentActiveInstanceId[0] = '\0';
    }

    // Release Barrier Step 3: purge all references from DisplayRuntime (session activeEngine & preemption stack)
    if (m_displayRuntime) {
        m_displayRuntime->purgeEngineReferences(eng, instId);
    }

    // Release Barrier Step 4: mark engine state as CORE1_RELEASED
    eng->setResourceState(EngineResourceState::CORE1_RELEASED);

    // Release Barrier Step 5: sever local instanceId immediately (never findable again by findActiveEngine)
    activeEngines[slotIndex].instanceId[0] = '\0';

    // Release Barrier Step 6: MOVE unique_ptr to retirement queue
    LOGI("RotationManager", "Retiring engine at %p (inst='%s') for Core 0 lifecycle destruction", eng, instId);
    if (Core0LifecycleDispatcher::instance().retire(std::move(activeEngines[slotIndex].engine))) {
        activeEngines[slotIndex].pendingRetirement = false;
    } else {
        LOGW("RotationManager", "Retirement queue full; retaining engine at %p in pending-retirement slot to retry", eng);
        activeEngines[slotIndex].pendingRetirement = true;
    }
}

void RotationManager::processPendingActions() {
    // 1. Retry retirement for any slots previously kept in pendingRetirement due to a full queue
    for (size_t i = 0; i < MAX_ACTIVE_ENGINES; ++i) {
        if (activeEngines[i].pendingRetirement && activeEngines[i].engine) {
            if (Core0LifecycleDispatcher::instance().retire(std::move(activeEngines[i].engine))) {
                activeEngines[i].pendingRetirement = false;
                LOGI("RotationManager", "Successfully retired pending engine on retry");
            }
        }
    }

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
                    retireEngineSlot(i);
                    break;
                }
            }
        } else if (p.first == RotationAction::RESET_ROTATION) {
            extern ConfigLoader config;
            ConfigSnapshotGuard guard = config.acquireSnapshot();

            // Prune and retire any active engines that are no longer in the rotation sequence
            for (size_t i = 0; i < MAX_ACTIVE_ENGINES; ++i) {
                if (activeEngines[i].engine && activeEngines[i].instanceId[0] != '\0') {
                    bool stillInRotation = false;
                    for (const auto& entry : guard->rotation) {
                        if (entry.instance_id == activeEngines[i].instanceId) {
                            stillInRotation = true;
                            break;
                        }
                    }
                    if (!stillInRotation) {
                        LOGI("RotationManager", "Pruning deactivated engine '%s' (removed from rotation)", activeEngines[i].instanceId);
                        retireEngineSlot(i);
                    }
                }
            }

            // Re-anchor to wherever the currently active instance now sits in the updated
            // rotation list instead of unconditionally jumping back to slot 0. Every single
            // rotation-list edit (reorder, duration tweak, add/remove an unrelated screen)
            // queues RESET_ROTATION, and always restarting from index 0 tore down and rebuilt
            // whatever engine happened to occupy that slot - including a full I2S audio driver
            // stop/start if the audio visualizer was running - even though nothing about the
            // screen actually playing had changed. If the active instance is no longer present
            // (removed from rotation) this falls back to slot 0, which is the only case where
            // restarting from the top is actually correct.
            int newIndex = 0;
            if (currentActiveInstanceId[0] != '\0') {
                for (size_t i = 0; i < guard->rotation.size(); ++i) {
                    if (guard->rotation[i].instance_id == currentActiveInstanceId) {
                        newIndex = (int)i;
                        break;
                    }
                }
            }
            currentIndex = newIndex;
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
        if (!activeEngines[i].pendingRetirement && activeEngines[i].engine && strncmp(activeEngines[i].instanceId, instanceId, sizeof(activeEngines[i].instanceId)) == 0) {
            return activeEngines[i].engine.get();
        }
    }
    return nullptr;
}

size_t RotationManager::getActiveEngineCount() const {
    size_t cnt = 0;
    for (size_t i = 0; i < MAX_ACTIVE_ENGINES; ++i) {
        if (!activeEngines[i].pendingRetirement && activeEngines[i].engine) {
            cnt++;
        }
    }
    return cnt;
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
            if (desc && !desc->available) {
                LOGW("RotationManager", "Engine '%s' is unavailable on this hardware profile, skipping instance '%s'", inst.engine_id.c_str(), instanceId);
                return nullptr;
            }
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
    if (currentActiveInstanceId[0] != '\0') {
      IEngine* oldEngine = findActiveEngine(currentActiveInstanceId);
      if (oldEngine) {
        oldEngine->deactivate();
      }
      currentActiveInstanceId[0] = '\0';
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
  m_slotMissing = false;
  m_missingClears = 0;
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
  if (currentActiveInstanceId[0] != '\0' && strcmp(currentActiveInstanceId, newInstanceId.c_str()) != 0) {
      IEngine* oldEngine = findActiveEngine(currentActiveInstanceId);
      if (oldEngine) {
          oldEngine->deactivate();
      }
      if (m_ctx && m_ctx->getMatrix()) {
          // Both DMA buffers have to go black. Clearing once only blanks the back buffer, so the
          // front one still holds the engine that just ended; while the next engine loads its first
          // frame (a GIF read from the card takes a moment) any flip puts that old frame back on the
          // panel for an instant.
          m_ctx->getMatrix()->fillScreen(0);
          if (matrixEngine.isDoubleBuffered()) {
              matrixEngine.present();
              m_ctx->getMatrix()->fillScreen(0);
          }
          matrixEngine.markExternalDraw();
      }
  }

  // Activate new engine
  IEngine* newEngine = getOrCreateEngine(newInstanceId.c_str());
  if (newEngine) {
      if (newEngine->selfPaced()) {
          newEngine->setRotationBudget(dur);
      }
      if (strcmp(currentActiveInstanceId, newInstanceId.c_str()) != 0 || (newEngine->selfPaced() && newEngine->isFinished())) {
          newEngine->activate();
      }
  }
  
  strncpy(currentActiveInstanceId, newInstanceId.c_str(), sizeof(currentActiveInstanceId) - 1);
  currentActiveInstanceId[sizeof(currentActiveInstanceId) - 1] = '\0';
  
  LOGI("RotationManager", "Switched to engine %s | Heap: Free=%u, MinFree=%u, MaxAlloc=%u", 
      mod.c_str(), ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  switchDepth = 0;
}

bool RotationManager::isCurrentRealtime() const {
    if (currentActiveInstanceId[0] == '\0') return false;
    IEngine* engine = findActiveEngine(currentActiveInstanceId);
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
    if (currentActiveInstanceId[0] == '\0') return nullptr;
    return findActiveEngine(currentActiveInstanceId);
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
        if (currentActiveInstanceId[0] != '\0') {
            IEngine* engine = findActiveEngine(currentActiveInstanceId);
            if (engine) engine->deactivate();
        }
        LOGI("RotationManager", "Rotation Manager SUSPENDED.");
    } else {
        LOGI("RotationManager", "Rotation Manager RESUMED.");
        if (currentActiveInstanceId[0] != '\0') {
            IEngine* engine = findActiveEngine(currentActiveInstanceId);
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
        if (currentActiveInstanceId[0] != '\0') {
            IEngine* oldEngine = findActiveEngine(currentActiveInstanceId);
            if (oldEngine) {
                oldEngine->deactivate();
            }
            currentActiveInstanceId[0] = '\0';
        }
        return true;
    }

    if (currentIndex >= guard->rotation.size()) {
        currentIndex = 0;
        switchToModule(0);
    }

    uint32_t now = millis();
    const char* inst_id = guard->rotation[currentIndex].instance_id.c_str();
    uint32_t dur = guard->rotation[currentIndex].duration_sec;
    
    bool advance = false;
    bool isSoloMode = (guard->rotation.size() == 1);

    IEngine* activeEngine = findActiveEngine(inst_id);
    if (!activeEngine && !m_slotMissing) {
        activeEngine = getOrCreateEngine(inst_id);   // logs the reason itself; asked once per slot
    }
    
    bool shouldFlip = true;
    if (activeEngine) {
        if (activeEngine->needsClear() && m_ctx && m_ctx->getMatrix()) {
            m_ctx->getMatrix()->fillScreen(0);
            matrixEngine.markExternalDraw();
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
        } else {
            if (activeEngine->selfPaced() && activeEngine->isFinished()) {
                activeEngine->activate();
            }
        }
    } else {
        // No engine for this slot: the instance is missing from the config (a save that never reached
        // the card, a deleted instance) or failed to load. Blank both framebuffers so the panel does not
        // hold whatever was drawn last (at boot, the IP notice), say so once, and move straight on
        // instead of sitting out the slot's duration. A solo rotation stays blank rather than frozen.
        if (!m_slotMissing) {
            m_slotMissing = true;
            LOGW("RotationManager", "Rotation slot %d refers to instance '%s' which does not exist or could not be loaded; %s",
                 (int)currentIndex, (inst_id ? inst_id : "(null)"), isSoloMode ? "showing a blank panel" : "skipping it");
        }
        if (m_missingClears < 2) {
            if (m_ctx && m_ctx->getMatrix()) m_ctx->getMatrix()->fillScreen(0);
            m_missingClears++;
            shouldFlip = true;
        } else {
            shouldFlip = false;
        }
        if (!isSoloMode) {
            advance = true;
        }
    }

    if (advance && !isSoloMode) {
        currentIndex = (currentIndex + 1) % guard->rotation.size();
        switchToModule(currentIndex);
    }
    return shouldFlip;
}

String RotationManager::getCurrentEngineId() const {
    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    if (guard->rotation.empty() || currentIndex >= guard->rotation.size()) return "";
    String inst_id = guard->rotation[currentIndex].instance_id;
    const auto* inst = guard->getInstance(inst_id);
    return inst ? inst->engine_id : "";
}
