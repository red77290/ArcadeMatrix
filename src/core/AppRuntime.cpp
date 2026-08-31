#include "AppRuntime.h"
#include <FS.h>
#include <SPI.h>
#include "SDUtils.h"
#include "../include/HardwareProfile.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
#include "Logger.h"
#include <time.h>
#if defined(USE_RTC) && USE_RTC
#include "RTCUtils.h"
#include "esp_sntp.h"

static void time_sync_notification_cb(struct timeval *tv) {
    struct tm timeinfo;
    gmtime_r(&tv->tv_sec, &timeinfo);
    writeRTC(timeinfo);
    Serial.println("RTC updated with UTC time from NTP");
}
#endif

#include "../include/core/EngineRegistry.h"
#include "../engines/EngineRegistrar.h"
#include "ConfigSanitizer.h"
#include "../hal/HardwareHAL.h"
#include "../hal/GyroHAL.h"
#include "AudioHub.h"

ConfigLoader config;
SemaphoreHandle_t sdMutex = nullptr;
std::mutex configMutex;
#if !USE_SD_MMC
SdFs sd;
#endif
MatrixEngine matrixEngine;
OverlayManager overlayManager;
RotationManager* rotationManager = nullptr;
VisualizerEngine* visualizerEngine = nullptr;
GifEngine* gifEngine = nullptr;
AppRuntime app;

ConfigLoader& AppRuntime::getConfig() {
    return config;
}

String getPosixTimezone(String tz) {
    if (tz == "Europe/Paris" || tz == "Europe/Berlin" || tz == "Europe/Madrid" || tz == "Europe/Rome" || tz == "Europe/Brussels" || tz == "Europe/Amsterdam" || tz == "Europe/Vienna" || tz == "Europe/Zurich" || tz == "Europe/Warsaw" || tz == "Europe/Prague" || tz == "Europe/Stockholm" || tz == "Europe/Oslo" || tz == "Europe/Copenhagen") return "CET-1CEST,M3.5.0,M10.5.0/3";
    if (tz == "Europe/London" || tz == "Europe/Lisbon" || tz == "Atlantic/Canary") return "GMT0BST,M3.5.0/1,M10.5.0";
    if (tz == "Europe/Dublin") return "GMT0IST,M3.5.0/1,M10.5.0";
    if (tz == "Europe/Athens" || tz == "Europe/Helsinki" || tz == "Europe/Bucharest" || tz == "Europe/Kyiv" || tz == "Europe/Sofia" || tz == "Europe/Tallinn" || tz == "Europe/Riga" || tz == "Europe/Vilnius") return "EET-2EEST,M3.5.0/3,M10.5.0/4";
    if (tz == "Europe/Moscow") return "MSK-3";
    if (tz == "Europe/Istanbul") return "TRT-3";
    if (tz == "Atlantic/Reykjavik") return "GMT0";
    if (tz == "Atlantic/Azores") return "AZOT1AZOST,M3.5.0/0,M10.5.0/1";
    if (tz == "America/New_York" || tz == "America/Montreal" || tz == "America/Toronto" || tz == "America/Detroit" || tz == "America/Indiana/Indianapolis") return "EST5EDT,M3.2.0,M11.1.0";
    if (tz == "America/Chicago" || tz == "America/Winnipeg") return "CST6CDT,M3.2.0,M11.1.0";
    if (tz == "America/Mexico_City") return "CST6";
    if (tz == "America/Phoenix") return "MST7";
    if (tz == "America/Denver" || tz == "America/Boise" || tz == "America/Edmonton") return "MST7MDT,M3.2.0,M11.1.0";
    if (tz == "America/Los_Angeles" || tz == "America/Vancouver" || tz == "America/Tijuana") return "PST8PDT,M3.2.0,M11.1.0";
    if (tz == "America/Anchorage") return "AKST9AKDT,M3.2.0,M11.1.0";
    if (tz == "America/Halifax") return "AST4ADT,M3.2.0,M11.1.0";
    if (tz == "America/St_Johns") return "NST3:30NDT,M3.2.0,M11.1.0";
    if (tz == "Pacific/Honolulu") return "HST10";
    if (tz == "America/Sao_Paulo") return "BRT3";
    if (tz == "America/Buenos_Aires") return "ART3";
    if (tz == "America/Bogota") return "COT5";
    if (tz == "America/Lima") return "PET5";
    if (tz == "America/Santiago") return "CLT4CLST,M9.1.6/24,M4.1.6/24";
    if (tz == "Africa/Cairo") return "EET-2EEST,M4.5.5/0,M10.5.5/24";
    if (tz == "Africa/Johannesburg") return "SAST-2";
    if (tz == "Africa/Casablanca") return "WEST-1";
    if (tz == "Africa/Nairobi") return "EAT-3";
    if (tz == "Africa/Lagos") return "WAT-1";
    if (tz == "Asia/Jerusalem") return "IST-2IDT,M3.4.4/26,M10.5.0";
    if (tz == "Asia/Riyadh") return "AST-3";
    if (tz == "Asia/Dubai") return "GST-4";
    if (tz == "Asia/Tehran") return "IRST-3:30";
    if (tz == "Asia/Karachi") return "PKT-5";
    if (tz == "Asia/Kolkata") return "IST-5:30";
    if (tz == "Asia/Dhaka") return "BST-6";
    if (tz == "Asia/Bangkok" || tz == "Asia/Jakarta") return "ICT-7";
    if (tz == "Asia/Singapore" || tz == "Asia/Hong_Kong" || tz == "Asia/Shanghai" || tz == "Asia/Taipei" || tz == "Asia/Manila") return "CST-8";
    if (tz == "Asia/Tokyo") return "JST-9";
    if (tz == "Asia/Seoul") return "KST-9";
    if (tz == "Australia/Sydney" || tz == "Australia/Melbourne") return "AEST-10AEDT,M10.1.0,M4.1.0/3";
    if (tz == "Australia/Brisbane") return "AEST-10";
    if (tz == "Australia/Adelaide") return "ACST-9:30ACDT,M10.1.0,M4.1.0/3";
    if (tz == "Australia/Perth") return "AWST-8";
    if (tz == "Pacific/Guam") return "ChST-10";
    if (tz == "Pacific/Auckland") return "NZST-12NZDT,M9.5.0,M4.1.0/3";
    if (tz == "Pacific/Fiji") return "FJT-12";
    if (tz == "UTC" || tz == "GMT") return "UTC0";
    return tz;
}

