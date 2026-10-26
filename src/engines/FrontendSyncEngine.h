#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <PicoMQTT.h>
#include <HTTPClient.h>
#include <atomic>
#include <map>
#include <vector>
#include "../../include/core/EngineContract.h"
#include "../core/ConfigLoader.h"
#include "GifEngine.h"
#include "MessageEngine.h"

class IDrawingSurface;

/**
 * @class InGameShowcaseEngine
 * @brief Lightweight lock-free RGB565 frame presenter for DisplaySourceId::INGAME_SHOWCASE.
 *
 * Strictly enforces all 20 real-time architectural invariants on Core 1:
 * - Zero dynamic allocation, zero String, and zero std::mutex on Core 1.
 * - Renders exclusively via IDrawingSurface::blit565 (strict HUB75 DMA isolation).
 * - Allocation-free and wait-free deactivate() / pause() / resume().
 */
class InGameShowcaseEngine : public IEngine {
public:
    InGameShowcaseEngine() = default;
    ~InGameShowcaseEngine() override = default;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void pause() override;
    void resume() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

    bool isRealtime() const override { return true; }
    bool needsClear() const override { return false; }
    bool hasNewFrame() const override { return m_dirty; }
    bool allowRotation() const override { return false; }

    // Lock-free Core 0 -> Core 1 frame handoff
    void publishFrame(const uint16_t* framePtr, uint16_t w, uint16_t h) {
        m_frameW.store(w, std::memory_order_relaxed);
        m_frameH.store(h, std::memory_order_relaxed);
        m_activeFramePtr.store(framePtr, std::memory_order_release);
        m_frameSeq.fetch_add(1, std::memory_order_release);
    }

    void clearFrame() {
        m_activeFramePtr.store(nullptr, std::memory_order_release);
        m_frameSeq.fetch_add(1, std::memory_order_release);
    }

    bool isDisplayEngaged() const {
        return m_displayEngaged.load(std::memory_order_acquire);
    }

    uint16_t surfaceWidth() const { return m_width; }
    uint16_t surfaceHeight() const { return m_height; }

private:
    IDrawingSurface* m_surface = nullptr;
    uint16_t m_width = 128;
    uint16_t m_height = 32;

    std::atomic<const uint16_t*> m_activeFramePtr{nullptr};
    std::atomic<uint16_t> m_frameW{0};
    std::atomic<uint16_t> m_frameH{0};
    std::atomic<uint32_t> m_frameSeq{0};
    std::atomic<bool> m_displayEngaged{false};

    uint32_t m_lastRenderedSeq = 0;
    bool m_dirty = false;
};

// Listens for game-launch/stop events from Recalbox/Batocera/RetroPie over MQTT and displays the
// corresponding Pixelcade-style marquee artwork directly from the SD card (0 ms local navigation).
// When DMD Media Showcase is enabled and reachable, also supports:
// - Dynamic MQTT broker sharing (stopping embedded PicoMQTT::Server to free ~20-30 KB SRAM).
// - Late-preempting In-Game Showcase sequences (Card + Panoramas + Gameplay Video) via Core 0 worker.
class FrontendSyncEngine {
public:
    static constexpr const char* MQTT_TOPIC_WILDCARD = "system/playing/#";
    static constexpr const char* MQTT_TOPIC_RECALBOX = "system/playing/recalbox";
    static constexpr const char* MQTT_TOPIC_BATOCERA = "system/playing/batocera";
    static constexpr const char* MQTT_TOPIC_RETROPIE = "system/playing/retropie";
    static constexpr const char* MQTT_TOPIC_SHOWCASE_CMD = "dmd_showcase/clients/+/command";

    FrontendSyncEngine(
        const MqttConfig& config,
        GifEngine* gifEngine,
        MessageEngine* messageEngine = nullptr,
        InGameShowcaseEngine* inGameEngine = nullptr
    );
    ~FrontendSyncEngine();

    void begin();
    bool loop();
    void stop();
    bool isPlaying() const { return isGamePlaying; }
    bool wantsInGameShowcaseDisplay() const { return m_wantsInGameDisplay.load(std::memory_order_acquire); }

    void updateRuntimeConfig(
        const MqttConfig& mqttCfg,
        const ShowcaseConfig& showcaseCfg,
        const String& systemLang,
        uint16_t panelW,
        uint16_t panelH
    );

    // Non-blocking O(1) read of the resolved MQTT broker target for GET /api/mqtt/broker-target.
    static void getResolvedBrokerTarget(
        String& outHost,
        uint16_t& outPort,
        String& outSource,
        bool& outShowcaseActive
    );

    // Pure helpers exposed for deterministic unit testing.
    static bool parseShowcaseHealthBroker(
        const char* healthJson,
        const char* fallbackHost,
        String& outHost,
        uint16_t& outPort
    );

    static String buildShowcaseQueueRequestJson(
        const MqttConfig& mqttCfg,
        const String& systemId,
        const String& gameQuery,
        uint16_t width,
        uint16_t height,
        const String& lang
    );

