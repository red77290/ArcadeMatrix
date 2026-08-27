#include "FrontendSyncEngine.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"
#include "../core/Logger.h"
#include "../core/Globals.h"
#include <esp_task_wdt.h>

FrontendSyncEngine* FrontendSyncEngine::instance = nullptr;

FrontendSyncEngine::FrontendSyncEngine(MqttConfig& config, GifEngine* gifEngine, MessageEngine* messageEngine)
    : mqttConfig(config), gif(gifEngine), message(messageEngine), mqttClient(espClient) {
    instance = this;
    lastReconnectAttempt = 0;
}

void FrontendSyncEngine::begin() {
    stop();
    if (!mqttConfig.enabled) return;
    
    hasReceivedAnyEvent = false;
    hasPendingEvent = false;
    isGamePlaying = false;
    waitingDisplayed = false;
    lastReconnectAttempt = 0;
    
    if (mqttConfig.broker.isEmpty() || mqttConfig.broker == "127.0.0.1" || mqttConfig.broker == "localhost") {
        LOGI("RetroFrontend", "Starting embedded PicoMQTT Broker on port %d...", mqttConfig.port);
        internalBroker = new PicoMQTT::Server(mqttConfig.port);
        
        // Subscribe to the configured topics locally
        if (mqttConfig.topic_batocera.length() > 0) {
            internalBroker->subscribe(mqttConfig.topic_batocera.c_str(), [](const char* topic, const char* payload) {
                if (instance) instance->handleMessage(String(topic), String(payload));
            });
        }
        if (mqttConfig.topic_recalbox.length() > 0) {
            internalBroker->subscribe(mqttConfig.topic_recalbox.c_str(), [](const char* topic, const char* payload) {
                if (instance) instance->handleMessage(String(topic), String(payload));
            });
        }
        internalBroker->subscribe("/Recalbox/EmulationStation/Event", [](const char* topic, const char* payload) {
            if (instance) instance->handleMessage(String(topic), String(payload));
        });
        
        internalBroker->begin();
        LOGI("RetroFrontend", "Embedded MQTT Broker started successfully.");
    } else {
        LOGI("RetroFrontend", "Configuring external MQTT Client connecting to %s:%d", mqttConfig.broker.c_str(), mqttConfig.port);
        espClient.setTimeout(1); // 1s max timeout
        mqttClient.setClient(espClient);
        mqttClient.setServer(mqttConfig.broker.c_str(), mqttConfig.port);
        mqttClient.setCallback(FrontendSyncEngine::callback);
        mqttClient.setSocketTimeout(1);
    }
    systemMappings = loadMappingsFromSD();
}

void FrontendSyncEngine::reconnectTaskFunc(void* param) {
    FrontendSyncEngine* self = (FrontendSyncEngine*)param;
    if (self) {
        self->reconnect();
        self->isReconnecting = false;
        self->reconnectTaskHandle = nullptr;
    }
    vTaskDelete(NULL);
}

void FrontendSyncEngine::stop() {
    isGamePlaying = false;
    waitingDisplayed = false;
    hasPendingEvent = false;
    
    if (reconnectTaskHandle) {
        vTaskDelete(reconnectTaskHandle);
        reconnectTaskHandle = nullptr;
    }
    isReconnecting = false;

    if (internalBroker) {
        delete internalBroker;
        internalBroker = nullptr;
    }
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    
    if (gif) gif->stop();
    if (message) message->deactivate();
    LOGI("RetroFrontend", "MQTT Listener stopped.");
}

bool FrontendSyncEngine::loop() {
    if (!mqttConfig.enabled) {
        if (waitingDisplayed) {
            waitingDisplayed = false;
            if (message) message->deactivate();
        }
        return true;
    }
    
    if (internalBroker) {
        internalBroker->loop();
    } else {
        if (!mqttClient.connected()) {
            long now = millis();
            if (now - lastReconnectAttempt > 30000 && !isReconnecting) {
                lastReconnectAttempt = now;
                isReconnecting = true;
                xTaskCreatePinnedToCore(reconnectTaskFunc, "MqttReconnect", 4096, this, 1, &reconnectTaskHandle, 0);
            }
        } else {
            mqttClient.loop();
        }
    }
    
    if (hasPendingEvent) {
        hasPendingEvent = false;
        uint32_t reqId = currentRequestId;
        handleGameEvent(pendingPayload, reqId);
    } else if (!hasReceivedAnyEvent && !waitingDisplayed && message && millis() > 12000) {
        waitingDisplayed = true;
        MessageConfig cfg = { "WAITING FOR MARQUEE", 0xFFFF, 1, "rtl", 40, 0 };
        message->displayMessage(cfg);
        LOGI("RetroFrontend", "MQTT Enabled: Displaying WAITING FOR MARQUEE on DMD.");
    }
    
    return true;
}

