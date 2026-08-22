#include "core/AppEngineContext.h"
#include <FS.h>
#include <Arduino.h>
#include <SPI.h>
#include "core/SDUtils.h"
#include "HardwareProfile.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
#include "core/ConfigLoader.h"
#include "core/MatrixEngine.h"
#include "engines/GifEngine.h"
#include "engines/clocks/ArcadeClock.h"
#include "engines/MessageEngine.h"
#include "api/WebServerAPI.h"
#include "core/Logger.h"
#include <time.h>
#include "HardwareProfile.h"
#if defined(USE_RTC) && USE_RTC
#include "../src/core/RTCUtils.h"
#include "esp_sntp.h"

void time_sync_notification_cb(struct timeval *tv) {
    struct tm timeinfo;
    gmtime_r(&tv->tv_sec, &timeinfo);
    writeRTC(timeinfo);
    Serial.println("RTC updated with UTC time from NTP");
}
#endif

#include "engines/FrontendSyncEngine.h"
#include "engines/DateEngine.h"
#include "engines/FighterEngine.h"
#include "engines/MarqueeEngine.h"
#include "engines/CryptoEngine.h"
#include "engines/StockEngine.h"
#include "core/RotationManager.h"
#include "core/BitmapFontLoader.h"
#include "core/ConfigSanitizer.h"
#include "api/CoinGeckoProvider.h"
#include "api/BinanceProvider.h"
#include "api/OpenWeatherMapProvider.h"
#include "api/YahooFinanceProvider.h"
#include "hal/HardwareHAL.h"
#include "engines/TempEngine.h"
#include "engines/VisualizerEngine.h"
#include "../include/core/EngineRegistry.h"
#include "engines/DecibelEngine.h"
#include "engines/EngineRegistrar.h"

#include "core/DisplayArbiter.h"

// Pins definition moved to HardwareProfile.h

SemaphoreHandle_t sdMutex;

ConfigLoader config;
MatrixEngine matrixEngine;
GifEngine* gifEngine = nullptr;

VisualizerEngine* visualizerEngine = nullptr;
RotationManager* rotationManager = nullptr;
MessageEngine* messageEngine = nullptr;
MarqueeEngine* marqueeEngine = nullptr;
WebServerAPI* webServer = nullptr;
FrontendSyncEngine* frontendListener = nullptr;
AppEngineContext* appCtx = nullptr;
BitmapFontLoader customFontLoader;
DisplayArbiter displayArbiter;

#if !USE_SD_MMC
SdFs sd;
#endif

unsigned long lastTick = 0;
uint8_t currentMinute = 42;
#ifndef PIO_UNIT_TESTING


