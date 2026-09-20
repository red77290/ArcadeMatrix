#include "AppRuntime.h"
#include <FS.h>
#include <SPI.h>
#include "SDUtils.h"
#include "../include/HardwareProfile.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
#include "Logger.h"
#include "RenderStats.h"
#include "SdSpace.h"
#include "CpuLoad.h"
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
#include "Core0Lifecycle.h"
#include <esp_ota_ops.h>
#include "BuildInfo.h"

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
    
    // Explicitly enforce maximum CPU frequency (240MHz on ESP32 / ESP32-S3) to prevent
    // clock throttling after software resets (e.g. esp_restart() after OTA)
    setCpuFrequencyMhz(240);
    LOGI("System", "CPU Frequency: %u MHz | APB Frequency: %u MHz",
         (unsigned)getCpuFrequencyMhz(), (unsigned)(getApbFrequency() / 1000000));

    // 1. Initialize HAL first so auto-detection can be used by Registrar
    hardwareHAL.begin();
    EngineRegistrar::registerAll();
    
    randomSeed(esp_random());

    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (runningPartition && esp_ota_get_state_partition(runningPartition, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_NEW || ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            LOGI("OTA", "New OTA partition marked valid, rollback cancelled.");
        }
    }
    LOGI("System", "Starting ArcadeMatrix v%s (commit %s) from partition '%s' (0x%08x)",
         FIRMWARE_VERSION, BUILD_GIT_COMMIT,
         runningPartition ? runningPartition->label : "app0",
         runningPartition ? (unsigned)runningPartition->address : 0);

    constexpr uint32_t WDT_TIMEOUT_S = 30;
    esp_task_wdt_init(WDT_TIMEOUT_S, true);