void FrontendSyncEngine::reconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Attempting MQTT connection...");
        if (mqttClient.connect(mqttConfig.deviceName.c_str(), mqttConfig.user.c_str(), mqttConfig.pass.c_str())) {
            Serial.println("connected");
            // Subscribe to the *configured* topics (config.json [MQTT] TOPIC_BATOCERA/TOPIC_RECALBOX)
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

void FrontendSyncEngine::callback(char* topic, byte* payload, unsigned int length) {
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
void FrontendSyncEngine::handleMessage(String topic, String msg) {
    LOGI("RetroFrontend", "MQTT Event Received: Topic = '%s', Payload = '%s'", topic.c_str(), msg.c_str());
    
    if (topic == mqttConfig.topic_recalbox || topic == mqttConfig.topic_batocera ||
        topic.startsWith("recalbox/system/playing") || topic.startsWith("batocera/system/playing")) {
        LOGI("RetroFrontend", "Matched Recalbox/Batocera playing topic, queueing game event.");
        pendingPayload = msg;
        hasPendingEvent = true;
        currentRequestId++;
        return;
    }
    
    if (topic == "/Recalbox/EmulationStation/Event") {
        if (msg == "stop" || msg == "stopgame") {
            LOGI("RetroFrontend", "Received native EmulationStation stop event, returning to idle rotation.");
            if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
                gif->stop();
                xSemaphoreGive(sdMutex);
            }
            hasPendingEvent = false;
        } else if (msg == "rungame") {
            LOGI("RetroFrontend", "Received native EmulationStation rungame event.");
            if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
                gif->playGif("/gifs/recalbox_generic.raw");
                xSemaphoreGive(sdMutex);
            }
        }
        return;
    }
}

void FrontendSyncEngine::handleGameEvent(const String& jsonPayload, uint32_t reqId) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, jsonPayload);
    if (err) {
        LOGE("RetroFrontend", "Failed to parse MQTT JSON payload: %s", err.c_str());
        return;
    }

    const char* status = doc["status"] | "";
    const char* gameRaw = doc["game"] | "";
    const char* systemRaw = doc["system"] | "";
    const char* typeRaw = doc["type"] | "";

    hasReceivedAnyEvent = true;

    if (strcmp(status, "stopped") == 0) {
        LOGI("RetroFrontend", "Received stopped event, keeping last marquee displayed.");
        hasPendingEvent = false;
        return;
    }

    String cleanSystem = cleanSystemName(String(systemRaw));
    String cleanGame = cleanSystemName(String(gameRaw));

    if (strcmp(typeRaw, "system") == 0 || cleanGame.length() == 0 || cleanGame.equalsIgnoreCase(cleanSystem)) {
        handleSystemEvent(cleanSystem.length() > 0 ? cleanSystem : String(systemRaw), reqId);
        return;
    }

    isGamePlaying = true;
    waitingDisplayed = false;
    if (message) message->deactivate();

    String game = String(gameRaw);
    String folder = mapSystemToPixelcadeFolder(cleanSystem);

    String cleanGameNoTags = game;
    int idxParen = cleanGameNoTags.indexOf(" (");
    if (idxParen != -1) cleanGameNoTags = cleanGameNoTags.substring(0, idxParen);
    int idxBrack = cleanGameNoTags.indexOf(" [");
    if (idxBrack != -1) cleanGameNoTags = cleanGameNoTags.substring(0, idxBrack);
    cleanGameNoTags.trim();

    std::vector<String> nameVariants;
    nameVariants.push_back(game);
    if (cleanGameNoTags.length() > 0 && cleanGameNoTags != game) {
        nameVariants.push_back(cleanGameNoTags);
    }
    if (cleanGame.length() > 0 && cleanGame != game && cleanGame != cleanGameNoTags) {
        nameVariants.push_back(cleanGame);
    }

    String foundArtPath = "";
    bool exists = false;

    bool lockAcquired = (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100)));

    for (const String& nameVar : nameVariants) {
        String testPaths[] = {
            "/pixelcade/" + folder + "/" + nameVar,
            "/" + folder + "/" + nameVar,
            "/pixelcade/console/" + nameVar,
            "/console/" + nameVar
        };
        for (const auto& basePath : testPaths) {
            if (sd.exists(basePath + ".png")) {
                foundArtPath = basePath + ".png";
                exists = true;
                break;
            }
            if (sd.exists(basePath + ".gif")) {
                foundArtPath = basePath + ".gif";
                exists = true;
                break;
            }
        }
        if (exists) break;
    }

    if (lockAcquired) {
        xSemaphoreGive(sdMutex);
    }

    if (reqId != currentRequestId) return;

    if (exists && foundArtPath.length() > 0) {
        LOGI("RetroFrontend", "Playing cached Pixelcade art: %s", foundArtPath.c_str());
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            gif->playGif(foundArtPath.c_str());
            xSemaphoreGive(sdMutex);
        }
        return;
    }

    LOGI("RetroFrontend", "No cached artwork for %s, displaying text title and downloading...", game.c_str());
    if (message) {
        String clean = cleanSystemName(game);
        clean.replace("-", " ");
        clean.replace("_", " ");
        MessageConfig cfg = { clean, 0x07FF, 1, clean.length() > 8 ? "rtl" : "none", 40, 0 };
        message->displayMessage(cfg);
    }
    
    String downloadedPath = "";
    bool downloaded = false;
    for (const String& nameVar : nameVariants) {
        if (downloadPixelcadeArt(folder, nameVar + ".png", downloadedPath, reqId)) {
            downloaded = true;
            break;
        }
        if (reqId != currentRequestId) return;
        
        if (downloadPixelcadeArt(folder, nameVar + ".gif", downloadedPath, reqId)) {
            downloaded = true;
            break;
        }
        if (reqId != currentRequestId) return;
    }

    if (reqId != currentRequestId) return;

    if (downloaded && downloadedPath.length() > 0) {
        LOGI("RetroFrontend", "Playing downloaded Pixelcade art: %s", downloadedPath.c_str());
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            gif->playGif(downloadedPath.c_str());
            xSemaphoreGive(sdMutex);
        }
        if (message) {
            MessageConfig emptyCfg = { "", 0x0000, 1, "none", 0, 0 };
            message->displayMessage(emptyCfg);
        }
    }
}

