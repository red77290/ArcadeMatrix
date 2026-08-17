#include "RetroFrontendListener.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"
#include "../core/Logger.h"
#include "../core/Globals.h"
#include <esp_task_wdt.h>

RetroFrontendListener* RetroFrontendListener::instance = nullptr;

RetroFrontendListener::RetroFrontendListener(MqttConfig& config, GifEngine* gifEngine, ClockEngine* clockEngine, MessageEngine* messageEngine)
    : mqttConfig(config), gif(gifEngine), clock(clockEngine), message(messageEngine), mqttClient(espClient) {
    instance = this;
    lastReconnectAttempt = 0;
}

void RetroFrontendListener::begin() {
    if (!mqttConfig.enabled || mqttConfig.broker.isEmpty()) return;
    
    if (mqttConfig.broker == "127.0.0.1" || mqttConfig.broker == "localhost") {
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
        mqttClient.setServer(mqttConfig.broker.c_str(), mqttConfig.port);
        mqttClient.setCallback(RetroFrontendListener::callback);
    }
}

void RetroFrontendListener::stop() {
    isGamePlaying = false;
    waitingDisplayed = false;
    if (gif) gif->stop();
    if (message) message->stop();
    LOGI("RetroFrontend", "MQTT Listener stopped.");
}

bool RetroFrontendListener::loop() {
    if (!mqttConfig.enabled) {
        if (waitingDisplayed) {
            waitingDisplayed = false;
            if (message) message->stop();
        }
        return true;
    }
    
    if (internalBroker) {
        internalBroker->loop();
    } else {
        if (!mqttClient.connected()) {
            long now = millis();
            if (now - lastReconnectAttempt > 30000) {
                lastReconnectAttempt = now;
                reconnect();
            }
        } else {
            mqttClient.loop();
        }
    }
    
    if (hasPendingEvent) {
        hasPendingEvent = false;
        uint32_t reqId = currentRequestId;
        handleGameEvent(pendingPayload, reqId);
    } else if (!isGamePlaying && !waitingDisplayed && message && millis() > 12000) {
        waitingDisplayed = true;
        MessageConfig cfg = { "WAITING FOR MARQUEE", 0xFFFF, 1, "none", 40, 0 };
        message->displayMessage(cfg);
        LOGI("RetroFrontend", "MQTT Enabled: Displaying WAITING FOR MARQUEE on DMD.");
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

void RetroFrontendListener::handleGameEvent(const String& jsonPayload, uint32_t reqId) {
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

    if (strcmp(status, "stopped") == 0) {
        LOGI("RetroFrontend", "Received stopped event, returning to WAITING FOR MARQUEE.");
        isGamePlaying = false;
        waitingDisplayed = false;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            if (gif) gif->stop();
            if (message) message->stop();
            xSemaphoreGive(sdMutex);
        }
        hasPendingEvent = false;
        return;
    }

    if (strcmp(typeRaw, "system") == 0 || (strlen(gameRaw) == 0 && strlen(systemRaw) > 0)) {
        handleSystemEvent(String(systemRaw), reqId);
        return;
    }

    if (strlen(gameRaw) == 0) {
        LOGI("RetroFrontend", "Received empty game event, ignored.");
        return;
    }

    isGamePlaying = true;
    waitingDisplayed = false;
    if (message) message->stop();

    String game = String(gameRaw);
    String folder = mapSystemToPixelcadeFolder(String(systemRaw));

    String cleanGame = game;
    int idxParen = cleanGame.indexOf(" (");
    if (idxParen != -1) cleanGame = cleanGame.substring(0, idxParen);
    int idxBrack = cleanGame.indexOf(" [");
    if (idxBrack != -1) cleanGame = cleanGame.substring(0, idxBrack);
    cleanGame.trim();

    std::vector<String> nameVariants;
    nameVariants.push_back(game);
    if (cleanGame.length() > 0 && cleanGame != game) {
        nameVariants.push_back(cleanGame);
    }

    String foundArtPath = "";
    bool exists = false;

    bool lockAcquired = (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100)));

    for (const String& nameVar : nameVariants) {
        String basePath = "/pixelcade/" + folder + "/" + nameVar;
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
        String clean = game;
        clean.replace("-", " ");
        clean.replace("_", " ");
        MessageConfig cfg = { clean, 0x07FF, 1, clean.length() > 8 ? "rtl" : "none", 40, 30 };
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

void RetroFrontendListener::handleSystemEvent(const String& systemId, uint32_t reqId) {
    isGamePlaying = true;
    waitingDisplayed = false;
    if (message) message->stop();

    std::vector<SystemVariant> variants = getSystemNameVariants(systemId);

    String foundArtPath = "";
    bool exists = false;

    bool lockAcquired = (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100)));

    for (const auto& v : variants) {
        String basePath = "/pixelcade/" + v.folder + "/" + v.name;
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
        String clean = systemId;
        clean.replace("-", " ");
        clean.replace("_", " ");
        clean.toUpperCase();
        MessageConfig cfg = { clean, 0x07FF, 1, clean.length() > 8 ? "rtl" : "none", 40, 30 };
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

std::vector<RetroFrontendListener::SystemVariant> RetroFrontendListener::getSystemNameVariants(const String& systemId) {
    std::vector<String> names;
    String sysLower = systemId;
    sysLower.toLowerCase();
    String sysUpper = systemId;
    sysUpper.toUpperCase();
    String sysSpace = systemId;
    sysSpace.replace("_", " ");

    names.push_back(systemId);
    if (sysLower != systemId) names.push_back(sysLower);
    if (sysUpper != systemId && sysUpper != sysLower) names.push_back(sysUpper);
    if (sysSpace != systemId && sysSpace != sysLower && sysSpace != sysUpper) names.push_back(sysSpace);

    if (sysLower == "snes" || sysLower == "supernintendo") {
        names.push_back("Super Nintendo");
        names.push_back("Super Nintendo Entertainment System");
        names.push_back("- Super Nintendo");
    } else if (sysLower == "nes" || sysLower == "famicom") {
        names.push_back("Nintendo Entertainment System");
        names.push_back("3dnes");
    } else if (sysLower == "megadrive" || sysLower == "genesis") {
        names.push_back("genesis");
        names.push_back("Genesis");
        names.push_back("Mega Drive");
        names.push_back("SEGA Genesis");
        names.push_back("- Genesis");
    } else if (sysLower == "mame" || sysLower == "arcade" || sysLower == "fbneo" || sysLower == "fba") {
        names.push_back("arcade");
        names.push_back("Arcade");
        names.push_back("- Arcade");
        names.push_back("MAME");
        names.push_back("mame");
    } else if (sysLower == "n64") {
        names.push_back("Nintendo 64");
    } else if (sysLower == "gb" || sysLower == "gameboy") {
        names.push_back("Game Boy");
    } else if (sysLower == "gba") {
        names.push_back("Game Boy Advance");
    } else if (sysLower == "gbc") {
        names.push_back("Game Boy Color");
    } else if (sysLower == "psx" || sysLower == "ps1") {
        names.push_back("PlayStation");
        names.push_back("Sony PlayStation");
    } else if (sysLower == "dreamcast") {
        names.push_back("Dreamcast");
        names.push_back("SEGA Dreamcast");
    } else if (sysLower == "neogeo") {
        names.push_back("Neo Geo");
        names.push_back("SNK Neo Geo");
    } else if (sysLower == "atari2600") {
        names.push_back("Atari_2600");
        names.push_back("Atari 2600");
    } else if (sysLower == "mastersystem") {
        names.push_back("Master System");
        names.push_back("SEGA Master System");
    } else if (sysLower == "gamegear") {
        names.push_back("Game Gear");
        names.push_back("SEGA Game Gear");
    } else if (sysLower == "pcengine" || sysLower == "tg16") {
        names.push_back("NEC PC Engine");
        names.push_back("PC Engine");
    } else if (sysLower == "amiga") {
        names.push_back("Commodore Amiga");
        names.push_back("Amiga");
    } else if (sysLower == "c64") {
        names.push_back("COMMODORE_64");
        names.push_back("Commodore 64");
    }

    std::vector<String> uniqueNames;
    for (const auto& n : names) {
        bool found = false;
        for (const auto& un : uniqueNames) {
            if (un == n) { found = true; break; }
        }
        if (!found) uniqueNames.push_back(n);
    }

    std::vector<SystemVariant> list;
    const char* folders[] = { "system", "console" };
    for (const char* f : folders) {
        for (const auto& n : uniqueNames) {
            list.push_back({ String(f), n });
        }
    }

    return list;
}

String RetroFrontendListener::mapSystemToPixelcadeFolder(const String& systemId) {
    struct Mapping { const char* systemId; const char* folder; };
    static const Mapping table[] = {
        {"mame", "mame"}, {"fbneo", "mame"}, {"neogeo", "neogeo"},
        {"nes", "console/nes"}, {"snes", "console/snes"}, {"n64", "console/n64"},
        {"gb", "console/gb"}, {"gba", "console/gba"}, {"gbc", "console/gbc"},
        {"megadrive", "console/genesis"}, {"genesis", "console/genesis"},
        {"mastersystem", "console/mastersystem"}, {"gamegear", "console/gamegear"},
        {"psx", "console/psx"}, {"dreamcast", "console/dreamcast"},
        {"pcengine", "console/pcengine"}, {"atari2600", "console/atari2600"}
    };
    for (const auto& m : table) {
        if (systemId == m.systemId) return String(m.folder);
    }
    return systemId;
}

bool RetroFrontendListener::downloadPixelcadeArt(const String& folder, const String& filename, String& outPath, uint32_t reqId) {

    String baseUrl = "https://raw.githubusercontent.com/alinke/pixelcade/master/" + folder + "/";
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