AppRuntime::AppRuntime() {}

AppRuntime::~AppRuntime() {
    delete rotationManager;
    delete m_appCtx;
    delete visualizerEngine;
    delete m_messageEngine;
    delete m_marqueeEngine;
    delete gifEngine;
    delete m_webServer;
    delete m_frontendListener;
}

void AppRuntime::initialize() {
    Serial.begin(115200);
    delay(1000);
    
    // 1. Initialize HAL first so auto-detection can be used by Registrar
    hardwareHAL.begin();
    EngineRegistrar::registerAll();
    
    randomSeed(esp_random());
    LOGI("System", "Starting ArcadeMatrix Firmware...");

    constexpr uint32_t WDT_TIMEOUT_S = 30;
    esp_task_wdt_init(WDT_TIMEOUT_S, true);

#if defined(USE_RTC) && USE_RTC
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (initRTC()) {
        struct tm timeinfo;
        if (readRTC(timeinfo)) {
            struct timeval tv;
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

    WiFi.mode(WIFI_STA);

    if (hardwareHAL.capabilities().hasPsram) {
        LOGI("System", "PSRAM Detected: Total Hardware = %u MB (%u bytes), Currently Free = %u bytes",
             hardwareHAL.capabilities().psramBytes / (1024 * 1024), hardwareHAL.capabilities().psramBytes, ESP.getFreePsram());
    } else {
        LOGI("System", "No PSRAM detected on hardware.");
    }

    // Initialize SD Card
#if USE_SD_MMC
    if (!SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)) {
        Serial.println("CRITICAL ERROR: SD_MMC setPins Failed! Rebooting...");
        while (1) { delay(100); }
    }
    if (!sd.begin("/sdcard", true)) {
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

    if (!config.loadFromSD("/config.json")) {
        LOGW("Config", "/config.json not found or failed to parse. Using defaults.");
    } else {
        LOGI("Config", "Configuration loaded from /config.json.");
    }

    ConfigSanitizer::sanitize(config);

    ConfigSnapshot snapshot = config.getSnapshot();

    LOGI("Matrix", "Matrix Config: %dx%d, Chain: %d", snapshot.matrix.width, snapshot.matrix.height, snapshot.matrix.chainLength);
    if (!matrixEngine.begin(snapshot.matrix)) {
        LOGE("Matrix", "CRITICAL ERROR: Matrix init failed!");
        while (1) { delay(100); }
    }
    matrixEngine.setBrightness(snapshot.matrix.powerLimitPercent);
    LOGI("System", "Free Heap after Matrix init: %d bytes", ESP.getFreeHeap());

    gyroHAL.begin();
    displayOrientationManager.begin(matrixEngine.getDisplay());
    displayOrientationManager.setRotationOffset(snapshot.matrix.rotation_offset);
    displayOrientationManager.setTransitionEffect(snapshot.matrix.rotation_transition);
    displayOrientationManager.setTransitionDuration(snapshot.matrix.rotation_transition_duration_ms);

    uint8_t initialRotation = (snapshot.matrix.auto_rotate && gyroHAL.isAvailable()) 
        ? (gyroHAL.getOrientation().suggestedRotation + snapshot.matrix.rotation_offset) % 4
        : (snapshot.matrix.rotation_offset % 4);
    displayOrientationManager.setRotation(initialRotation, false);

    audioHub.begin();

    rotationManager = new RotationManager();
    m_appCtx = new AppEngineContext(matrixEngine.getDisplay(), m_frontendListener);
    rotationManager->setEngineContext(m_appCtx);
    overlayManager.initialize(m_appCtx, &config);

    m_displayRuntime.begin(m_appCtx, &matrixEngine, rotationManager,
                           &overlayManager, &displayOrientationManager, &m_displayArbiter);

    auto desc = EngineRegistry::getDescriptor("audiovisualizer");
    if (desc && desc->factory) {
        auto visPtr = desc->factory();
        visualizerEngine = static_cast<VisualizerEngine*>(visPtr.release());
        visualizerEngine->initialize(m_appCtx, nullptr);
        m_displayRuntime.registerSourceEngine(DisplaySourceId::VISUALIZER, visualizerEngine);
    }

    auto msgDesc = EngineRegistry::getDescriptor("message");
    if (msgDesc && msgDesc->factory) {
        auto msgPtr = msgDesc->factory();
        m_messageEngine = static_cast<MessageEngine*>(msgPtr.release());
        m_messageEngine->initialize(m_appCtx, nullptr);
        m_displayRuntime.registerSourceEngine(DisplaySourceId::MQTT, m_messageEngine);
    }

    auto gifDesc = EngineRegistry::getDescriptor("gifs");
    if (gifDesc && gifDesc->factory) {
        auto gifPtr = gifDesc->factory();
        gifEngine = static_cast<GifEngine*>(gifPtr.release());
        gifEngine->initialize(m_appCtx, nullptr);
        m_displayRuntime.registerSourceEngine(DisplaySourceId::GIF, gifEngine);
    }

    const auto* clockInst = snapshot.getInstance("clock_main");
    String fontPath = clockInst ? clockInst->config.getString("clock_font_path") : "";
    if (fontPath.length() > 0) {
        if (m_customFontLoader.loadFromSD(fontPath.c_str())) {
            m_messageEngine->setCustomFont(m_customFontLoader.getFont());
            Serial.println("Custom font applied to MessageEngine.");
        } else {
            Serial.println("Warning: custom_font_path set but failed to load; using default font.");
        }
    }

    // Connect to Wi-Fi
    if (snapshot.wifi.ssid.length() > 0) {
        Serial.printf("Connecting to Wi-Fi: %s\n", snapshot.wifi.ssid.c_str());
        MessageConfig connMsg = {"Connecting to Wi-Fi...", 0xFFFF, 1, "rtl", 50, 10};
        m_messageEngine->displayMessage(connMsg);

        WiFi.onEvent([](WiFiEvent_t event) {
            if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
                Serial.println("Wi-Fi disconnected - attempting to reconnect...");
                WiFi.reconnect();
            }
        });

        WiFi.mode(WIFI_STA);
        WiFi.setHostname(snapshot.wifi.hostname.c_str());
        WiFi.begin(snapshot.wifi.ssid.c_str(), snapshot.wifi.password.c_str());
        WiFi.setSleep(false);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            matrixEngine.getDisplay()->fillScreen(0);
            m_messageEngine->update(m_appCtx);
            m_messageEngine->render(m_appCtx);
            matrixEngine.getDisplay()->flipDMABuffer();
            attempts++;
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            LOGI("WiFi", "Wi-Fi Connected! IP Address: %s", WiFi.localIP().toString().c_str());
            String ipMsg = "IP: " + WiFi.localIP().toString();
            MessageConfig ipConfig = {ipMsg, 0x07E0, 1, "rtl", 50, 5};
            m_messageEngine->displayMessage(ipConfig);
            
            if (MDNS.begin(snapshot.wifi.hostname.c_str())) {
                LOGI("WiFi", "mDNS responder started: http://%s.local", snapshot.wifi.hostname.c_str());
            }
            configTzTime(getPosixTimezone(snapshot.system.timezone).c_str(), "pool.ntp.org");
            
            m_webServer = new WebServerAPI(80, m_messageEngine);
            m_webServer->begin();
            m_webServer->setVisualizerEngine(visualizerEngine);

            auto marqueeDesc = EngineRegistry::getDescriptor("marquee");
            if (marqueeDesc && marqueeDesc->factory) {
                auto marqPtr = marqueeDesc->factory();
                m_marqueeEngine = static_cast<MarqueeEngine*>(marqPtr.release());
                m_marqueeEngine->initialize(m_appCtx, nullptr);
            }
            m_webServer->setMarqueeEngine(m_marqueeEngine);
            m_displayRuntime.registerSourceEngine(DisplaySourceId::MARQUEE, m_marqueeEngine);
            MDNS.addService("http", "tcp", 80);
            MDNS.addService("upnp", "tcp", 80);
            MDNS.addService("mediarenderer", "tcp", 80);
            
            if (snapshot.mqtt.enabled) {
                m_frontendListener = new FrontendSyncEngine(snapshot.mqtt, gifEngine, m_messageEngine);
                m_frontendListener->begin();
                if (m_appCtx) m_appCtx->setEventBus(m_frontendListener);
            }
        } else {
            Serial.println("Wi-Fi connection failed. Starting Access Point (AP) Mode.");
            WiFi.mode(WIFI_AP);
            WiFi.softAP("ArcadeMatrix", "12345678");
            String apMsg = "Offline Mode (AP: ArcadeMatrix)";
            MessageConfig failConfig = {apMsg, 0xF800, 1, "rtl", 50, 1};
            m_messageEngine->displayMessage(failConfig);
            
            m_webServer = new WebServerAPI(80, m_messageEngine);
            m_webServer->begin();
            auto marqueeDesc = EngineRegistry::getDescriptor("marquee");
            if (marqueeDesc && marqueeDesc->factory) {
                auto marqPtr = marqueeDesc->factory();
                m_marqueeEngine = static_cast<MarqueeEngine*>(marqPtr.release());
                m_marqueeEngine->initialize(m_appCtx, nullptr);
                m_displayRuntime.registerSourceEngine(DisplaySourceId::MARQUEE, m_marqueeEngine);
            }
            m_webServer->setMarqueeEngine(m_marqueeEngine);
        }
    } else {
        Serial.println("No Wi-Fi credentials provided. Starting Access Point (AP) Mode.");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ArcadeMatrix", "12345678");
        String apMsg = "Offline Mode (AP: ArcadeMatrix)";
        MessageConfig failConfig = {apMsg, 0xF800, 1, "rtl", 50, 1};
        m_messageEngine->displayMessage(failConfig);
        
        m_webServer = new WebServerAPI(80, m_messageEngine);
        m_webServer->begin();
        auto marqueeDesc = EngineRegistry::getDescriptor("marquee");
        if (marqueeDesc && marqueeDesc->factory) {
            auto marqPtr = marqueeDesc->factory();
            m_marqueeEngine = static_cast<MarqueeEngine*>(marqPtr.release());
            m_marqueeEngine->initialize(m_appCtx, nullptr);
            m_displayRuntime.registerSourceEngine(DisplaySourceId::MARQUEE, m_marqueeEngine);
        }
        m_webServer->setMarqueeEngine(m_marqueeEngine);
    }

    // Allow message to finish scrolling before main loop
    LOGD("System", "Waiting for MessageEngine to finish...");
    unsigned long startWait = millis();
    while (m_messageEngine->isActive()) {
        matrixEngine.getDisplay()->fillScreen(0);
        m_messageEngine->update(m_appCtx);
        m_messageEngine->render(m_appCtx);
        matrixEngine.getDisplay()->flipDMABuffer();
        delay(5);
        if (millis() - startWait > 5000) {
            LOGW("System", "MessageEngine wait timeout! Force stopping.");
            m_messageEngine->deactivate();
            break;
        }
    }
    LOGD("System", "MessageEngine finished.");
    
    LOGD("System", "Starting rotationManager...");
    rotationManager->begin(config);
    LOGD("System", "rotationManager started.");

    audioSessionManager.begin();
    audioSessionManager.update(snapshot);

    LOGI("System", "Setup complete. Entering loop().");
}