void FrontendSyncEngine::handleSystemEvent(const String& systemId, uint32_t reqId) {
    isGamePlaying = true;
    waitingDisplayed = false;
    if (message) message->deactivate();

    std::vector<SystemVariant> variants = getSystemNameVariantsMapped(systemMappings, systemId);

    String foundArtPath = "";
    bool exists = false;

    bool lockAcquired = (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100)));

    for (const auto& v : variants) {
        String testPaths[] = {
            "/pixelcade/" + v.folder + "/" + v.name,
            "/" + v.folder + "/" + v.name,
            "/pixelcade/" + v.name,
            "/" + v.name
        };
        for (const auto& basePath : testPaths) {
            if (sd.exists(basePath + ".png")) {
                foundArtPath = basePath + ".png";
                exists = true;
                break;
            }
            if (sd.exists(basePath + ".gif")) {
                foundArtPath = basePath + ".gif";
                exists = true;
                break;
            }
        }
        if (exists) break;
    }

    if (lockAcquired) {
        xSemaphoreGive(sdMutex);
    }

    if (reqId != currentRequestId) return;

    if (exists && foundArtPath.length() > 0) {
        LOGI("RetroFrontend", "Playing cached Pixelcade system art: %s", foundArtPath.c_str());
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            gif->playGif(foundArtPath.c_str());
            xSemaphoreGive(sdMutex);
        }
        return;
    }

    LOGI("RetroFrontend", "No cached artwork for system %s, displaying text title and downloading...", systemId.c_str());
    if (message) {
        String clean = cleanSystemName(systemId);
        clean.replace("-", " ");
        clean.replace("_", " ");
        clean.toUpperCase();
        MessageConfig cfg = { clean, 0x07FF, 1, clean.length() > 8 ? "rtl" : "none", 40, 0 };
        message->displayMessage(cfg);
    }

    String downloadedPath = "";
    bool downloaded = false;
    for (const auto& v : variants) {
        if (downloadPixelcadeArt(v.folder, v.name + ".png", downloadedPath, reqId)) {
            downloaded = true;
            break;
        }
        if (reqId != currentRequestId) return;

        if (downloadPixelcadeArt(v.folder, v.name + ".gif", downloadedPath, reqId)) {
            downloaded = true;
            break;
        }
        if (reqId != currentRequestId) return;
    }

    if (reqId != currentRequestId) return;

    if (downloaded && downloadedPath.length() > 0) {
        LOGI("RetroFrontend", "Playing downloaded Pixelcade system art: %s", downloadedPath.c_str());
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            gif->playGif(downloadedPath.c_str());
            xSemaphoreGive(sdMutex);
        }
        if (message) {
            MessageConfig emptyCfg = { "", 0x0000, 1, "none", 0, 0 };
            message->displayMessage(emptyCfg);
        }
    }
}

