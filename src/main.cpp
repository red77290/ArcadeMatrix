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
#include "engines/ClockEngine.h"
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

#include "engines/RetroFrontendListener.h"
#include "engines/DateEngine.h"
#include "engines/WeatherEngine.h"
#include "engines/FighterEngine.h"
#include "engines/MarqueeEngine.h"
#include "engines/CryptoEngine.h"
#include "engines/StockEngine.h"
#include "core/RotationManager.h"
#include "core/BitmapFontLoader.h"

// Pins definition moved to HardwareProfile.h

SemaphoreHandle_t sdMutex;

ConfigLoader config;
MatrixEngine matrixEngine;
GifEngine gifEngine;

ClockEngine* clockEngine = nullptr;
DateEngine* dateEngine = nullptr;
WeatherEngine* weatherEngine = nullptr;
FighterEngine* fighterEngine = nullptr;
CryptoEngine cryptoEngine;
StockEngine stockEngine;
RotationManager* rotationManager = nullptr;
MessageEngine* messageEngine = nullptr;
MarqueeEngine* marqueeEngine = nullptr;
WebServerAPI* webServer = nullptr;
RetroFrontendListener* frontendListener = nullptr;
BitmapFontLoader customFontLoader;

#if !USE_SD_MMC
SdFs sd;
#endif