void AppRuntime::handleNightMode(const ConfigSnapshot& snapshot) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        bool is_night = false;
        if (snapshot.system.night_mode_enabled) {
            int now_min = timeinfo.tm_hour * 60 + timeinfo.tm_min;
            int off_min = snapshot.system.turn_off_at.substring(0, 2).toInt() * 60 + snapshot.system.turn_off_at.substring(3).toInt();
            int wake_min = snapshot.system.wake_up_at.substring(0, 2).toInt() * 60 + snapshot.system.wake_up_at.substring(3).toInt();
            if (off_min > wake_min) {
                is_night = (now_min >= off_min || now_min < wake_min);
            } else {
                is_night = (now_min >= off_min && now_min < wake_min);
            }
        }
        
        if (is_night) {
            if (snapshot.system.night_brightness == 0) {
                matrixEngine.getDisplay()->fillScreen(0);
                matrixEngine.getDisplay()->flipDMABuffer();
                delay(1000);
                return;
            } else {
                matrixEngine.setBrightness(snapshot.system.night_brightness);
            }
        } else {
            matrixEngine.setBrightness(snapshot.matrix.powerLimitPercent);
        }
    }
}

void AppRuntime::syncMqtt(const ConfigSnapshot& snapshot) {
    static bool lastMqttEnabled = snapshot.mqtt.enabled;
    static String lastMqttBroker = snapshot.mqtt.broker;
    static int lastMqttPort = snapshot.mqtt.port;
    static String lastMqttTopicBato = snapshot.mqtt.topic_batocera;
    static String lastMqttTopicRecal = snapshot.mqtt.topic_recalbox;

    if (snapshot.mqtt.enabled != lastMqttEnabled || 
        (snapshot.mqtt.enabled && (snapshot.mqtt.broker != lastMqttBroker || snapshot.mqtt.port != lastMqttPort || 
                                 snapshot.mqtt.topic_batocera != lastMqttTopicBato || snapshot.mqtt.topic_recalbox != lastMqttTopicRecal))) {
        lastMqttEnabled = snapshot.mqtt.enabled;
        lastMqttBroker = snapshot.mqtt.broker;
        lastMqttPort = snapshot.mqtt.port;
        lastMqttTopicBato = snapshot.mqtt.topic_batocera;
        lastMqttTopicRecal = snapshot.mqtt.topic_recalbox;

        if (snapshot.mqtt.enabled) {
            if (!m_frontendListener) {
                m_frontendListener = new FrontendSyncEngine(snapshot.mqtt, gifEngine, m_messageEngine);
            }
            m_frontendListener->begin();
            if (m_appCtx) m_appCtx->setEventBus(m_frontendListener);
        } else {
            if (m_frontendListener) {
                m_frontendListener->stop();
            }
            if (m_appCtx) m_appCtx->setEventBus(nullptr);
        }
    }

    if (m_frontendListener && snapshot.mqtt.enabled) {
        m_frontendListener->loop();
    }
}