String FrontendSyncEngine::cleanSystemName(const String& rawSystem) {
    String s = rawSystem;
    s.trim();
    
    String sNorm = s;
    sNorm.toLowerCase();
    sNorm.replace("_", " ");
    sNorm.replace("-", " ");
    
    const char* prefixes[] = {
        "arcade manufacturer ",
        "arcade system ",
        "arcade genre ",
        "arcade collection ",
        "manufacturer ",
        "system ",
        "genre ",
        "collection "
    };
    
    for (const char* p : prefixes) {
        if (sNorm.startsWith(p)) {
            int len = strlen(p);
            if (len <= s.length()) {
                String remainder = s.substring(len);
                if (remainder.startsWith("_") || remainder.startsWith("-")) {
                    remainder = remainder.substring(1);
                }
                remainder.trim();
                return remainder;
            }
        }
    }
    return s;
}

#include "BuiltinSystemMaps.h"

std::map<String, std::vector<String>> FrontendSyncEngine::loadMappingsFromSD() {
    std::map<String, std::vector<String>> mappings;

    // 1. Pre-populate with firmware embedded default mappings (290+ systems & manufacturers)
    for (size_t i = 0; BUILTIN_SYSTEM_MAPS[i].key != nullptr; i++) {
        String key = String(BUILTIN_SYSTEM_MAPS[i].key);
        String targetsStr = String(BUILTIN_SYSTEM_MAPS[i].targets);
        std::vector<String> targets;
        while (targetsStr.length() > 0) {
            int commaIdx = targetsStr.indexOf(',');
            String t;
            if (commaIdx != -1) {
                t = targetsStr.substring(0, commaIdx);
                targetsStr = targetsStr.substring(commaIdx + 1);
            } else {
                t = targetsStr;
                targetsStr = "";
            }
            t.trim();
            if (t.length() > 0) targets.push_back(t);
        }
        if (targets.size() > 0) {
            mappings[key] = targets;
        }
    }

    const char* jsonPaths[] = { "/pixelcade/systems.json", "/systems.json" };
    bool loadedJson = false;

    for (const char* p : jsonPaths) {
        if (sd.exists(p)) {
            FsFile f = sd.open(p, FILE_OPEN_READ);
            if (f) {
                DynamicJsonDocument doc(16384);
                DeserializationError err = deserializeJson(doc, f);
                f.close();
                if (!err && doc.is<JsonObject>()) {
                    JsonObject root = doc.as<JsonObject>();
                    for (JsonPair kv : root) {
                        String key = String(kv.key().c_str());
                        key.trim();
                        key.toLowerCase();
                        std::vector<String> targets;
                        if (kv.value().is<JsonArray>()) {
                            for (JsonVariant val : kv.value().as<JsonArray>()) {
                                if (val.is<const char*>()) {
                                    targets.push_back(String(val.as<const char*>()));
                                }
                            }
                        } else if (kv.value().is<const char*>()) {
                            targets.push_back(String(kv.value().as<const char*>()));
                        }
                        if (key.length() > 0 && targets.size() > 0) {
                            mappings[key] = targets;
                        }
                    }
                    LOGI("RetroFrontend", "Loaded system mappings from SD JSON: %s (%u entries)", p, (unsigned int)mappings.size());
                    loadedJson = true;
                    break;
                }
            }
        }
    }

    if (!loadedJson) {
        const char* csvPaths[] = { "/pixelcade/console.csv", "/console.csv" };
        for (const char* p : csvPaths) {
            if (sd.exists(p)) {
                FsFile f = sd.open(p, FILE_OPEN_READ);
                if (f) {
                    while (f.available()) {
                        String line = f.readStringUntil('\n');
                        line.trim();
                        if (line.length() == 0 || line.startsWith("#")) continue;
                        int commaIdx = line.indexOf(',');
                        if (commaIdx != -1) {
                            String key = line.substring(0, commaIdx);
                            key.trim();
                            key.toLowerCase();
                            String rest = line.substring(commaIdx + 1);
                            std::vector<String> targets;
                            while (rest.length() > 0) {
                                int nextComma = rest.indexOf(',');
                                String target;
                                if (nextComma != -1) {
                                    target = rest.substring(0, nextComma);
                                    rest = rest.substring(nextComma + 1);
                                } else {
                                    target = rest;
                                    rest = "";
                                }
                                target.trim();
                                if (target.length() > 0) {
                                    targets.push_back(target);
                                }
                            }
                            if (key.length() > 0 && targets.size() > 0) {
                                mappings[key] = targets;
                            }
                        }
                    }
                    f.close();
                    LOGI("RetroFrontend", "Loaded system mappings from SD CSV: %s", p);
                    break;
                }
            }
        }
    }
    return mappings;
}