unsigned long lastTick = 0;
uint8_t currentMinute = 42;
#ifndef UNIT_TEST


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

    sdMutex = xSemaphoreCreateMutex();

    // 0. Pre-initialize Wi-Fi driver to reserve its internal RAM buffers
    // before the HUB75 matrix or other engines fragment the memory.
    WiFi.mode(WIFI_STA);

    if (psramFound()) {
        LOGI("System", "PSRAM Detected: Total Hardware = %u MB (%u bytes), Currently Free = %u bytes",
             ESP.getPsramSize() / (1024 * 1024), ESP.getPsramSize(), ESP.getFreePsram());
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
    if (!config.parseFromSD("/conf.ini")) {
        LOGW("Config", "/conf.ini not found or failed to parse. Using defaults.");
    } else {
        LOGI("Config", "Configuration loaded from /conf.ini.");
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
    gifEngine.begin(matrixEngine.getDisplay());

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
                    gifEngine.setDefaultPlaylists(paths);
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
            
            gifEngine.setDefaultPlaylists(paths);
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

    clockEngine = new ClockEngine(matrixEngine.getDisplay());
    clockEngine->setTheme(static_cast<PublisherTheme>(config.time.clock_theme));
    dateEngine = new DateEngine(matrixEngine.getDisplay());
    dateEngine->setResolution(matrixEngine.getDisplay()->width(), matrixEngine.getDisplay()->height());
    dateEngine->setTheme((PublisherTheme)config.dateSettings.theme);
    
    weatherEngine = new WeatherEngine(matrixEngine.getDisplay());
    fighterEngine = new FighterEngine(matrixEngine.getDisplay());
    fighterEngine->initialize();
    cryptoEngine.begin(matrixEngine.getDisplay());
    stockEngine.begin(matrixEngine.getDisplay());
    rotationManager = new RotationManager(clockEngine, dateEngine, weatherEngine, &gifEngine, fighterEngine, &cryptoEngine, &stockEngine);
    messageEngine = new MessageEngine(matrixEngine.getDisplay());
    // marqueeEngine allocation deferred until after webServer->begin() to prevent AsyncTCP task failure due to heap fragmentation

    // 4b. Optional SD-loadable custom bitmap font (see docs/DEVELOPER.md, tools/bdf_to_amfont)
    if (config.fonts.custom_font_path.length() > 0) {
        if (customFontLoader.loadFromSD(config.fonts.custom_font_path.c_str())) {
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
            messageEngine->loop();
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
            
            configTzTime(getPosixTimezone(config.time.timezone).c_str(), config.time.ntpServer.c_str());
            LOGI("NTP", "NTP Time Sync initiated.");
            
            LOGI("System", "Free Heap before Web Server start: %d bytes", ESP.getFreeHeap());
            webServer = new WebServerAPI(80, messageEngine, clockEngine);
            webServer->begin();
            marqueeEngine = new MarqueeEngine(matrixEngine.getDisplay(), config.matrix.width, config.matrix.height);
            webServer->setMarqueeEngine(marqueeEngine);
            MDNS.addService("http", "tcp", 80);
            
            if (config.mqtt.enabled) {
                frontendListener = new RetroFrontendListener(config.mqtt, &gifEngine, clockEngine, messageEngine);
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
            marqueeEngine = new MarqueeEngine(matrixEngine.getDisplay(), config.matrix.width, config.matrix.height);
            webServer->setMarqueeEngine(marqueeEngine);
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
        marqueeEngine = new MarqueeEngine(matrixEngine.getDisplay(), config.matrix.width, config.matrix.height);
        webServer->setMarqueeEngine(marqueeEngine);
    }
    

    // Allow message to finish scrolling before main loop
    LOGD("System", "Waiting for MessageEngine to finish...");
    unsigned long startWait = millis();
    while (messageEngine->isActive()) {
        matrixEngine.getDisplay()->fillScreen(0);
        messageEngine->loop();
        matrixEngine.getDisplay()->flipDMABuffer();
        delay(5);
        if (millis() - startWait > 5000) {
            LOGW("System", "MessageEngine wait timeout! Force stopping.");
            messageEngine->stop();
            break;
        }
    }
    LOGD("System", "MessageEngine finished.");
    
    LOGD("System", "Starting rotationManager...");
    rotationManager->begin(config);
    LOGD("System", "rotationManager started.");
    
    TimeData initialTime = {10, 42, 0};
    clockEngine->updateTime(initialTime);
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
    if (!config.standby.matrix_power) {
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

    bool shouldClear = true;
    if (gifEngine.isActive()) {
        shouldClear = false; // AnimatedGIF needs previous frame in buffer
    }

    bool shouldFlip = true;
    if (shouldClear) {
        matrixEngine.getDisplay()->fillScreen(0);
    }

    // Handle Idle Rotation Logic
    // Marquee (live box-art/frontend push) takes priority over everything else while active,
    // matching the RPi's behavior where a marquee push interrupts whatever the idle rotation
    // was showing.
    if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
        if (marqueeEngine && marqueeEngine->isActive()) {
            shouldFlip = marqueeEngine->loop();
        } else if (messageEngine && messageEngine->isActive()) {
            shouldFlip = messageEngine->loop();
        } else if (gifEngine.isActive() && rotationManager->getCurrentModule() != MODULE_GIFS) {
            shouldFlip = gifEngine.loop();
        } else if (config.mqtt.enabled) {
            if (gifEngine.isActive()) {
                shouldFlip = gifEngine.loop();
            } else {
                matrixEngine.getDisplay()->fillScreen(0);
                matrixEngine.getDisplay()->setTextSize(1);
                matrixEngine.getDisplay()->setTextColor(matrixEngine.getDisplay()->color565(128, 128, 128));
                int yPos = (config.matrix.height / 2) - 8;
                matrixEngine.getDisplay()->setCursor(4, yPos);
                matrixEngine.getDisplay()->print("Waiting for");
                matrixEngine.getDisplay()->setCursor(14, yPos + 10);
                matrixEngine.getDisplay()->print("Marquee...");
                shouldFlip = true;
            }
        } else {
            shouldFlip = rotationManager->loop();
        }
        
        if (frontendListener) frontendListener->loop();
        xSemaphoreGive(sdMutex);
    }

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
            
            static int lastDay = -1;
            if (timeinfo.tm_mday != lastDay) {
                lastDay = timeinfo.tm_mday;
                if ((int)config.dateSettings.theme == 99 || config.dateSettings.theme == THEME_NONE) {
                    PublisherTheme randomTheme = static_cast<PublisherTheme>(random(0, 20));
                    dateEngine->setTheme(randomTheme);
                    LOGI("DateEngine", "New day detected (%02d)! Selected dynamic Date theme: %d", lastDay, (int)randomTheme);
                }
            }
            
            TimeData realTime = {(uint8_t)timeinfo.tm_hour, (uint8_t)timeinfo.tm_min, (uint8_t)timeinfo.tm_sec};
            clockEngine->updateTime(realTime);
            
            static const char* fr_months_short[] = {"Janv", "Fevr", "Mars", "Avr", "Mai", "Juin", "Juil", "Aout", "Sept", "Oct", "Nov", "Dece"};
            static const char* fr_months_long[]  = {"Janvier", "Fevrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Aout", "Septembre", "Octobre", "Novembre", "Decembre"};
            static const char* fr_days_short[]   = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};
            static const char* fr_days_long[]    = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};

            static const char* es_months_short[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};
            static const char* es_months_long[]  = {"Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio", "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"};
            static const char* es_days_short[]   = {"Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"};
            static const char* es_days_long[]    = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"};

            static const char* en_months_short[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            static const char* en_months_long[]  = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
            static const char* en_days_short[]   = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            static const char* en_days_long[]    = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

            const char** m_short = en_months_short;
            const char** m_long  = en_months_long;
            const char** d_short = en_days_short;
            const char** d_long  = en_days_long;

            String lang = config.weather.lang;
            if (lang.equalsIgnoreCase("fr")) {
                m_short = fr_months_short; m_long = fr_months_long;
                d_short = fr_days_short;   d_long = fr_days_long;
            } else if (lang.equalsIgnoreCase("es")) {
                m_short = es_months_short; m_long = es_months_long;
                d_short = es_days_short;   d_long = es_days_long;
            }

            String dateRes = config.dateSettings.format;
            if (dateRes.length() == 0) dateRes = "DD/MM";

            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%04d", timeinfo.tm_year + 1900);
            dateRes.replace("YYYY", numBuf); dateRes.replace("%Y", numBuf);

            snprintf(numBuf, sizeof(numBuf), "%02d", (timeinfo.tm_year + 1900) % 100);
            dateRes.replace("YY", numBuf); dateRes.replace("%y", numBuf);

            snprintf(numBuf, sizeof(numBuf), "%02d", timeinfo.tm_mon + 1);
            dateRes.replace("MM", numBuf); dateRes.replace("%m", numBuf);

            dateRes.replace("MMMM", m_long[timeinfo.tm_mon]); dateRes.replace("%B", m_long[timeinfo.tm_mon]);
            dateRes.replace("MMM", m_short[timeinfo.tm_mon]); dateRes.replace("%b", m_short[timeinfo.tm_mon]);

            snprintf(numBuf, sizeof(numBuf), "%02d", timeinfo.tm_mday);
            dateRes.replace("DD", numBuf); dateRes.replace("%d", numBuf);

            dateRes.replace("DDDD", d_long[timeinfo.tm_wday]); dateRes.replace("%A", d_long[timeinfo.tm_wday]);
            dateRes.replace("DDD", d_short[timeinfo.tm_wday]); dateRes.replace("%a", d_short[timeinfo.tm_wday]);

            dateEngine->setDateData({(uint8_t)timeinfo.tm_mday, (uint8_t)(timeinfo.tm_mon + 1), (uint8_t)((timeinfo.tm_year + 1900) % 100)});
            dateEngine->setDate(dateRes.c_str());
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
