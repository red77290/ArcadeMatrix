#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <PicoMQTT.h>
#include <HTTPClient.h>
#include "../core/ConfigLoader.h"
#include "GifEngine.h"
#include "ClockEngine.h"
#include "MessageEngine.h"
#include <unordered_map>

// Listens for game-launch/stop events from Recalbox/Batocera over MQTT and displays the
// corresponding Pixelcade-style marquee artwork directly from the SD card (see
// tools/pixelcade_sync/ for the offline PC-side tool that pre-populates /pixelcade/<system>/*.png
// on the SD card, and tools/recalbox_daemon/ for the companion daemon that publishes these events -
// same wire format/topic as ArcadeMatrix_RPi's core/ssh_installer.py daemon, so a single daemon
// install serves both projects). Falls back to displaying the game name as scrolling text (via
// MessageEngine) if no matching artwork is found on the SD card.
class RetroFrontendListener {
public:
    RetroFrontendListener(MqttConfig& config, GifEngine* gifEngine, ClockEngine* clockEngine, MessageEngine* messageEngine = nullptr);
    void begin();
    bool loop();
    void stop();

    struct SystemVariant {
        String folder;
        String name;
    };
    static std::vector<SystemVariant> getSystemNameVariants(const String& systemId);

private:
    MqttConfig& mqttConfig;
    GifEngine* gif;
    ClockEngine* clock;
    MessageEngine* message;
    WiFiClient espClient;
    PubSubClient mqttClient;
    PicoMQTT::Server* internalBroker = nullptr;

    unsigned long lastReconnectAttempt;
    uint32_t currentRequestId = 0;
    bool isGamePlaying = false;
    bool waitingDisplayed = false;

    void reconnect();
    static void callback(char* topic, byte* payload, unsigned int length);
    static RetroFrontendListener* instance;

    void handleMessage(String topic, String payload);

    bool hasPendingEvent = false;
    String pendingPayload = "";

    // Parses a {"status": "...", "game": "...", "system": "..."} JSON payload (the format
    // published by tools/recalbox_daemon/arcadematrix_daemon.py) and either displays the matching
    // SD-cached Pixelcade artwork, falls back to scrolling text, or stops playback on "stopped".
    void handleGameEvent(const String& jsonPayload, uint32_t reqId);
    void handleSystemEvent(const String& systemId, uint32_t reqId);



    // Maps a Recalbox/Batocera SystemId (e.g. "snes", "fbneo") to the folder name used by the
    // Pixelcade repository (e.g. "snes", "mame") - mirrors ArcadeMatrix_RPi's core/dmd_cache.py
    // SYSTEM_MAP exactly, so artwork synced by tools/pixelcade_sync/ resolves identically on
    // both projects.
    static String mapSystemToPixelcadeFolder(const String& systemId);

    // Downloads the missing artwork from Pixelcade GitHub repository to the SD card.
    // Returns true if successfully downloaded.
    bool downloadPixelcadeArt(const String& folder, const String& name, String& outPath, uint32_t reqId);
};