void AppRuntime::evaluateDisplayRequests(const ConfigSnapshot& snapshot) {
    if (visualizerEngine) {
        bool visEnabled = false;
        const EngineInstanceSnapshot* activeInst = nullptr;
        for (const auto& inst : snapshot.instances) {
            if (inst.engine_id == "audiovisualizer" || inst.engine_id == "visualizer") {
                if (inst.config.getBool("priority_mode", false) || inst.config.getBool("enabled", false)) {
                    visEnabled = true;
                    activeInst = &inst;
                    break;
                }
            }
        }
        if (visEnabled) {
            if (activeInst) visualizerEngine->onConfigChanged(&activeInst->config);
            DisplayRequest req{DisplaySourceId::VISUALIZER, DisplayPriority::VISUALIZER, RequestLifecycle::UNTIL_CANCELLED, true};
            m_displayArbiter.submitRequest(req);
        } else {
            m_displayArbiter.cancelRequest(DisplaySourceId::VISUALIZER);
        }
    }

    if (m_marqueeEngine) {
        if (m_marqueeEngine->isActive()) {
            DisplayRequest req{DisplaySourceId::MARQUEE, DisplayPriority::MARQUEE, RequestLifecycle::UNTIL_CANCELLED, true};
            m_displayArbiter.submitRequest(req);
        } else {
            m_displayArbiter.cancelRequest(DisplaySourceId::MARQUEE);
        }
    }
    if (m_messageEngine) {
        if (m_messageEngine->isActive()) {
            DisplayRequest req{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::UNTIL_CANCELLED, true};
            m_displayArbiter.submitRequest(req);
        } else {
            m_displayArbiter.cancelRequest(DisplaySourceId::MQTT);
        }
    }
    if (gifEngine) {
        if (gifEngine->isActive()) {
            DisplayRequest req{DisplaySourceId::GIF, DisplayPriority::GIF, RequestLifecycle::UNTIL_CANCELLED, true};
            m_displayArbiter.submitRequest(req);
        } else {
            m_displayArbiter.cancelRequest(DisplaySourceId::GIF);
        }
    }
}