std::vector<FrontendSyncEngine::SystemVariant> FrontendSyncEngine::getSystemNameVariants(const String& rawSystem) {
    std::map<String, std::vector<String>> emptyMap;
    return getSystemNameVariantsMapped(emptyMap, rawSystem);
}

std::vector<FrontendSyncEngine::SystemVariant> FrontendSyncEngine::getSystemNameVariantsMapped(
    const std::map<String, std::vector<String>>& mappings,
    const String& rawSystem) {
    String clean = cleanSystemName(rawSystem);
    String rawLower = rawSystem;
    rawLower.trim();
    rawLower.toLowerCase();

    std::vector<String> nameVariants;

    String sysLower = clean;
    sysLower.toLowerCase();
    String sysNospace = sysLower;
    sysNospace.replace(" ", "");
    String sysUnderscore = sysLower;
    sysUnderscore.replace(" ", "_");

    // 1. High priority: User / systems.json explicit mappings
    String lookupKeys[] = { rawLower, sysLower, sysNospace, sysUnderscore };
    for (const auto& key : lookupKeys) {
        auto it = mappings.find(key);
        if (it != mappings.end()) {
            for (const auto& target : it->second) {
                bool exists = false;
                for (const auto& nv : nameVariants) {
                    if (nv == target) { exists = true; break; }
                }
                if (!exists) nameVariants.push_back(target);
            }
        }
    }

    // Check embedded keywords in multi-word names (e.g., "Capcom cps1" -> "cps1", "capcom")
    const char* embeddedKeywords[] = { "cps1", "cps2", "cps3", "atomiswave", "naomi", "neogeo" };
    for (const char* kw : embeddedKeywords) {
        if (sysNospace.indexOf(kw) != -1) {
            String kwStr = String(kw);
            auto it = mappings.find(kwStr);
            if (it != mappings.end()) {
                for (const auto& target : it->second) {
                    bool exists = false;
                    for (const auto& nv : nameVariants) {
                        if (nv == target) { exists = true; break; }
                    }
                    if (!exists) nameVariants.push_back(target);
                }
            }
            String defZ = "default-z" + kwStr;
            bool existsZ = false;
            for (const auto& nv : nameVariants) {
                if (nv == defZ) { existsZ = true; break; }
            }
            if (!existsZ) nameVariants.push_back(defZ);

            String def = "default-" + kwStr;
            bool existsDef = false;
            for (const auto& nv : nameVariants) {
                if (nv == def) { existsDef = true; break; }
            }
            if (!existsDef) nameVariants.push_back(def);
        }
    }

    // Extract individual words in reverse order (e.g. "cps1" before "capcom")
    int lastSpace = 0;
    std::vector<String> words;
    for (size_t i = 0; i <= sysLower.length(); i++) {
        if (i == sysLower.length() || sysLower[i] == ' ') {
            if (i > (size_t)lastSpace) {
                String w = sysLower.substring(lastSpace, i);
                w.trim();
                if (w.length() > 0 && w != "arcade" && w != "manufacturer" && w != "system" && w != "genre") {
                    words.push_back(w);
                }
            }
            lastSpace = i + 1;
        }
    }
    for (int i = (int)words.size() - 1; i >= 0; i--) {
        const String& w = words[i];
        auto it = mappings.find(w);
        if (it != mappings.end()) {
            for (const auto& target : it->second) {
                bool exists = false;
                for (const auto& nv : nameVariants) {
                    if (nv == target) { exists = true; break; }
                }
                if (!exists) nameVariants.push_back(target);
            }
        }
        String defZ = "default-z" + w;
        bool existsZ = false;
        for (const auto& nv : nameVariants) {
            if (nv == defZ) { existsZ = true; break; }
        }
        if (!existsZ) nameVariants.push_back(defZ);

        String def = "default-" + w;
        bool existsDef = false;
        for (const auto& nv : nameVariants) {
            if (nv == def) { existsDef = true; break; }
        }
        if (!existsDef) nameVariants.push_back(def);
    }

    std::vector<String> baseNames;
    String sysUpper = clean;
    sysUpper.toUpperCase();
    String sysSpace = clean;
    sysSpace.replace("_", " ");

    String sysTitle = clean;
    bool newWord = true;
    for (size_t i = 0; i < sysTitle.length(); i++) {
        if (sysTitle[i] == '_' || sysTitle[i] == ' ') {
            sysTitle[i] = ' ';
            newWord = true;
        } else if (newWord) {
            sysTitle[i] = toupper(sysTitle[i]);
            newWord = false;
        } else {
            sysTitle[i] = tolower(sysTitle[i]);
        }
    }

    baseNames.push_back(clean);
    if (sysLower != clean) baseNames.push_back(sysLower);
    if (sysNospace != clean && sysNospace != sysLower) baseNames.push_back(sysNospace);
    if (sysUnderscore != clean && sysUnderscore != sysLower && sysUnderscore != sysNospace) baseNames.push_back(sysUnderscore);
    if (sysUpper != clean && sysUpper != sysLower) baseNames.push_back(sysUpper);
    if (sysTitle != clean && sysTitle != sysLower && sysTitle != sysUpper) baseNames.push_back(sysTitle);
    if (sysSpace != clean && sysSpace != sysLower && sysSpace != sysUpper) baseNames.push_back(sysSpace);

    if (sysLower == "snes" || sysLower == "supernintendo") {
        baseNames.push_back("Super Nintendo");
        baseNames.push_back("Super Nintendo Entertainment System");
        baseNames.push_back("- Super Nintendo");
    } else if (sysLower == "nes" || sysLower == "famicom") {
        baseNames.push_back("Nintendo Entertainment System");
        baseNames.push_back("3dnes");
    } else if (sysLower == "megadrive" || sysLower == "genesis") {
        baseNames.push_back("genesis");
        baseNames.push_back("Genesis");
        baseNames.push_back("Mega Drive");
        baseNames.push_back("SEGA Genesis");
        baseNames.push_back("- Genesis");
    } else if (sysLower == "mame" || sysLower == "arcade" || sysLower == "fbneo" || sysLower == "fba") {
        baseNames.push_back("arcade");
        baseNames.push_back("Arcade");
        baseNames.push_back("- Arcade");
        baseNames.push_back("MAME");
        baseNames.push_back("mame");
    } else if (sysLower == "n64") {
        baseNames.push_back("Nintendo 64");
    } else if (sysLower == "gb" || sysLower == "gameboy") {
        baseNames.push_back("Game Boy");
    } else if (sysLower == "gba") {
        baseNames.push_back("Game Boy Advance");
    } else if (sysLower == "gbc") {
        baseNames.push_back("Game Boy Color");
    } else if (sysLower == "psx" || sysLower == "ps1") {
        baseNames.push_back("PlayStation");
        baseNames.push_back("Sony PlayStation");
    } else if (sysLower == "dreamcast") {
        baseNames.push_back("Dreamcast");
        baseNames.push_back("SEGA Dreamcast");
    } else if (sysLower == "neogeo") {
        baseNames.push_back("Neo Geo");
        baseNames.push_back("SNK Neo Geo");
    } else if (sysLower == "atari" || sysLower == "atari2600" || sysLower == "atari7800" || sysLower == "atari5200" || sysLower == "atari800" || sysLower == "atarilynx" || sysLower == "atarijaguar" || sysLower == "atarist") {
        baseNames.push_back("atari");
        baseNames.push_back("Atari");
        baseNames.push_back("Atari_2600");
        baseNames.push_back("Atari 2600");
        baseNames.push_back("Atari_7800");
        baseNames.push_back("Atari 7800");
    } else if (sysLower == "mastersystem") {
        baseNames.push_back("Master System");
        baseNames.push_back("SEGA Master System");
    } else if (sysLower == "gamegear") {
        baseNames.push_back("Game Gear");
        baseNames.push_back("SEGA Game Gear");
    } else if (sysLower == "pcengine" || sysLower == "tg16") {
        baseNames.push_back("NEC PC Engine");
        baseNames.push_back("PC Engine");
    } else if (sysLower == "amiga") {
        baseNames.push_back("Commodore Amiga");
        baseNames.push_back("Amiga");
    } else if (sysLower == "c64") {
        baseNames.push_back("COMMODORE_64");
        baseNames.push_back("Commodore 64");
    }

    std::vector<String> uniqueBase;
    for (const auto& n : baseNames) {
        bool found = false;
        for (const auto& un : uniqueBase) {
            if (un == n) { found = true; break; }
        }
        if (!found) uniqueBase.push_back(n);
    }

    String cleanLower = clean;
    cleanLower.toLowerCase();
    String cleanNospace = cleanLower;
    cleanNospace.replace(" ", "");
    String cleanUnderscore = cleanLower;
    cleanUnderscore.replace(" ", "_");
    String cleanKebab = cleanLower;
    cleanKebab.replace(" ", "-");

    // 1. Direct name variants (e.g. cave, atari, capcom, data_east, dataeast)
    nameVariants.push_back(cleanLower);
    nameVariants.push_back(clean);
    nameVariants.push_back(cleanUnderscore);
    nameVariants.push_back(cleanNospace);
    nameVariants.push_back(cleanKebab);

    // 2. default- prefixed variants
    nameVariants.push_back("default-" + clean);
    nameVariants.push_back("default-" + cleanLower);
    nameVariants.push_back("default-" + cleanUnderscore);
    nameVariants.push_back("default-" + cleanNospace);
    nameVariants.push_back("default-" + cleanKebab);
    nameVariants.push_back("default-_" + clean);
    nameVariants.push_back("default-_" + cleanLower);
    nameVariants.push_back("default-_" + cleanUnderscore);

    // 3. Pixelcade z-prefixed board/publisher conventions
    nameVariants.push_back("default-z" + cleanLower);
    nameVariants.push_back("default-z" + cleanNospace);
    nameVariants.push_back("z" + cleanLower);
    nameVariants.push_back("z" + cleanNospace);

    // 4. Publisher classic collections
    nameVariants.push_back("default-arcade_" + cleanUnderscore + "_classics");
    nameVariants.push_back("default-arcade" + cleanNospace + "classics");
    nameVariants.push_back("default-manufacture_" + cleanUnderscore);
    nameVariants.push_back("default-manufacture_" + cleanLower);

    for (const auto& b : uniqueBase) {
        String bLower = b;
        bLower.toLowerCase();
        String bNospace = bLower;
        bNospace.replace(" ", "");
        String bUnderscore = bLower;
        bUnderscore.replace(" ", "_");

        nameVariants.push_back(b);
        nameVariants.push_back(bLower);
        nameVariants.push_back(bUnderscore);
        nameVariants.push_back("default-" + b);
        nameVariants.push_back("default-" + bLower);
        nameVariants.push_back("default-_" + b);
        nameVariants.push_back("default-z" + bLower);
        nameVariants.push_back("default-z" + bNospace);
        nameVariants.push_back("default-arcade_" + bUnderscore + "_classics");
        nameVariants.push_back("default-arcade" + bNospace + "classics");
        nameVariants.push_back("default-manufacture_" + bUnderscore);
        nameVariants.push_back("default-manufacture_" + bLower);
    }

    std::vector<String> finalNames;
    for (const auto& n : nameVariants) {
        bool found = false;
        for (const auto& fn : finalNames) {
            if (fn == n) { found = true; break; }
        }
        if (!found) finalNames.push_back(n);
    }

    // Folder search is exclusively /console/
    std::vector<SystemVariant> list;
    for (const auto& n : finalNames) {
        list.push_back({ "console", n });
    }

    return list;
}