    struct SystemVariant {
        String folder;
        String name;
    };
    static String cleanSystemName(const String& rawSystem);
    static std::vector<SystemVariant> getSystemNameVariants(const String& systemId);
    static std::vector<SystemVariant> getSystemNameVariantsMapped(const std::map<String, std::vector<String>>& mappings, const String& systemId);
    static std::map<String, std::vector<String>> loadMappingsFromSD();
    static String mapSystemToPixelcadeFolder(const String& systemId);

private:
    MqttConfig mqttConfig;
    ShowcaseConfig showcaseConfig;
    String m_systemLang = "en";
    uint16_t m_panelW = 128;
    uint16_t m_panelH = 32;

    GifEngine* gif;
    MessageEngine* message;
    InGameShowcaseEngine* m_inGameEngine = nullptr;
    WiFiClient espClient;
    PubSubClient mqttClient;
    PicoMQTT::Server* internalBroker = nullptr;
    std::map<String, std::vector<String>> systemMappings;

    unsigned long lastReconnectAttempt = 0;
    unsigned long m_beginMs = 0;
    uint32_t currentRequestId = 0;
    bool isGamePlaying = false;
    bool waitingDisplayed = false;
    bool hasReceivedAnyEvent = false;
    bool m_transportConfigured = false;
    bool m_activeFromShowcase = false;
    char m_activeBrokerHost[96] = "";
    uint16_t m_activeBrokerPort = 1883;
    char m_activeMarqueePath[128] = "";
    bool m_wasShowingInGame = false;

    std::atomic<bool> isReconnecting{false};
    std::atomic<bool> stopReconnectRequested{false};
    std::atomic<bool> reconnectTaskExited{true};
    TaskHandle_t reconnectTaskHandle = nullptr;
    static void reconnectTaskFunc(void* param);

    // Lock-free Core 1 <-> Core 0 Showcase worker synchronization (zero std::mutex on Core 1)
    struct WorkerConfigSlot {
        char showcaseUrl[96] = "http://dmdmediashowcase.local:8890";
        char localBroker[96] = "";
        char lang[8] = "en";
        char cycleMode[16] = "once";
        char cardStyleSource[20] = "showcase_profile";
        char cardLayout[20] = "classic_split";
        char cardTheme[16] = "tinted";
        char textScrollSpeed[12] = "4";
        char videoMode[16] = "auto_action";
        char colorBoost[16] = "auto";
        uint16_t localPort = 1883;
        uint16_t panelW = 128;
        uint16_t panelH = 32;
        uint16_t marqueeIntervalSec = 45;
        uint8_t snapshotPanoramaCount = 0;
        uint8_t videoMaxDurationS = 12;
        uint8_t videoFps = 15;
        bool mqttEnabled = false;
        bool showcaseEnabled = false;
        bool useShowcaseBroker = true;
        bool ingameShowcaseEnabled = true;
        bool includeCardIntro = true;
        bool showPoster = true;
        bool showSynopsis = true;
        bool jacketPanorama = true;
        bool marqueePanorama = false;
        bool showVideo = true;
    };

    struct InGameJobSlot {
        char system[48] = "";
        char game[128] = "";
        uint32_t token = 0;
        bool active = false;
    };

    std::atomic<uint32_t> m_cfgSeq{0};
    WorkerConfigSlot m_workerCfg;
    std::atomic<uint32_t> m_cfgGen{1};

    std::atomic<uint32_t> m_jobSeq{0};
    InGameJobSlot m_jobSlot;
    std::atomic<uint32_t> m_playSessionToken{0};
    std::atomic<bool> m_wantsInGameDisplay{false};
    std::atomic<bool> m_readyWakeupHint{false};

    std::atomic<bool> m_workerStopRequested{false};
    std::atomic<bool> m_workerExited{true};
    TaskHandle_t m_showcaseWorkerHandle = nullptr;
    uint16_t* m_streamFrames[2] = {nullptr, nullptr};
    size_t m_streamFramePixels = 0;

    static void showcaseWorkerTaskFunc(void* param);
    void showcaseWorkerLoop();
    void stopReconnectTask();
    void applyBrokerTransport(bool fromShowcase, const char* host, uint16_t port);
    void syncWorkerConfigSlot();
    void startInGameShowcaseSession(const String& cleanSystem, const String& rawGame);
    void cancelInGameShowcaseSession();
    bool allocateStreamBuffers(uint16_t w, uint16_t h);
    void freeStreamBuffersSafely();
    bool streamReadyItemOnce(
        const char* resolvedServerUrl,
        const char* itemId,
        uint32_t jobToken,
        uint16_t expectedW,
        uint16_t expectedH
    );
    static void deleteShowcaseQueueItemHttp(
        const char* resolvedServerUrl,
        const char* syncClientId,
        const char* queueId
    );

    void reconnect();
    static void callback(char* topic, byte* payload, unsigned int length);
    static FrontendSyncEngine* instance;

    void handleMessage(String topic, String payload);

    bool hasPendingEvent = false;
    unsigned long pendingEventTime = 0;
    String pendingPayload = "";

    void handleGameEvent(const String& jsonPayload, uint32_t reqId);
    void handleSystemEvent(const String& systemId, uint32_t reqId);
    bool downloadPixelcadeArt(const String& folder, const String& name, String& outPath, uint32_t reqId);
};