void AppRuntime::update() {
    esp_task_wdt_reset();

    ConfigSnapshot snapshot = config.getSnapshot();

    audioSessionManager.update(snapshot);

    if (m_displayRuntime.isTransitioning()) {
        m_displayRuntime.renderTransition();
        matrixEngine.getDisplay()->flipDMABuffer();
        m_displayRuntime.getScheduler().delayUntilNextFrame(true);
        return;
    }

    if (m_firstLoop) {
        LOGI("System", "Entered first loop() iteration!");
        m_firstLoop = false;
    }

    if (!snapshot.matrix.matrix_power) {
        if (m_wasPoweredOn) {
            matrixEngine.getDisplay()->fillScreen(0);
            matrixEngine.getDisplay()->flipDMABuffer();
            matrixEngine.getDisplay()->fillScreen(0);
            matrixEngine.getDisplay()->flipDMABuffer();
            m_wasPoweredOn = false;
        }
        delay(100);
        return;
    }
    m_wasPoweredOn = true;

    syncMqtt(snapshot);
    evaluateDisplayRequests(snapshot);

    DisplayDecision decision = m_displayRuntime.update(snapshot);
    FrameRenderResult renderResult = m_displayRuntime.render(decision, m_appCtx);

    handleNightMode(snapshot);

    if (m_displayRuntime.getScheduler().evaluatePresentation(renderResult)) {
        matrixEngine.getDisplay()->flipDMABuffer();
    }

    bool isRealtime = decision.isRealtime || (rotationManager && rotationManager->isCurrentRealtime());
    m_displayRuntime.getScheduler().delayUntilNextFrame(isRealtime);
}