String getPosixTimezone(String tz) {
    if (tz == "Europe/Paris" || tz == "Europe/Berlin" || tz == "Europe/Madrid" || tz == "Europe/Rome") return "CET-1CEST,M3.5.0,M10.5.0/3";
    if (tz == "Europe/London") return "GMT0BST,M3.5.0/1,M10.5.0";
    if (tz == "America/New_York") return "EST5EDT,M3.2.0,M11.1.0";
    if (tz == "America/Chicago") return "CST6CDT,M3.2.0,M11.1.0";
    if (tz == "America/Denver") return "MST7MDT,M3.2.0,M11.1.0";
    if (tz == "America/Los_Angeles") return "PST8PDT,M3.2.0,M11.1.0";
    if (tz == "Asia/Tokyo") return "JST-9";
    if (tz == "Asia/Shanghai") return "CST-8";
    if (tz == "Australia/Sydney") return "AEST-10AEDT,M10.1.0,M4.1.0/3";
    return tz; // Fallback to raw string (which could be a POSIX string)
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // 1. Initialize HAL first so auto-detection can be used by the Registrar
    hardwareHAL.begin();
    
    // Register all dynamic engines & wrappers (uses HAL info)
    EngineRegistrar::registerAll();
    
    
    randomSeed(esp_random());
    LOGI("System", "Starting ArcadeMatrix Firmware...");

    // Hardware watchdog: if setup()/loop() ever hangs (SD/matrix init failure, WiFi driver
    // lockup, an unexpected infinite loop, ...) for more than WDT_TIMEOUT_S seconds without
    // being reset, the ESP32 reboots itself instead of staying bricked until someone power
    // cycles it. Intentionally NOT fed inside the two `while(1)` critical-failure loops below,
    // so those still trigger a watchdog reboot (retry loop) rather than running forever silently.
    constexpr uint32_t WDT_TIMEOUT_S = 30;
    esp_task_wdt_init(WDT_TIMEOUT_S, true /* panic and reboot on timeout */);

#if defined(USE_RTC) && USE_RTC
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (initRTC()) {
        struct tm timeinfo;
        if (readRTC(timeinfo)) {
            // Early in setup, TZ is not set, so mktime assumes UTC (which is what we stored)
            struct timeval tv;
            
            // To be strictly safe, unset TZ during mktime just in case
            char* oldTZ = getenv("TZ");
            setenv("TZ", "", 1);
            tzset();
            tv.tv_sec = mktime(&timeinfo);
            if (oldTZ) setenv("TZ", oldTZ, 1);
            else unsetenv("TZ");
            tzset();
            
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);
            Serial.println("System time set from Hardware RTC (UTC)");
        } else {
            Serial.println("Failed to read Hardware RTC");
        }
    } else {
        Serial.println("Failed to init Hardware RTC");
    }
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#endif

    esp_task_wdt_add(NULL);

    // Initialize Hardware Abstraction Layer & Sensors
    hardwareHAL.begin();

    sdMutex = xSemaphoreCreateMutex();

    // 0. Pre-initialize Wi-Fi driver to reserve its internal RAM buffers
    // before the HUB75 matrix or other engines fragment the memory.
    WiFi.mode(WIFI_STA);

    if (hardwareHAL.capabilities().hasPsram) {
        LOGI("System", "PSRAM Detected: Total Hardware = %u MB (%u bytes), Currently Free = %u bytes",
             hardwareHAL.capabilities().psramBytes / (1024 * 1024), hardwareHAL.capabilities().psramBytes, ESP.getFreePsram());
    } else {
        LOGI("System", "No PSRAM detected on hardware.");
    }

    // 1. Initialize SD Card
#if USE_SD_MMC
    if (!SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)) {
        Serial.println("CRITICAL ERROR: SD_MMC setPins Failed! Rebooting...");
        while (1) { delay(100); }
    }
    if (!sd.begin("/sdcard", true)) { // true = 1-bit mode
        Serial.println("CRITICAL ERROR: SD_MMC Mount Failed! Rebooting via watchdog...");
        while (1) { delay(100); }
    }
#else
    SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, SD_CS_PIN);
    SdSpiConfig spiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(25), &SPI);
    if (!sd.begin(spiConfig)) {
        Serial.println("CRITICAL ERROR: SD Card Mount Failed! Rebooting via watchdog...");
        while (1) { delay(100); }
    }
