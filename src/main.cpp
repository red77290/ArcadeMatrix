#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "core/ConfigLoader.h"
#include "core/MatrixEngine.h"
#include "engines/GifEngine.h"
#include "engines/ClockEngine.h"
#include "engines/clocks/ArcadeClock.h"
#include "engines/MessageEngine.h"
#include "api/WebServerAPI.h"
#include "engines/RetroFrontendListener.h"
#include "engines/DateEngine.h"
#include "engines/WeatherEngine.h"
#include "engines/FighterEngine.h"
#include "core/RotationManager.h"

// Hardware Pins for SD Card (VSPI)
#define SD_CS_PIN 5
#define VSPI_SCK 18
#define VSPI_MISO 19
#define VSPI_MOSI 23

ConfigLoader config;
MatrixEngine matrixEngine;
GifEngine gifEngine;

ClockEngine* clockEngine = nullptr;
DateEngine* dateEngine = nullptr;
WeatherEngine* weatherEngine = nullptr;
FighterEngine* fighterEngine = nullptr;
RotationManager* rotationManager = nullptr;
MessageEngine* messageEngine = nullptr;
WebServerAPI* webServer = nullptr;
RetroFrontendListener* frontendListener = nullptr;

unsigned long lastTick = 0;
uint8_t currentMinute = 42;
#ifndef UNIT_TEST

void setup() {
    Serial.begin(115200);
    delay(1000);
    randomSeed(esp_random());
    Serial.println("\n\n--- Starting ArcadeMatrix Firmware ---");

    // 1. Initialize SD Card using VSPI
    SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, SPI)) {
        Serial.println("CRITICAL ERROR: SD Card Mount Failed!");
        while (1) { delay(100); }
    }
    Serial.println("SD Card mounted successfully.");

    // 2. Load Configuration from SD
    if (!config.parseFromSD("/conf.ini")) {
        Serial.println("Warning: /conf.ini not found or failed to parse. Using defaults.");
    } else {
        Serial.println("Configuration loaded from /conf.ini.");
    }

    // 3. Initialize Matrix
    Serial.printf("Matrix Config: %dx%d, Chain: %d\n", config.matrix.width, config.matrix.height, config.matrix.chainLength);
    if (!matrixEngine.begin(config.matrix)) {
        Serial.println("CRITICAL ERROR: Matrix init failed!");
        while (1) { delay(100); }
    }
    matrixEngine.setBrightness(config.matrix.powerLimitPercent);
    Serial.printf("Free Heap after Matrix init: %d bytes\n", ESP.getFreeHeap());

    // 4. Initialize Engines
    gifEngine.begin(matrixEngine.getDisplay());

    // Load saved GIF playlists from SD, fallback to default /gifs
    {
        File playlistFile = SD.open("/playlists_selected.json", FILE_READ);
        if (playlistFile) {
            DynamicJsonDocument doc(4096);
            DeserializationError error = deserializeJson(doc, playlistFile);
            playlistFile.close();
            if (!error) {
                JsonArray arr = doc["playlists"].as<JsonArray>();
                std::vector<String> paths;
                for (JsonVariant v : arr) paths.push_back(v.as<String>());
                if (!paths.empty()) {
                    gifEngine.setDefaultPlaylists(paths);
                    Serial.printf("Loaded %d GIF playlists from playlists_selected.json\n", paths.size());
                } else {
                    gifEngine.setDefaultPlaylists({"/gifs"});
                }
            } else {
                gifEngine.setDefaultPlaylists({"/gifs"});
            }
        } else {
            gifEngine.setDefaultPlaylists({"/gifs"});
        }
    }

    clockEngine = new ClockEngine(matrixEngine.getDisplay());
    clockEngine->setTheme(static_cast<PublisherTheme>(config.time.clock_theme));
    dateEngine = new DateEngine(matrixEngine.getDisplay());
    dateEngine->setResolution(config.matrix.width, config.matrix.height);
    dateEngine->setTheme((PublisherTheme)config.dateSettings.theme);
    
    weatherEngine = new WeatherEngine(matrixEngine.getDisplay());
    fighterEngine = new FighterEngine(matrixEngine.getDisplay());
    fighterEngine->initialize();
    rotationManager = new RotationManager(clockEngine, dateEngine, weatherEngine, &gifEngine, fighterEngine);
    messageEngine = new MessageEngine(matrixEngine.getDisplay());

    // 5. Connect to Wi-Fi
    if (config.wifi.ssid.length() > 0) {
        Serial.printf("Connecting to Wi-Fi: %s\n", config.wifi.ssid.c_str());
        
        MessageConfig connMsg = {"Connecting to Wi-Fi...", 0xFFFF, 1, "rtl", 50, 10};
        messageEngine->displayMessage(connMsg);
        
        WiFi.setHostname(config.wifi.hostname.c_str());
        WiFi.begin(config.wifi.ssid.c_str(), config.wifi.password.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            matrixEngine.getDisplay()->fillScreen(0);
            messageEngine->loop();
            matrixEngine.getDisplay()->flipDMABuffer();
            attempts++;
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Wi-Fi Connected!");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            
            String ipMsg = "IP: " + WiFi.localIP().toString();
            MessageConfig ipConfig = {ipMsg, 0x07E0, 1, "rtl", 50, 5};
            messageEngine->displayMessage(ipConfig);
            
            if (MDNS.begin(config.wifi.hostname.c_str())) {
                Serial.printf("mDNS responder started: http://%s.local\n", config.wifi.hostname.c_str());
            }
            
            configTzTime(config.time.timezone.c_str(), config.time.ntpServer.c_str());
            Serial.println("NTP Time Sync initiated.");
            
            webServer = new WebServerAPI(80, messageEngine, clockEngine);
            webServer->begin();
            MDNS.addService("http", "tcp", 80);
            
            if (config.mqtt.enabled) {
                frontendListener = new RetroFrontendListener(config.mqtt, &gifEngine, clockEngine);
                frontendListener->begin();
            }
        } else {
            Serial.println("Wi-Fi connection failed. Starting Access Point (AP) Mode.");
            WiFi.mode(WIFI_AP);
            WiFi.softAP("ArcadeMatrix", "12345678");
            
            String apMsg = "Offline Mode (AP: ArcadeMatrix)";
            MessageConfig failConfig = {apMsg, 0xF800, 1, "rtl", 50, 1};
            messageEngine->displayMessage(failConfig);
            
            webServer = new WebServerAPI(80, messageEngine, clockEngine);
            webServer->begin();
        }
    } else {
        Serial.println("No Wi-Fi credentials provided. Starting Access Point (AP) Mode.");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ArcadeMatrix", "12345678");
        
        String apMsg = "Offline Mode (AP: ArcadeMatrix)";
        MessageConfig failConfig = {apMsg, 0xF800, 1, "rtl", 50, 1};
        messageEngine->displayMessage(failConfig);
        
        webServer = new WebServerAPI(80, messageEngine, clockEngine);
        webServer->begin();
    }
    
    // Allow message to finish scrolling before main loop
    while (messageEngine->isActive()) {
        matrixEngine.getDisplay()->fillScreen(0);
        messageEngine->loop();
        matrixEngine.getDisplay()->flipDMABuffer();
        delay(5);
    }
    
    rotationManager->begin(config);
    
    TimeData initialTime = {10, 42, 0};
    clockEngine->updateTime(initialTime);
}

