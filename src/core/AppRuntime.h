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

#include "drawing/IDrawingSurface.h"
#include <memory>

struct MatrixRecoveryStatus {
    bool safeModeActive = false;
    bool headlessMode = false;
    int rejectedWidth = 0;
    int rejectedHeight = 0;
    int rejectedChain = 0;
    int rejectedColorDepth = 0;
    int rejectedLatchBlanking = 0;
    char reason[128] = "";
};

class AppRuntime {
public:
    AppRuntime();
    ~AppRuntime();

    void initialize();
    void update();

    inline ConfigLoader& getConfig();
    inline DisplayRuntime& getDisplayRuntime() { return m_displayRuntime; }
    inline IDrawingSurface* getDrawingSurface() { return m_drawingSurface.get(); }
    inline const MatrixRecoveryStatus& getMatrixRecoveryStatus() const { return m_matrixRecovery; }
    inline void clearMatrixRecoveryStatus() { m_matrixRecovery = MatrixRecoveryStatus{}; }

private:
    DisplayArbiter m_displayArbiter;
    DisplayRuntime m_displayRuntime;
    std::unique_ptr<IDrawingSurface> m_drawingSurface;
    uint16_t* m_preallocatedCanvasBuffer = nullptr;
    AppEngineContext* m_appCtx = nullptr;
    BitmapFontLoader m_customFontLoader;

    MessageEngine* m_messageEngine = nullptr;
    MarqueeEngine* m_marqueeEngine = nullptr;
    InGameShowcaseEngine* m_inGameShowcaseEngine = nullptr;
    WebServerAPI* m_webServer = nullptr;
    FrontendSyncEngine* m_frontendListener = nullptr;

    bool m_wasPoweredOn = true;
    int m_lastAppliedBrightness = -1;
    bool m_firstLoop = true;
    int m_lastSec = -1;
    uint32_t m_lastReconciledVersion = 0;

    bool m_lastMqttEnabled = false;
    String m_lastMqttBroker = "";
    int m_lastMqttPort = 0;
    String m_lastMqttUser = "";
    String m_lastMqttPass = "";

    struct ProducerSyncState {
        bool active = false;
        uint32_t requestId = 0;
        EngineHandle handle;
        DisplayPriority priority = DisplayPriority::GIF;
    };

    ProducerSyncState m_syncMqtt;
    ProducerSyncState m_syncMarquee;
    ProducerSyncState m_syncGif;
    ProducerSyncState m_syncVis;
    ProducerSyncState m_syncInGameShowcase;
    MatrixRecoveryStatus m_matrixRecovery;

    bool handleNightMode(const ConfigSnapshot& snapshot);
    void syncMqtt(const ConfigSnapshot& snapshot);
    void evaluateDisplayRequests(const ConfigSnapshot& snapshot);
};

extern AppRuntime app;