#endif
    LOGI("SD", "SD Card mounted successfully.");

    // 2. Load Configuration from SD
    if (!config.loadFromSD("/config.json")) {
        LOGW("Config", "/config.json not found or failed to parse. Using defaults.");
    } else {
        LOGI("Config", "Configuration loaded from /config.json.");
    }

    SanitizeResult sanitizeRes = ConfigSanitizer::sanitizeInstances(config, engineRegistry);
    if (sanitizeRes.modified) {
        config.saveToSD("/config.json");
    }

    // 3. Initialize Matrix
    LOGI("Matrix", "Matrix Config: %dx%d, Chain: %d", config.matrix.width, config.matrix.height, config.matrix.chainLength);
    if (!matrixEngine.begin(config.matrix)) {
        LOGE("Matrix", "CRITICAL ERROR: Matrix init failed!");
        while (1) { delay(100); }
    }
    matrixEngine.setBrightness(config.matrix.powerLimitPercent);
    LOGI("System", "Free Heap after Matrix init: %d bytes", ESP.getFreeHeap());

    // 4. Initialize Engines
    // GifEngine initialization deferred to EngineRegistry

    // Load saved GIF playlists from SD, fallback to all available playlists
    {
        bool selectedLoaded = false;
        FsFile playlistFile;
        if (sd.exists("/playlists_selected.json")) {
            playlistFile = sd.open("/playlists_selected.json", FILE_OPEN_READ);
        }
        if (playlistFile) {
            DynamicJsonDocument doc(4096);
            DeserializationError error = deserializeJson(doc, playlistFile);
            playlistFile.close();
            if (!error) {
                JsonArray arr = doc["playlists"].as<JsonArray>();
                std::vector<String> paths;
                for (JsonVariant v : arr) paths.push_back(v.as<String>());
                if (!paths.empty()) {
                if (gifEngine) gifEngine->setDefaultPlaylists(paths);
                    selectedLoaded = true;
                    LOGI("GIF", "Loaded %d GIF playlists from playlists_selected.json", paths.size());
                }
            }
        }
        
        if (!selectedLoaded) {
            std::vector<String> paths;
            FsFile master;
            if (sd.exists("/gifs/playlists.json")) {
                master = sd.open("/gifs/playlists.json", FILE_OPEN_READ);
            }
            if (master) {
                DynamicJsonDocument masterDoc(16384);
                if (!deserializeJson(masterDoc, master)) {
                    JsonObject root = masterDoc.as<JsonObject>();
                    for (JsonPair kv : root) {
                        paths.push_back(kv.value()["path"].as<String>());
                    }
                }
                master.close();
            }
            if (paths.empty()) {
                paths.push_back("/gifs");
            }
            
            if (gifEngine) gifEngine->setDefaultPlaylists(paths);
            LOGI("GIF", "Defaulted to %d GIF playlists.", paths.size());

            // Save default selection to /playlists_selected.json so it persists immediately
            if (sd.exists("/playlists_selected.json")) sd.remove("/playlists_selected.json");
            FsFile f = sd.open("/playlists_selected.json", FILE_OPEN_WRITE);
            if (f) {
                DynamicJsonDocument saveDoc(1024);
                JsonArray arr = saveDoc["playlists"].to<JsonArray>();
                for (const String& p : paths) arr.add(p);
                serializeJson(saveDoc, f);
                f.close();
                LOGI("GIF", "Created initial playlists_selected.json on SD with all playlists.");
            }
        }
    }
    
    // CryptoEngine is now created by IEngine factory.
    
    // StockEngine is now created by IEngine factory.
    
    // TempEngine is now created by IEngine factory.
    // DecibelEngine is now created by IEngine factory.
    rotationManager = new RotationManager();
    appCtx = new AppEngineContext(matrixEngine.getDisplay(), frontendListener);
    rotationManager->setEngineContext(appCtx);
    
    auto desc = EngineRegistry::getDescriptor("visualizer");
    if (desc && desc->factory) {
        auto visPtr = desc->factory();
        visualizerEngine = static_cast<VisualizerEngine*>(visPtr.release());
        visualizerEngine->initialize(appCtx, nullptr);
    }

    auto msgDesc = EngineRegistry::getDescriptor("message");
    if (msgDesc && msgDesc->factory) {
        auto msgPtr = msgDesc->factory();
        messageEngine = static_cast<MessageEngine*>(msgPtr.release());
        messageEngine->initialize(appCtx, nullptr);
    }

    auto gifDesc = EngineRegistry::getDescriptor("gifs");
    if (gifDesc && gifDesc->factory) {
        auto gifPtr = gifDesc->factory();
        gifEngine = static_cast<GifEngine*>(gifPtr.release());
        gifEngine->initialize(appCtx, nullptr);
    }
    // marqueeEngine allocation deferred until after webServer->begin() to prevent AsyncTCP task failure due to heap fragmentation

    // 4b. Optional SD-loadable custom bitmap font (see docs/DEVELOPER.md, tools/bdf_to_amfont)
    String fontPath = config.getInstance("clock_main") ? config.getInstance("clock_main")->config.getString("clock_font_path") : "";
    if (fontPath.length() > 0) {
        if (customFontLoader.loadFromSD(fontPath.c_str())) {
            messageEngine->setCustomFont(customFontLoader.getFont());
            Serial.println("Custom font applied to MessageEngine.");
        } else {
            Serial.println("Warning: custom_font_path set but failed to load; using default font.");
        }
    }

    // 5. Connect to Wi-Fi
    if (config.wifi.ssid.length() > 0) {
        Serial.printf("Connecting to Wi-Fi: %s\n", config.wifi.ssid.c_str());
        
        MessageConfig connMsg = {"Connecting to Wi-Fi...", 0xFFFF, 1, "rtl", 50, 10};
        messageEngine->displayMessage(connMsg);

        // Auto-reconnect on drop: without this, the ESP32 Wi-Fi driver's default modem-sleep
        // power-save mode is known to cause exactly this symptom on some routers/APs - the
        // device connects, briefly does one or two requests (e.g. NTP sync), then silently goes
        // to sleep/drops off and is never seen again (router shows it gone, Web UI unreachable),
        // even though nothing in our own code disconnected it.
        WiFi.onEvent([](WiFiEvent_t event) {
            if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
                Serial.println("Wi-Fi disconnected - attempting to reconnect...");
                WiFi.reconnect();
            }
        });

        WiFi.mode(WIFI_STA);
        WiFi.setHostname(config.wifi.hostname.c_str());
        WiFi.begin(config.wifi.ssid.c_str(), config.wifi.password.c_str());
        // Disable Wi-Fi modem sleep: this is the actual root cause of the "connects just long
        // enough to sync NTP time, then vanishes from the router / Web UI becomes unreachable"
        // symptom on many routers/APs. Must be called after WiFi.begin() starts the driver.
        WiFi.setSleep(false);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            matrixEngine.getDisplay()->fillScreen(0);
            messageEngine->update(appCtx);
            messageEngine->render(appCtx);
            matrixEngine.getDisplay()->flipDMABuffer();
            attempts++;
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            LOGI("WiFi", "Wi-Fi Connected! IP Address: %s", WiFi.localIP().toString().c_str());
            
            String ipMsg = "IP: " + WiFi.localIP().toString();
            MessageConfig ipConfig = {ipMsg, 0x07E0, 1, "rtl", 50, 5};
            messageEngine->displayMessage(ipConfig);
            
            if (MDNS.begin(config.wifi.hostname.c_str())) {
                LOGI("WiFi", "mDNS responder started: http://%s.local", config.wifi.hostname.c_str());
            }
            
            configTzTime(getPosixTimezone(config.system.timezone).c_str(), "pool.ntp.org");
            LOGI("NTP", "NTP Time Sync initiated.");
            
            LOGI("System", "Free Heap before Web Server start: %d bytes", ESP.getFreeHeap());
            webServer = new WebServerAPI(80, messageEngine);
            webServer->begin();
            webServer->setVisualizerEngine(visualizerEngine);
            auto msgDesc = EngineRegistry::getDescriptor("marquee");
            if (msgDesc && msgDesc->factory) {
                auto msgPtr = msgDesc->factory();
                marqueeEngine = static_cast<MarqueeEngine*>(msgPtr.release());
                marqueeEngine->initialize(appCtx, nullptr);
            }
            webServer->setMarqueeEngine(marqueeEngine);
            MDNS.addService("http", "tcp", 80);
            
            if (config.mqtt.enabled) {
                frontendListener = new FrontendSyncEngine(config.mqtt, gifEngine, messageEngine);
                frontendListener->begin();
            }
        } else {
            Serial.println("Wi-Fi connection failed. Starting Access Point (AP) Mode.");
            WiFi.mode(WIFI_AP);
            WiFi.softAP("ArcadeMatrix", "12345678");
            
            String apMsg = "Offline Mode (AP: ArcadeMatrix)";
            MessageConfig failConfig = {apMsg, 0xF800, 1, "rtl", 50, 1};
            messageEngine->displayMessage(failConfig);
            
            webServer = new WebServerAPI(80, messageEngine);
            webServer->begin();
            auto msgDesc = EngineRegistry::getDescriptor("marquee");
            if (msgDesc && msgDesc->factory) {
                auto msgPtr = msgDesc->factory();
                marqueeEngine = static_cast<MarqueeEngine*>(msgPtr.release());
                marqueeEngine->initialize(appCtx, nullptr);
            }
            webServer->setMarqueeEngine(marqueeEngine);
        }
    } else {
        Serial.println("No Wi-Fi credentials provided. Starting Access Point (AP) Mode.");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ArcadeMatrix", "12345678");
        
        String apMsg = "Offline Mode (AP: ArcadeMatrix)";
        MessageConfig failConfig = {apMsg, 0xF800, 1, "rtl", 50, 1};
        messageEngine->displayMessage(failConfig);
        
        webServer = new WebServerAPI(80, messageEngine);
        webServer->begin();
        auto msgDesc = EngineRegistry::getDescriptor("marquee");
        if (msgDesc && msgDesc->factory) {
            auto msgPtr = msgDesc->factory();
            marqueeEngine = static_cast<MarqueeEngine*>(msgPtr.release());
            marqueeEngine->initialize(appCtx, nullptr);
        }
        webServer->setMarqueeEngine(marqueeEngine);
    }
    

    // Allow message to finish scrolling before main loop
    LOGD("System", "Waiting for MessageEngine to finish...");
    unsigned long startWait = millis();
    while (messageEngine->isActive()) {
        matrixEngine.getDisplay()->fillScreen(0);
        messageEngine->update(appCtx);
        messageEngine->render(appCtx);
        matrixEngine.getDisplay()->flipDMABuffer();
        delay(5);
        if (millis() - startWait > 5000) {
            LOGW("System", "MessageEngine wait timeout! Force stopping.");
            messageEngine->deactivate();
            break;
        }
    }
    LOGD("System", "MessageEngine finished.");
    
    LOGD("System", "Starting rotationManager...");
    rotationManager->begin(config);
    LOGD("System", "rotationManager started.");
    
    TimeData initialTime = {10, 42, 0};
    LOGI("System", "Setup complete. Entering loop().");
}


