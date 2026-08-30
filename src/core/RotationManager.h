#pragma once
#include <Arduino.h>
#include <vector>
#include "ConfigLoader.h"
#include "../engines/DateEngine.h"
#include "../engines/GifEngine.h"
#include "../engines/FighterEngine.h"


#include "AppEngineContext.h"
#include <map>
#include <memory>
#include <mutex>

enum class RotationAction {
    NOTIFY_CONFIG_CHANGED,
    RECREATE_INSTANCE,
    RESET_ROTATION
};

class RotationManager {
public:
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
    IEngine* getActiveEngine(const String& instanceId);
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
    std::map<String, std::unique_ptr<IEngine>> activeEngines;
    
    size_t currentIndex;
    uint32_t moduleStartTime;
    uint8_t switchDepth;
    bool suspended = false;
    String currentActiveInstanceId = "";

    void switchToModule(int index);
};