String FrontendSyncEngine::mapSystemToPixelcadeFolder(const String& systemId) {
    String sLower = systemId;
    sLower.trim();
    sLower.toLowerCase();
    sLower.replace(" ", "");
    sLower.replace("_", "");
    sLower.replace("-", "");

    struct Mapping { const char* systemId; const char* folder; };
    static const Mapping table[] = {
        {"mame", "mame"}, {"fbneo", "mame"}, {"fba", "mame"}, {"arcade", "mame"},
        {"cave", "mame"}, {"capcom", "mame"}, {"cps1", "mame"}, {"cps2", "mame"}, {"cps3", "mame"},
        {"konami", "mame"}, {"taito", "mame"}, {"dataeast", "mame"}, {"midway", "mame"},
        {"irem", "mame"}, {"namco", "mame"}, {"toaplan", "mame"}, {"technos", "mame"},
        {"sammy", "mame"}, {"atomiswave", "mame"}, {"naomi", "mame"}, {"neogeo", "neogeo"}, {"snk", "mame"},
        {"nes", "console/nes"}, {"famicom", "console/nes"},
        {"snes", "console/snes"}, {"supernintendo", "console/snes"},
        {"n64", "console/n64"}, {"nintendo64", "console/n64"},
        {"gb", "console/gb"}, {"gameboy", "console/gb"},
        {"gba", "console/gba"}, {"gameboyadvance", "console/gba"},
        {"gbc", "console/gbc"}, {"gameboycolor", "console/gbc"},
        {"megadrive", "console/genesis"}, {"genesis", "console/genesis"},
        {"mastersystem", "console/mastersystem"}, {"gamegear", "console/gamegear"},
        {"psx", "console/psx"}, {"ps1", "console/psx"}, {"playstation", "console/psx"},
        {"ps2", "console/ps2"}, {"psp", "console/psp"},
        {"dreamcast", "console/dreamcast"}, {"saturn", "console/saturn"},
        {"pcengine", "console/pcengine"}, {"tg16", "console/pcengine"},
        {"atari2600", "console/atari2600"}, {"atari5200", "console/atari5200"}, {"atari7800", "console/atari7800"}
    };
    for (const auto& m : table) {
        if (sLower == m.systemId) return String(m.folder);
    }
    return systemId;
}

