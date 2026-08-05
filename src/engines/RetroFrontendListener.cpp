#include "RetroFrontendListener.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"

RetroFrontendListener* RetroFrontendListener::instance = nullptr;

RetroFrontendListener::RetroFrontendListener(MqttConfig& config, GifEngine* gifEngine, ClockEngine* clockEngine, MessageEngine* messageEngine)
    : mqttConfig(config), gif(gifEngine), clock(clockEngine), message(messageEngine), mqttClient(espClient) {
    instance = this;
    lastReconnectAttempt = 0;
}

void RetroFrontendListener::begin() {
    if (!mqttConfig.enabled || mqttConfig.broker.isEmpty()) return;
    
    mqttClient.setServer(mqttConfig.broker.c_str(), mqttConfig.port);
    mqttClient.setCallback(RetroFrontendListener::callback);
}

bool RetroFrontendListener::loop() {
    if (!mqttConfig.enabled) return true;
    
    if (!mqttClient.connected()) {
        long now = millis();
        // Increase reconnect delay to 30 seconds (30000ms) to avoid lagging the main matrix
        // loop every 5 seconds if the MQTT broker is offline or unreachable.
        if (now - lastReconnectAttempt > 30000) {
            lastReconnectAttempt = now;
            reconnect();
        }
    } else {
        mqttClient.loop();
    }
    return true;
}

void RetroFrontendListener::reconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Attempting MQTT connection...");
        if (mqttClient.connect(mqttConfig.deviceName.c_str(), mqttConfig.user.c_str(), mqttConfig.pass.c_str())) {
            Serial.println("connected");
            // Subscribe to the *configured* topics (conf.ini [MQTT] TOPIC_BATOCERA/TOPIC_RECALBOX)
            // rather than hardcoded ones - this is what tools/recalbox_daemon/ actually publishes
            // to (default "recalbox/system/playing"/"batocera/system/playing"), matching
            // ArcadeMatrix_RPi's core/ssh_installer.py daemon exactly so the same daemon install
            // drives both projects.
            if (mqttConfig.topic_batocera.length() > 0) {
                mqttClient.subscribe(mqttConfig.topic_batocera.c_str());
            }
            if (mqttConfig.topic_recalbox.length() > 0) {
                mqttClient.subscribe(mqttConfig.topic_recalbox.c_str());
            }
            // Also keep the native EmulationStation event topic for basic stop/start signals from
            // setups that don't run the custom daemon (see docs/DEVELOPER.md).
            mqttClient.subscribe("/Recalbox/EmulationStation/Event");
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
        }
    }
}

void RetroFrontendListener::callback(char* topic, byte* payload, unsigned int length) {
    if (!instance) return;
    
    String payloadStr;
    for (int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }
    
    instance->handleMessage(String(topic), payloadStr);
}

// Note: the "msg" parameter here intentionally avoids the name "message" - the class also has a
// member `MessageEngine* message` (see header), and shadowing it with a String parameter here would
// be a foot-gun for anyone adding code that needs the MessageEngine pointer inside this function.
void RetroFrontendListener::handleMessage(String topic, String msg) {
    Serial.printf("Frontend Event: [%s] %s\n", topic.c_str(), msg.c_str());
    
    if (topic == mqttConfig.topic_recalbox || topic == mqttConfig.topic_batocera) {
        // tools/recalbox_daemon/arcadematrix_daemon.py JSON format:
        // {"status": "playing"|"browsing"|"stopped", "game": "<rom basename>", "system": "<SystemId>"}
        handleGameEvent(msg);
        return;
    }
    
    if (topic == "/Recalbox/EmulationStation/Event") {
        // Recalbox natively publishes lowercase events to /Recalbox/EmulationStation/Event
        // Examples: "rungame", "stop", "shutdown"
        if (msg == "stop" || msg == "stopgame") {
            gif->stop();
        } else if (msg == "rungame") {
            // No game/system detail is available on this native topic (unlike the custom daemon's
            // JSON payload above) - a generic placeholder is the best we can do here.
            gif->playGif("/gifs/recalbox_generic.raw"); // Placeholder
        }
        return;
    }
    
    // Legacy custom bridge script format (plain text, not JSON) - kept for backward compatibility
    // with older bridge scripts documented prior to the JSON daemon.
    if (msg == "STOP_GAME") {
        gif->stop();
    } else if (msg.startsWith("START_GAME:")) {
        String gifPath = msg.substring(11);
        gif->playGif(gifPath.c_str());
    }
}

void RetroFrontendListener::handleGameEvent(const String& jsonPayload) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, jsonPayload);
    if (err) {
        Serial.printf("RetroFrontendListener: failed to parse JSON payload: %s\n", err.c_str());
        return;
    }

    const char* status = doc["status"] | "";
    const char* gameRaw = doc["game"] | "";
    const char* systemRaw = doc["system"] | "";

    if (strcmp(status, "stopped") == 0 || strlen(gameRaw) == 0) {
        gif->stop();
        return;
    }

    String game = String(gameRaw);
    String folder = mapSystemToPixelcadeFolder(String(systemRaw));
    String artPath = "/pixelcade/" + folder + "/" + game + ".png";

    if (sd.exists(artPath)) {
        Serial.printf("RetroFrontendListener: playing cached Pixelcade art %s\n", artPath.c_str());
        gif->playGif(artPath.c_str());
        return;
    }

    // No SD-cached artwork for this game (not yet synced by tools/pixelcade_sync/, or a game that
    // Pixelcade simply doesn't have art for) - fall back to scrolling the game name as text, same
    // behavior as ArcadeMatrix_RPi's main.py when its DMDCache has no cached/downloadable image.
    Serial.printf("RetroFrontendListener: no cached artwork at %s, falling back to text\n", artPath.c_str());
    if (message) {
        String clean = game;
        clean.replace("-", " ");
        clean.replace("_", " ");
        MessageConfig cfg = { clean, 0x07FF, 1, clean.length() > 8 ? "rtl" : "none", 40, 30 };
        message->displayMessage(cfg);
    }
}

String RetroFrontendListener::mapSystemToPixelcadeFolder(const String& systemId) {
    // Mirrors ArcadeMatrix_RPi's core/dmd_cache.py SYSTEM_MAP exactly - keep both in sync if you
    // add a new system, otherwise the same SD/sync-tool artwork won't resolve identically on both
    // projects.
    struct Mapping { const char* systemId; const char* folder; };
    static const Mapping table[] = {
        {"mame", "mame"}, {"fbneo", "mame"}, {"neogeo", "neogeo"},
        {"nes", "nes"}, {"snes", "snes"}, {"n64", "n64"},
        {"gb", "gb"}, {"gba", "gba"}, {"gbc", "gbc"},
        {"megadrive", "genesis"}, {"genesis", "genesis"},
        {"mastersystem", "mastersystem"}, {"gamegear", "gamegear"},
        {"psx", "psx"}, {"dreamcast", "dreamcast"},
        {"pcengine", "pcengine"}, {"atari2600", "atari2600"},
    };
    for (const auto& m : table) {
        if (systemId == m.systemId) return String(m.folder);
    }
    // Unknown system: fall back to using the raw SystemId as the folder name (matches the RPi's
    // SYSTEM_MAP.get(system, system) default behavior).
    return systemId;
}
