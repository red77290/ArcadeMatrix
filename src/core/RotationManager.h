#pragma once
#include <Arduino.h>
#include <vector>
#include <array>
#include "ConfigLoader.h"
#include "../engines/DateEngine.h"
#include "../engines/GifEngine.h"
#include "../engines/FighterEngine.h"

#include "AppEngineContext.h"
#include <memory>
#include <mutex>

enum class RotationAction {
    NOTIFY_CONFIG_CHANGED,
    RECREATE_INSTANCE,
    RESET_ROTATION
};

struct ActiveEngineSlot {
    char instanceId[32]{0};
    std::unique_ptr<IEngine> engine{};
};

class RotationManager {
public:
    static constexpr size_t MAX_ACTIVE_ENGINES = 32;

    RotationManager();
    
    void begin(const ConfigLoader& cfg);
    void notifyConfigChanged(const String& instanceId);
    void recreateInstance(const String& instanceId);
    bool loop();
    
    // Reset to start of rotation (e.g. after manual interruption)
    void resetRotation();
    
    String getCurrentInstanceId() const;
    String getCurrentEngineId() const;
    void setSuspended(bool suspended);
    bool isSuspended() const { return suspended; }
    
    bool isCurrentRealtime() const;
    OverlayConfig getCurrentOverlays() const;
    IEngine* getCurrentActiveEngine() const;

    // Core Runtime Services for fully migrated engines
    void setEngineContext(AppEngineContext* ctx) { m_ctx = ctx; }
    
    // Pure lookup (zero allocation, bounded O(32) on hot-path)
    IEngine* findActiveEngine(const char* instanceId) const;
    
    // Lazy creation/lookup (cold-path only)
    IEngine* getOrCreateEngine(const char* instanceId);
    
    // Backwards compatible overloads
    inline IEngine* getActiveEngine(const char* instanceId) { return getOrCreateEngine(instanceId); }
    inline IEngine* getActiveEngine(const String& instanceId) { return getOrCreateEngine(instanceId.c_str()); }

    void notifyGeometryChanged(const DisplayGeometry& geometry);

    // Thread-safe API for WebServer
    void queueAction(RotationAction action, const String& instanceId = "");

    // Helper: count valid non-empty comma-separated symbols
    static size_t countSymbols(const String& symbols);

private:
    std::mutex actionMutex;
    std::vector<std::pair<RotationAction, String>> pendingActions;
    void processPendingActions();

    AppEngineContext* m_ctx = nullptr;
    std::array<ActiveEngineSlot, MAX_ACTIVE_ENGINES> activeEngines{};
    
    size_t currentIndex = 0;
    uint32_t moduleStartTime = 0;
    uint8_t switchDepth = 0;
    bool suspended = false;
    String currentActiveInstanceId = "";

    void switchToModule(int index);
};
