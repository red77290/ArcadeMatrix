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


class RotationManager {
public:
    RotationManager();
    
    void begin(const ConfigLoader& cfg);
    void notifyConfigChanged(const String& instanceId);
    bool loop();
    
    // Reset to start of rotation (e.g. after manual interruption)
    void resetRotation();
    
    
    String getCurrentInstanceId() const;
    void setSuspended(bool suspended);
    bool isSuspended() const { return suspended; }
    
    bool isCurrentRealtime() const;
    bool allowsCurrentOverlay() const;

    // Core Runtime Services for fully migrated engines
    void setEngineContext(AppEngineContext* ctx) { m_ctx = ctx; }
    IEngine* getActiveEngine(const String& instanceId);

    // Helper: count valid non-empty comma-separated symbols
    static size_t countSymbols(const String& symbols);

private:
    AppEngineContext* m_ctx = nullptr;
    std::map<String, std::unique_ptr<IEngine>> activeEngines;
    
    size_t currentIndex;
    uint32_t moduleStartTime;
    uint8_t switchDepth;
    bool suspended = false;
    String currentActiveInstanceId = "";

    void switchToModule(int index);
};