#if defined(USE_RTC) && USE_RTC
    // The I2C bus is already up: hardwareHAL.begin() owns Wire.begin() plus the tuned clock and
    // timeout. Re-initializing it here would silently reset those settings.
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
    // Configure SD_MMC with max_files=5 so vfs_fat_ctx_t (~3.5KB) stays in fast internal DRAM (<4KB threshold)
    // rather than spilling into external PSRAM where HUB75 DMA bus contention and cache invalidations can occur.
    if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 5)) {
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
    SdSpace::start();
    CpuLoad::start();

    uint32_t preConfigFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t preConfigLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    uint32_t preConfigLargestDma = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!config.loadFromSD("/config.json")) {
        LOGW("Config", "/config.json not found or failed to parse. Using defaults.");
    } else {
        LOGI("Config", "Configuration loaded from /config.json.");
    }
    uint32_t postConfigFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t postConfigLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    uint32_t postConfigLargestDma = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    LOGI("Config", "ConfigLoader DRAM telemetry: freeInternal=%u (delta=%d), largestInternal=%u, largestDma=%u",
         postConfigFree, (int)(postConfigFree - preConfigFree), postConfigLargest, postConfigLargestDma);

    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const ConfigSnapshot& snapshot = guard.get();

    LOGI("Matrix", "Matrix Config: %dx%d, Chain: %d", snapshot.matrix.width, snapshot.matrix.height, snapshot.matrix.chainLength);
    if (!matrixEngine.begin(snapshot.matrix)) {
        LOGE("Matrix", "CRITICAL ERROR: Matrix init failed!");
        while (1) { delay(100); }
    }
    matrixEngine.setBrightness(snapshot.matrix.powerLimitPercent);
    m_lastAppliedBrightness = snapshot.matrix.powerLimitPercent;
    LOGI("System", "Free Heap after Matrix init: %d bytes", ESP.getFreeHeap());

    // NOTE: begin() does NOT re-probe the gyroscope (that's HardwareHAL's job); it only
    // captures the Adafruit_GFX display pointer and reads its real width()/height() to seed
    // _geometry. Without this call, _display stays nullptr forever, applyGeometryAndNotify()
    // silently no-ops on every subsequent call, and _geometry stays stuck at its
    // default-constructed 64x32 for the entire session -- causing every geometry-aware engine
    // (e.g. Dashboard) to lay out for a tiny 64x32 canvas instead of the real panel size.
    displayOrientationManager.begin(matrixEngine.getDisplay());
    displayOrientationManager.setRotationOffset(snapshot.matrix.rotation_offset);
    displayOrientationManager.setTransitionEffect(snapshot.matrix.rotation_transition);
    displayOrientationManager.setTransitionDuration(snapshot.matrix.rotation_transition_duration_ms);

    uint8_t initialRotation = (snapshot.matrix.auto_rotate && gyroHAL.isAvailable()) 
        ? (gyroHAL.getOrientation().suggestedRotation + snapshot.matrix.rotation_offset) % 4
        : (snapshot.matrix.rotation_offset % 4);
    displayOrientationManager.setRotation(initialRotation, false);

    audioHub.begin();

    Core0LifecycleDispatcher::instance().begin();
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
        m_displayRuntime.registerSourceEngine(DisplaySourceId::VISUALIZER, visualizerEngine, EngineHandle("audiovisualizer", "visualizer_main"));
    }

    auto msgDesc = EngineRegistry::getDescriptor("message");
    if (msgDesc && msgDesc->factory) {
        auto msgPtr = msgDesc->factory();
        m_messageEngine = static_cast<MessageEngine*>(msgPtr.release());
        m_messageEngine->initialize(m_appCtx, nullptr);
        m_displayRuntime.registerSourceEngine(DisplaySourceId::MQTT, m_messageEngine, EngineHandle("message", "message_main"));
    }

    auto gifDesc = EngineRegistry::getDescriptor("gifs");
    if (gifDesc && gifDesc->factory) {
        auto gifPtr = gifDesc->factory();
        gifEngine = static_cast<GifEngine*>(gifPtr.release());
        gifEngine->initialize(m_appCtx, nullptr);
        m_displayRuntime.registerSourceEngine(DisplaySourceId::GIF, gifEngine, EngineHandle("gifs", "gifs_main"));
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

        static auto registerMdnsServices = []() {
            static bool s_servicesRegistered = false;
            if (s_servicesRegistered) return;
            s_servicesRegistered = true;
            MDNS.addService("http", "tcp", 80);
            MDNS.addService("upnp", "tcp", 80);
            MDNS.addService("mediarenderer", "tcp", 80);
        };

        String wifiHostname = snapshot.wifi.hostname;
        WiFi.onEvent([wifiHostname](WiFiEvent_t event, WiFiEventInfo_t info) {
            (void)info;
            if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
                Serial.println("Wi-Fi disconnected - attempting to reconnect...");
                WiFi.reconnect();
            } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
                LOGI("WiFi", "Wi-Fi Connected! IP Address: %s", WiFi.localIP().toString().c_str());
                if (MDNS.begin(wifiHostname.c_str())) {
                    LOGI("WiFi", "mDNS responder started: http://%s.local", wifiHostname.c_str());
                    registerMdnsServices();
                }
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
            matrixEngine.present();
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
            m_displayRuntime.registerSourceEngine(DisplaySourceId::MARQUEE, m_marqueeEngine, EngineHandle("marquee", "marquee_main"));
            registerMdnsServices();
            
            m_lastMqttEnabled = snapshot.mqtt.enabled;
            m_lastMqttBroker = snapshot.mqtt.broker;
            m_lastMqttPort = snapshot.mqtt.port;
            m_lastMqttUser = snapshot.mqtt.user;
            m_lastMqttPass = snapshot.mqtt.pass;
            if (snapshot.mqtt.enabled) {
                m_frontendListener = new FrontendSyncEngine(snapshot.mqtt, gifEngine, m_messageEngine);
                m_frontendListener->begin();
                if (m_appCtx) m_appCtx->setEventBus(m_frontendListener);
            }
        } else {
            Serial.println("Wi-Fi connection timed out. Starting dual Access Point (AP) & Station mode...");
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP("ArcadeMatrix", "12345678");
            String apMsg = "Offline Mode (AP: ArcadeMatrix)";
            MessageConfig failConfig = {apMsg, 0xF800, 1, "rtl", 50, 1};
            m_messageEngine->displayMessage(failConfig);
            
            m_webServer = new WebServerAPI(80, m_messageEngine);
            m_webServer->begin();
            m_webServer->setVisualizerEngine(visualizerEngine);
            auto marqueeDesc = EngineRegistry::getDescriptor("marquee");
            if (marqueeDesc && marqueeDesc->factory) {
                auto marqPtr = marqueeDesc->factory();
                m_marqueeEngine = static_cast<MarqueeEngine*>(marqPtr.release());
                m_marqueeEngine->initialize(m_appCtx, nullptr);
                m_displayRuntime.registerSourceEngine(DisplaySourceId::MARQUEE, m_marqueeEngine, EngineHandle("marquee", "marquee_main"));
            }
            m_webServer->setMarqueeEngine(m_marqueeEngine);
            // Re-arm background station connection so it automatically connects as soon as AP is ready
            WiFi.begin(snapshot.wifi.ssid.c_str(), snapshot.wifi.password.c_str());
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
            m_displayRuntime.registerSourceEngine(DisplaySourceId::MARQUEE, m_marqueeEngine, EngineHandle("marquee", "marquee_main"));
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
        matrixEngine.present();
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

    m_lastReconciledVersion = snapshot.version;
    evaluateDisplayRequests(snapshot);

    LOGI("System", "Setup complete. Entering loop().");
}

bool AppRuntime::handleNightMode(const ConfigSnapshot& snapshot) {
    bool is_night = false;
    struct tm timeinfo;
    if (snapshot.system.night_mode_enabled && getLocalTime(&timeinfo, 0)) {
        int now_min = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        int off_min = snapshot.system.turn_off_at.substring(0, 2).toInt() * 60 + snapshot.system.turn_off_at.substring(3).toInt();
        int wake_min = snapshot.system.wake_up_at.substring(0, 2).toInt() * 60 + snapshot.system.wake_up_at.substring(3).toInt();
        if (off_min > wake_min) {
            is_night = (now_min >= off_min || now_min < wake_min);
        } else {
            is_night = (now_min >= off_min && now_min < wake_min);
        }
    }
    
    uint8_t targetBrightness = is_night ? (uint8_t)snapshot.system.night_brightness : (uint8_t)snapshot.matrix.powerLimitPercent;
    if (is_night && targetBrightness == 0) {
        return false;
    }

    if (m_lastAppliedBrightness != (int)targetBrightness) {
        m_lastAppliedBrightness = targetBrightness;
        matrixEngine.setBrightness(targetBrightness);
    }
    return true;
}

void AppRuntime::syncMqtt(const ConfigSnapshot& snapshot) {
    if (snapshot.mqtt.enabled != m_lastMqttEnabled || 
        (snapshot.mqtt.enabled && (snapshot.mqtt.broker != m_lastMqttBroker || 
                                   snapshot.mqtt.port != m_lastMqttPort || 
                                   snapshot.mqtt.user != m_lastMqttUser || 
                                   snapshot.mqtt.pass != m_lastMqttPass))) {
        m_lastMqttEnabled = snapshot.mqtt.enabled;
        m_lastMqttBroker = snapshot.mqtt.broker;
        m_lastMqttPort = snapshot.mqtt.port;
        m_lastMqttUser = snapshot.mqtt.user;
        m_lastMqttPass = snapshot.mqtt.pass;

        if (snapshot.mqtt.enabled) {
            if (m_frontendListener) {
                delete m_frontendListener;
                m_frontendListener = nullptr;
            }
            m_frontendListener = new FrontendSyncEngine(snapshot.mqtt, gifEngine, m_messageEngine);
            m_frontendListener->begin();
            if (m_appCtx) m_appCtx->setEventBus(m_frontendListener);
        } else {
            if (m_frontendListener) {
                delete m_frontendListener;
                m_frontendListener = nullptr;
                if (m_appCtx) m_appCtx->setEventBus(nullptr);
            }
            if (m_messageEngine) {
                m_messageEngine->deactivate();
            }
            if (gifEngine) {
                gifEngine->stop();
            }
        }
    }

    if (m_frontendListener) {
        m_frontendListener->loop();
    }
}

void AppRuntime::evaluateDisplayRequests(const ConfigSnapshot& snapshot) {
    if (visualizerEngine) {
        bool visRequested = false;
        const EngineInstanceSnapshot* activeInst = nullptr;
        for (const auto& inst : snapshot.instances) {
            if (inst.engine_id == "audiovisualizer" || inst.engine_id == "visualizer") {
                bool enabled = inst.config.getBool("enabled", true);
                bool priorityMode = inst.config.getBool("priority_mode", false);
                bool autoOnAudio = inst.config.getBool("auto_on_audio", false);
                bool isAudioStreaming = (audioSessionManager.getActiveSession().state == AudioSessionState::STREAMING);
                if (enabled && (priorityMode || (autoOnAudio && isAudioStreaming))) {
                    visRequested = true;
                    activeInst = &inst;
                    break;
                }
            }
        }

        EngineHandle handle = EngineHandle("audiovisualizer", activeInst ? activeInst->instance_id.c_str() : "visualizer_main");
        if (visRequested) {
            if (activeInst) visualizerEngine->onConfigChanged(&activeInst->config);
            if (!m_syncVis.active || m_syncVis.handle != handle) {
                m_syncVis.active = true;
                m_syncVis.handle = handle;
                DisplayRequest req{DisplaySourceId::VISUALIZER, DisplayPriority::VISUALIZER, RequestLifecycle::UNTIL_CANCELLED, true};
                req.engineHandle = handle;
                m_displayArbiter.submitRequest(req);
            }
        } else {
            if (m_syncVis.active) {
                m_syncVis.active = false;
                m_syncVis.handle = EngineHandle();
                m_displayArbiter.cancelRequest(DisplaySourceId::VISUALIZER);
            }
        }
    }

    if (m_marqueeEngine) {
        bool active = m_marqueeEngine->isActive() && m_marqueeEngine->hasRawBuffer();
        EngineHandle handle("marquee", "marquee_main");
        if (active) {
            if (!m_syncMarquee.active || m_syncMarquee.handle != handle) {
                m_syncMarquee.active = true;
                m_syncMarquee.handle = handle;
                DisplayRequest req{DisplaySourceId::MARQUEE, DisplayPriority::MARQUEE, RequestLifecycle::UNTIL_CANCELLED, true};
                req.engineHandle = handle;
                req.allowsOverlay = snapshot.mqtt.allow_overlay;
                m_displayArbiter.submitRequest(req);
            }
        } else {
            if (m_syncMarquee.active) {
                m_syncMarquee.active = false;
                m_syncMarquee.handle = EngineHandle();
                m_displayArbiter.cancelRequest(DisplaySourceId::MARQUEE);
            }
        }
    }

    if (m_messageEngine) {
        bool active = m_messageEngine->isActive();
        EngineHandle handle("message", "message_main");
        if (active) {
            if (!m_syncMqtt.active || m_syncMqtt.handle != handle) {
                m_syncMqtt.active = true;
                m_syncMqtt.handle = handle;
                DisplayRequest req{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::UNTIL_CANCELLED, true};
                req.engineHandle = handle;
                req.allowsOverlay = snapshot.mqtt.allow_overlay;
                m_displayArbiter.submitRequest(req);
            }
        } else {
            if (m_syncMqtt.active) {
                m_syncMqtt.active = false;
                m_syncMqtt.handle = EngineHandle();
                m_displayArbiter.cancelRequest(DisplaySourceId::MQTT);
            }
        }
    }

    if (gifEngine) {
        bool active = gifEngine->isActive();
        EngineHandle handle("gifs", "gifs_main");
        if (active) {
            DisplayPriority priority = snapshot.mqtt.enabled
                ? DisplayPriority::MQTT
                : DisplayPriority::GIF;
            if (!m_syncGif.active || m_syncGif.handle != handle || m_syncGif.priority != priority) {
                m_syncGif.active = true;
                m_syncGif.handle = handle;
                m_syncGif.priority = priority;
                DisplayRequest req{DisplaySourceId::GIF, priority, RequestLifecycle::UNTIL_CANCELLED, true};
                req.engineHandle = handle;
                req.allowsOverlay = false;
                m_displayArbiter.submitRequest(req);
            }
        } else {
            if (m_syncGif.active) {
                m_syncGif.active = false;
                m_syncGif.handle = EngineHandle();
                m_syncGif.priority = DisplayPriority::GIF;
                m_displayArbiter.cancelRequest(DisplaySourceId::GIF);
            }
        }
    }
}

void AppRuntime::update() {
    esp_task_wdt_reset();

    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const ConfigSnapshot& snapshot = guard.get();

    if (snapshot.version != m_lastReconciledVersion) {
        m_lastReconciledVersion = snapshot.version;
        syncMqtt(snapshot);
    }
    if (m_frontendListener) {
        m_frontendListener->loop();
    }
    evaluateDisplayRequests(snapshot);

    audioSessionManager.update(snapshot);

    if (m_displayRuntime.isTransitioning()) {
        m_displayRuntime.renderTransition();
        matrixEngine.markExternalDraw();
        matrixEngine.present();
        m_displayRuntime.getScheduler().delayUntilNextFrame(true);
        return;
    }

    if (m_firstLoop) {
        LOGI("System", "Entered first loop() iteration!");
        m_firstLoop = false;
    }

    bool displayActive = snapshot.matrix.matrix_power && handleNightMode(snapshot);

    if (!displayActive) {
        if (m_wasPoweredOn) {
            matrixEngine.setBrightness(0);
            matrixEngine.getDisplay()->fillScreen(0);
            matrixEngine.present();
            matrixEngine.getDisplay()->fillScreen(0);
            matrixEngine.present();
            matrixEngine.markExternalDraw();
            m_wasPoweredOn = false;
            m_lastAppliedBrightness = 0;
        }
        delay(100);
        return;
    }
    if (!m_wasPoweredOn) {
        m_wasPoweredOn = true;
        m_lastAppliedBrightness = -1;
        handleNightMode(snapshot);
    }

    // Core 1 cadence probe. A visualizer that "stops and resumes" is either starved of PCM data or
    // running on a stalled render loop; this reports the second case explicitly.
    static unsigned long lastFrameEnd = 0;
    unsigned long tFrameStart = millis();
    g_renderStats.loops++;

    DisplayDecision decision = m_displayRuntime.update(snapshot);
    unsigned long tAfterUpdate = millis();
    FrameRenderResult renderResult = m_displayRuntime.render(decision, m_appCtx);
    unsigned long tAfterRender = millis();

    if (lastFrameEnd != 0) {
        unsigned long framePeriod = tAfterRender - lastFrameEnd;
        if (framePeriod > 100) {
            LOGW("System", "Core 1 frame stall: period=%lu ms (pre=%lu, update=%lu, render=%lu, heap=%u)",
                 framePeriod, tFrameStart - lastFrameEnd, tAfterUpdate - tFrameStart,
                 tAfterRender - tAfterUpdate, (unsigned)ESP.getFreeHeap());
        }
    }
    lastFrameEnd = tAfterRender;

    if (m_displayRuntime.getScheduler().evaluatePresentation(renderResult)) {
        matrixEngine.present();
    }

    // Periodic 5s render performance telemetry to serial logs
    static uint32_t lastPerfLogMs = 0;
    static uint32_t lastLoops = 0, lastPresents = 0, lastGifFrames = 0, lastBlitUs = 0, lastDecodeUs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastPerfLogMs >= 5000) {
        if (lastPerfLogMs != 0) {
            uint32_t dtMs = nowMs - lastPerfLogMs;
            uint32_t loops = g_renderStats.loops.load();
            uint32_t presents = g_renderStats.presents.load();
            uint32_t gifFrames = g_renderStats.gifFrames.load();
            uint32_t blitUs = g_renderStats.gifBlitMicros.load();
            uint32_t decodeUs = g_renderStats.gifDecodeMicros.load();

            float loopFps = (float)(loops - lastLoops) * 1000.0f / (float)dtMs;
            float presentFps = (float)(presents - lastPresents) * 1000.0f / (float)dtMs;
            uint32_t gf = gifFrames - lastGifFrames;

            if (gf > 0) {
                float gifFps = (float)gf * 1000.0f / (float)dtMs;
                float avgBlit = (float)(blitUs - lastBlitUs) / 1000.0f / (float)gf;
                float avgDecode = (float)(decodeUs - lastDecodeUs) / 1000.0f / (float)gf;
                LOGI("RenderStats", "FPS: %.1f present | GIF: %.1f FPS (blit=%.2f ms, decode=%.2f ms, loop=%.1f)",
                     presentFps, gifFps, avgBlit, avgDecode, loopFps);
            } else {
                LOGI("RenderStats", "FPS: %.1f present | loop=%.1f (freeHeap=%u)",
                     presentFps, loopFps, (unsigned)ESP.getFreeHeap());
            }

            lastLoops = loops;
            lastPresents = presents;
            lastGifFrames = gifFrames;
            lastBlitUs = blitUs;
            lastDecodeUs = decodeUs;
        } else {
            lastLoops = g_renderStats.loops.load();
            lastPresents = g_renderStats.presents.load();
            lastGifFrames = g_renderStats.gifFrames.load();
            lastBlitUs = g_renderStats.gifBlitMicros.load();
            lastDecodeUs = g_renderStats.gifDecodeMicros.load();
        }
        lastPerfLogMs = nowMs;
    }

    bool isRealtime = decision.isRealtime || (rotationManager && rotationManager->isCurrentRealtime());
    m_displayRuntime.getScheduler().delayUntilNextFrame(isRealtime, renderResult.nextDueInMs);
}