bool FrontendSyncEngine::downloadPixelcadeArt(const String& folder, const String& filename, String& outPath, uint32_t reqId) {

    String baseUrl = "https://raw.githubusercontent.com/red77290/pixelcade/master/" + folder + "/";
    // URL encode the filename spaces if any
    String urlFileName = filename;
    urlFileName.replace(" ", "%20");
    urlFileName.replace("!", "%21");
    urlFileName.replace("'", "%27");
    urlFileName.replace("(", "%28");
    urlFileName.replace(")", "%29");
    urlFileName.replace("&", "%26");
    String url = baseUrl + urlFileName;
    
    String dirPath = "/pixelcade/" + folder;
    String savePath = dirPath + "/" + filename;

    HTTPClient http;
    http.setTimeout(4000);
    http.setUserAgent("ArcadeMatrix-ESP32");
    
    LOGI("RetroFrontend", "Downloading %s", url.c_str());
    
    // Disable Task Watchdog on Core 1 temporarily because HTTP GET or TLS decryption 
    // can block for > 5 seconds on massive GIFs over a slow connection!
    esp_task_wdt_delete(NULL);
    
    if (http.begin(url)) {
        LOGI("RetroFrontend", "Starting HTTP GET...");
        int httpCode = http.GET();
        LOGI("RetroFrontend", "HTTP GET returned %d", httpCode);
        
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            int len = http.getSize();
            WiFiClient* stream = http.getStreamPtr();
            
            String dirPath = "/pixelcade/" + folder;
            String savePath = dirPath + "/" + filename;
            
            // The caller (main.cpp) already holds sdMutex! Do not take it again or it will deadlock!
            if (!sd.exists("/pixelcade")) {
                sd.mkdir("/pixelcade");
            }
            if (!sd.exists(dirPath.c_str())) {
                sd.mkdir(dirPath.c_str());
            }
            
            FsFile file = sd.open(savePath.c_str(), FILE_OPEN_WRITE);
            if (file) {
                WiFiClient* stream = http.getStreamPtr();
                    int len = http.getSize();
                    uint8_t buff[512] = { 0 };
                    
                    int bytesWrittenTotal = 0;
                    unsigned long lastLog = millis();
                    
                    while (http.connected() && (len > 0 || len == -1)) {
                        size_t size = stream->available();
                        if (size) {
                            // Use non-blocking read() instead of blocking readBytes()!
                            // readBytes() can block for many seconds if Wi-Fi is slow, bypassing our watchdog reset.
                            int c = stream->read(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                            if (c > 0) {
                                file.write(buff, c);
                                bytesWrittenTotal += c;
                                if (len > 0) len -= c;
                            }
                        } else {
                            delay(1);
                        }
                        
                        // THIS IS THE FIX: The Pixelcade GIFs can be 2MB and take 20s to download.
                        // delay(1) does NOT feed the Task Watchdog for the loopTask, so we must
                        // explicitly reset it here to prevent the ESP32 from panicking!
                        esp_task_wdt_reset();
                        
                        if (millis() - lastLog > 2000) {
                            LOGI("RetroFrontend", "Downloading... %d bytes written", bytesWrittenTotal);
                            lastLog = millis();
                        }
                        
                        // Keep processing MQTT messages while downloading!
                        if (internalBroker) internalBroker->loop();
                        if (mqttClient.connected()) mqttClient.loop();
                        
                        // If the user scrolled to a new game, abort this download!
                        if (reqId != currentRequestId) {
                            LOGI("RetroFrontend", "User scrolled to a new game, aborting download of %s", savePath.c_str());
                            file.close();
                            sd.remove(savePath.c_str());
                            http.end();
                            esp_task_wdt_add(NULL);
                            return false;
                        }
                    }
                    
                file.close();
                
                // Verify file was written
                if (bytesWrittenTotal > 0) {
                    if (sd.exists(savePath.c_str()) && sd.open(savePath.c_str(), FILE_OPEN_READ).size() > 100) {
                        outPath = savePath;
                        http.end();
                        LOGI("RetroFrontend", "Successfully downloaded and saved to %s", savePath.c_str());
                        esp_task_wdt_add(NULL); // Re-enable watchdog
                        return true;
                    }
                    // Delete corrupted/empty file
                    sd.remove(savePath.c_str());
                }
            } else {
                LOGE("RetroFrontend", "Failed to open file for writing: %s", savePath.c_str());
            }
        } else {
            LOGI("RetroFrontend", "HTTP GET failed for %s, error: %s", filename.c_str(), http.errorToString(httpCode).c_str());
        }
        http.end();
    }
    
    esp_task_wdt_add(NULL); // Re-enable watchdog on failure paths!
    return false;
}
