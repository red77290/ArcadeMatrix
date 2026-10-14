#pragma once
#include <Arduino.h>
#include <vector>
#include <array>
#include "ConfigLoader.h"
#include "../engines/DateEngine.h"
#include "../engines/GifEngine.h"
#include "../engines/FighterEngine.h"

#include "AppEngineContext.h"
#include "DisplayArbiter.h"
#include "RotationTransitionFX.h"
#include <memory>
#include <mutex>
#include <atomic>

class DisplayRuntime;

enum class RotationAction {
    NOTIFY_CONFIG_CHANGED,
    RECREATE_INSTANCE,
    RESET_ROTATION
};

struct PendingRotationAction {
    RotationAction action = RotationAction::RESET_ROTATION;
    char moduleName[32]{0};
};

struct ActiveEngineSlot {
    char instanceId[32]{0};
    std::unique_ptr<IEngine> engine{};
    bool pendingRetirement = false;
};

class RotationManager {
public:
    static constexpr size_t MAX_ACTIVE_ENGINES = 32;

    RotationManager();
    
    void begin(const ConfigLoader& cfg);
    void notifyConfigChanged(const char* instanceId);
    void notifyConfigChanged(const String& instanceId) { notifyConfigChanged(instanceId.c_str()); }
    void recreateInstance(const char* instanceId);
    void recreateInstance(const String& instanceId) { recreateInstance(instanceId.c_str()); }
    bool loop();
    
    // Reset to start of rotation (e.g. after manual interruption)
    void resetRotation();
    
    const char* getCurrentInstanceIdCStr() const { return currentActiveInstanceId; }
    String getCurrentInstanceId() const { return String(currentActiveInstanceId); }
    const char* getCurrentEngineIdCStr() const;
    String getCurrentEngineId() const;
    void setSuspended(bool suspended);
    bool isSuspended() const { return suspended; }
    
    bool isCurrentRealtime() const;
    OverlayConfig getCurrentOverlays() const;
    IEngine* getCurrentActiveEngine() const;

    // Core Runtime Services for fully migrated engines
    void setEngineContext(AppEngineContext* ctx) { m_ctx = ctx; }
    RotationTransitionFX m_slotFx;
    bool m_awaitingFirstFrame = false;   ///< the slot just changed and its engine has not drawn yet
    uint32_t m_slotFxStartedMs = 0;
    bool m_slotFxWasRunning = false;     ///< the effect was running on the previous loop
    /// Written from the web server on Core 0, read by the render loop on Core 1.
    std::atomic<RotationEffect> m_slotEffect{RotationEffect::NONE};
    std::atomic<int> m_slotFxMs{500};
    void setDisplayRuntime(DisplayRuntime* dr) { m_displayRuntime = dr; }

    /// Effect played over the gap when the rotation moves to the next slot ("none" disables it).
    void setSlotTransition(const String& effect, int durationMs) {
        m_slotFxMs.store((durationMs < 100) ? 100 : ((durationMs > 3000) ? 3000 : durationMs),
                         std::memory_order_relaxed);
        m_slotEffect.store(RotationTransitionFX::parseEffect(effect), std::memory_order_release);
    }
    
    /**
     * Hot-path lookup.
     *
     * - No allocation
     * - No instance creation
     * - No mutex
     * - Bounded O(MAX_ACTIVE_ENGINES)
     */
    IEngine* findActiveEngine(const char* instanceId) const;
    
    // Lazy creation/lookup (cold-path only)
    IEngine* getOrCreateEngine(const char* instanceId);

    // Count currently instantiated active engines (for monitoring & tests)
    size_t getActiveEngineCount() const;
    
    // Backwards compatible overloads
    inline IEngine* getActiveEngine(const char* instanceId) { return getOrCreateEngine(instanceId); }
    inline IEngine* getActiveEngine(const String& instanceId) { return getOrCreateEngine(instanceId.c_str()); }

    void notifyGeometryChanged(const DisplayGeometry& geometry);

    // Thread-safe API for WebServer (Core 0 multi-producer serialized via actionMutex, Core 1 lock-free consumer)
    void queueAction(RotationAction action, const char* instanceId = "");
    void queueAction(RotationAction action, const String& instanceId) { queueAction(action, instanceId.c_str()); }

    // Helper: count valid non-empty comma-separated symbols
    static size_t countSymbols(const String& symbols);

private:
    std::mutex actionMutex;
    LockFreeSPSCQueue<PendingRotationAction, 8> pendingActions;
    void processPendingActions();

    AppEngineContext* m_ctx = nullptr;
    DisplayRuntime* m_displayRuntime = nullptr;
    std::array<ActiveEngineSlot, MAX_ACTIVE_ENGINES> activeEngines{};
    
    size_t currentIndex = 0;
    uint32_t moduleStartTime = 0;
    bool m_slotMissing = false;      ///< current slot has no engine (instance missing or failed to load); logged once
    uint8_t m_missingClears = 0;     ///< framebuffers blanked so far for a missing slot (one per DMA buffer)
    uint8_t switchDepth = 0;
    bool suspended = false;
    char currentActiveInstanceId[32]{0};
    mutable char currentActiveEngineId[32]{0};

    void switchToModule(int index);
    void retireEngineSlot(size_t slotIndex);
};