void loop() {
    esp_task_wdt_reset();

    static bool firstLoop = true;
    if (firstLoop) {
        LOGD("System", "Entered first loop() iteration!");
        firstLoop = false;
    }

    static bool wasPoweredOn = true;

    // 1. Manual Power Toggle
    if (!config.matrix.matrix_power) {
        if (wasPoweredOn) {
            matrixEngine.getDisplay()->fillScreen(0);
            matrixEngine.getDisplay()->flipDMABuffer();
            matrixEngine.getDisplay()->fillScreen(0);
            matrixEngine.getDisplay()->flipDMABuffer();
            wasPoweredOn = false;
        }
        delay(100);
        return;
    }
    wasPoweredOn = true;

    // Handle incoming MQTT events before evaluating display logic
    // We do NOT wrap this in sdMutex because frontendListener handles SD access and network
    // operations asynchronously, and takes sdMutex internally only when needed!
    if (frontendListener) {
        frontendListener->loop();
    }

    bool shouldClear = true;
    if (gifEngine && gifEngine->isActive()) {
        shouldClear = false; // AnimatedGIF needs previous frame in buffer
    }

    bool shouldFlip = true;
    if (shouldClear) {
        matrixEngine.getDisplay()->fillScreen(0);
    }

    // Handle Idle Rotation Logic & Priority Display Overrides
    if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
        if (rotationManager) {
            rotationManager->setSuspended(config.mqtt.enabled);
        }

        // Synchronize Music Visualizer active state with config setting
        if (visualizerEngine) {
            if ((config.getInstance("visualizer_main") && config.getInstance("visualizer_main")->config.getBool("enabled")) && !visualizerEngine->isActive()) {
                visualizerEngine->activate();
                DisplayRequest req;
                req.source = "VISUALIZER";
                req.priority = DisplayPriority::VISUALIZER;
                req.lifecycle = RequestLifecycle::UNTIL_CANCELLED;
                req.preemptive = true;
                req.timeout_ms = 0;
                displayArbiter.submitRequest(req);
            } else if (!(config.getInstance("visualizer_main") && config.getInstance("visualizer_main")->config.getBool("enabled")) && visualizerEngine->isActive()) {
                visualizerEngine->deactivate();
                displayArbiter.cancelRequest("VISUALIZER");
            }
        }

        displayArbiter.clearExpired();
        
        if (marqueeEngine && marqueeEngine->isActive()) {
            DisplayRequest req{"MARQUEE", DisplayPriority::MARQUEE, RequestLifecycle::ONE_SHOT, true, "", 0, millis()};
            displayArbiter.submitRequest(req);
        }
        if (messageEngine && messageEngine->isActive()) {
            DisplayRequest req{"MESSAGE", DisplayPriority::MQTT, RequestLifecycle::ONE_SHOT, true, "", 0, millis()};
            displayArbiter.submitRequest(req);
        } else if (gifEngine && gifEngine->isActive() && rotationManager->getCurrentInstanceId() != "gifs") {
            DisplayRequest req{"GIF", DisplayPriority::GIF, RequestLifecycle::ONE_SHOT, true, "", 0, millis()};
            displayArbiter.submitRequest(req);
        }

        DisplayRequest winner = displayArbiter.evaluate();
        
        if (winner.source == "VISUALIZER") {
            visualizerEngine->update(appCtx);
            visualizerEngine->render(appCtx);
            shouldFlip = true;
        } else if (winner.source == "MARQUEE") {
            marqueeEngine->update(appCtx);
            marqueeEngine->render(appCtx);
            shouldFlip = true;
        } else if (winner.source == "MESSAGE") {
            messageEngine->update(appCtx);
            messageEngine->render(appCtx);
            shouldFlip = true;
        } else if (winner.source == "GIF") {
            if (gifEngine) {
                gifEngine->update(appCtx);
                gifEngine->render(appCtx);
                shouldFlip = true;
            }
        } else {
            shouldFlip = rotationManager->loop();
        }
        
        xSemaphoreGive(sdMutex);
    }

    // 2. Fetch Time & Handle Night Mode
    static int lastSec = -1;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        bool is_night = false;
        if (config.system.night_mode_enabled) {
            int now_min = timeinfo.tm_hour * 60 + timeinfo.tm_min;
            int off_min = config.system.turn_off_at.substring(0, 2).toInt() * 60 + config.system.turn_off_at.substring(3).toInt();
            int wake_min = config.system.wake_up_at.substring(0, 2).toInt() * 60 + config.system.wake_up_at.substring(3).toInt();
            if (off_min > wake_min) {
                is_night = (now_min >= off_min || now_min < wake_min);
            } else {
                is_night = (now_min >= off_min && now_min < wake_min);
            }
        }
        
        if (is_night) {
            if (config.system.night_brightness == 0) {
                matrixEngine.getDisplay()->fillScreen(0);
                matrixEngine.getDisplay()->flipDMABuffer();
                delay(1000);
                return;
            } else {
                matrixEngine.setBrightness(config.system.night_brightness);
            }
        } else {
            matrixEngine.setBrightness(config.matrix.powerLimitPercent);
        }
        
        if (timeinfo.tm_sec != lastSec) {
            lastSec = timeinfo.tm_sec;
            
            static int lastDay = -1;
            if (timeinfo.tm_mday != lastDay) {
                lastDay = timeinfo.tm_mday;
            }
        }
    }
    
    if (shouldFlip) {
        static bool firstFlip = true;
        if (firstFlip) {
            LOGD("System", "Calling flipDMABuffer()...");
        }
        matrixEngine.getDisplay()->flipDMABuffer();
        if (firstFlip) {
            LOGD("System", "flipDMABuffer() initialized successfully!");
            firstFlip = false;
        }
    }

    
    // Stable ~60 FPS frame limiter
    static unsigned long lastLoopTime = 0;
    unsigned long currentLoopTime = millis();
    if (currentLoopTime - lastLoopTime < 16) {
        delay(16 - (currentLoopTime - lastLoopTime));
    }
    lastLoopTime = millis();
}
#endif
