#pragma once
#include <Arduino.h>
#include "ConfigLoader.h"
#include "MatrixEngine.h"
#include "DisplayArbiter.h"
#include "OverlayManager.h"
#include "RotationManager.h"
#include "DisplayOrientationManager.h"
#include "DisplayRuntime.h"
#include "AppEngineContext.h"
#include "BitmapFontLoader.h"
#include "../api/WebServerAPI.h"
#include "../engines/FrontendSyncEngine.h"
#include "../engines/VisualizerEngine.h"
#include "../engines/MessageEngine.h"
#include "../engines/MarqueeEngine.h"
#include "../engines/GifEngine.h"
#include "../services/AudioSessionManager.h"

class AppRuntime {
public:
    AppRuntime();
    ~AppRuntime();

    void initialize();
    void update();

    inline ConfigLoader& getConfig();
    inline DisplayRuntime& getDisplayRuntime() { return m_displayRuntime; }

private:
    DisplayArbiter m_displayArbiter;
    DisplayRuntime m_displayRuntime;
    AppEngineContext* m_appCtx = nullptr;
    BitmapFontLoader m_customFontLoader;

    MessageEngine* m_messageEngine = nullptr;
    MarqueeEngine* m_marqueeEngine = nullptr;
    WebServerAPI* m_webServer = nullptr;
    FrontendSyncEngine* m_frontendListener = nullptr;

    bool m_wasPoweredOn = true;
    bool m_firstLoop = true;
    int m_lastSec = -1;
    uint32_t m_lastReconciledVersion = 0;

    void handleNightMode(const ConfigSnapshot& snapshot);
    void syncMqtt(const ConfigSnapshot& snapshot);
    void evaluateDisplayRequests(const ConfigSnapshot& snapshot);
};

extern AppRuntime app;