void loop() {
    // 1. Manual Power Toggle
    if (!config.standby.matrix_power) {
        delay(100);
        return; // display is already cleared by WebServerAPI
    }

    bool shouldClear = true;
    if (gifEngine.isActive()) {
        shouldClear = false; // AnimatedGIF needs previous frame in buffer
    }

    if (shouldClear) {
        matrixEngine.getDisplay()->fillScreen(0);
    }

    // Handle Idle Rotation Logic
    if (messageEngine && messageEngine->isActive()) {
        messageEngine->loop();
    } else {
        rotationManager->loop();
    }
    
    if (frontendListener) frontendListener->loop();

    // 2. Fetch Time & Handle Night Mode
    static int lastSec = -1;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        if (config.standby.night_mode_enabled) {
            int now_min = timeinfo.tm_hour * 60 + timeinfo.tm_min;
            int off_min = config.standby.turn_off_at.substring(0, 2).toInt() * 60 + config.standby.turn_off_at.substring(3).toInt();
            int wake_min = config.standby.wake_up_at.substring(0, 2).toInt() * 60 + config.standby.wake_up_at.substring(3).toInt();
            bool is_night = false;
            if (off_min > wake_min) {
                is_night = (now_min >= off_min || now_min < wake_min);
            } else {
                is_night = (now_min >= off_min && now_min < wake_min);
            }
            
            if (is_night) {
                if (config.standby.night_brightness == 0) {
                    matrixEngine.getDisplay()->fillScreen(0);
                    matrixEngine.getDisplay()->flipDMABuffer();
                    delay(1000);
                    return;
                } else {
                    matrixEngine.setBrightness(config.standby.night_brightness);
                }
            } else {
                matrixEngine.setBrightness(config.matrix.powerLimitPercent);
            }
        }
        
        if (timeinfo.tm_sec != lastSec) {
            lastSec = timeinfo.tm_sec;
            
            TimeData realTime = {(uint8_t)timeinfo.tm_hour, (uint8_t)timeinfo.tm_min, (uint8_t)timeinfo.tm_sec};
            clockEngine->updateTime(realTime);
            
            char dateBuffer[32];
            if (config.dateSettings.format == "MM/DD") {
                strftime(dateBuffer, sizeof(dateBuffer), "%m/%d", &timeinfo);
            } else if (config.dateSettings.format == "DD MMM") {
                strftime(dateBuffer, sizeof(dateBuffer), "%d %b", &timeinfo);
            } else {
                strftime(dateBuffer, sizeof(dateBuffer), "%d/%m", &timeinfo);
            }
            dateEngine->setDate(dateBuffer);
        }
    }
    
    matrixEngine.getDisplay()->flipDMABuffer();
    
    // Stable ~30 FPS frame limiter
    static unsigned long lastLoopTime = 0;
    unsigned long currentLoopTime = millis();
    if (currentLoopTime - lastLoopTime < 33) {
        delay(33 - (currentLoopTime - lastLoopTime));
    }
    lastLoopTime = millis();
}
#endif
