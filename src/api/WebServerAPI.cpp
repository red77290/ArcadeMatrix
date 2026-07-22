#include "WebServerAPI.h"
#include "../core/SDUtils.h"
#include <Update.h>
#include "../core/MatrixEngine.h"
#include "../engines/GifEngine.h"
#include "../core/RotationManager.h"
#include "WebUI.h"
#include "../core/Globals.h"

// Helper class to stream large files from SdFat to ESPAsyncWebServer
class AsyncSdFatResponse : public AsyncAbstractResponse {
private:
    FsFile _content;
public:
    AsyncSdFatResponse(const String& path, const String& contentType) {
        _code = 200;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            _content = sd.open(path.c_str(), O_READ);
            if (_content) _contentLength = _content.size();
            else _contentLength = 0;
            xSemaphoreGive(sdMutex);
        } else {
            _contentLength = 0;
        }
        _contentType = contentType;
    }
    ~AsyncSdFatResponse() {
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            if(_content) _content.close();
            xSemaphoreGive(sdMutex);
        }
    }
    size_t _fillBuffer(uint8_t *buf, size_t maxLen) override {
        size_t bytesRead = 0;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            if (_content) bytesRead = _content.read(buf, maxLen);
            xSemaphoreGive(sdMutex);
        }
        return bytesRead;
    }
};


WebServerAPI::WebServerAPI(uint16_t port, MessageEngine* msgEngine, ClockEngine* clkEngine) : server(port), msg(msgEngine), clock(clkEngine) {}

void WebServerAPI::setMarqueeEngine(MarqueeEngine* engine) {
    marquee = engine;
}

void WebServerAPI::begin() {
    setupRoutes();
    
    // Default headers for CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Serve the Web UI directly from Firmware Flash (PROGMEM)
    // This bypasses the SD card entirely, preventing SPI lockups with GifEngine!
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", WebUI_html, WebUI_html_len);
        response->addHeader("Content-Encoding", "identity");
        response->addHeader("Cache-Control", "no-store, max-age=0");
        request->send(response);
    });
    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", WebUI_html, WebUI_html_len);
        response->addHeader("Content-Encoding", "identity");
        response->addHeader("Cache-Control", "no-store, max-age=0");
        request->send(response);
    });

    server.begin();
    Serial.println("Web Server Started.");
}

void WebServerAPI::sendJsonResponse(AsyncWebServerRequest *request, JsonDocument& doc) {
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServerAPI::setupRoutes() {

    // API: Get Device Status
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(512);
        doc["status"] = "online";
        doc["uptime"] = millis();
        doc["free_heap"] = ESP.getFreeHeap();
        // Lowest free-heap value ever observed since boot: the single most useful number for
        // spotting slow memory leaks/fragmentation over days of uptime (a single free_heap
        // snapshot can look fine while still trending toward an OOM crash).
        doc["min_free_heap"] = ESP.getMinFreeHeap();
        doc["max_alloc_heap"] = ESP.getMaxAllocHeap();
        doc["psram_found"] = psramFound();
        if (psramFound()) {
            doc["free_psram"] = ESP.getFreePsram();
        }
        sendJsonResponse(request, doc);
    });

    // API: List custom SD fonts (.amf files converted via tools/bdf_to_amfont, dropped in /fonts)
    // for the Clock/Date "Font" dropdowns. Falls back to an empty list (dropdown just keeps its
    // "System/Default" option) if /fonts doesn't exist yet - no error either way.
    server.on("/api/fonts", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.to<JsonArray>();
        
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            FsFile dir = sd.open("/fonts", O_READ);
            if (dir && dir.isDir()) {
                FsFile entry = dir.openNextFile();
            while (entry) {
                yield();
                if (!entry.isDir()) {
                    String name = getFileName(entry);
                    if (name.endsWith(".amf") || name.endsWith(".AMF")) {
                        // getName may be relative or absolute depending on core version; normalize to "/fonts/xxx.amf"
                        if (!name.startsWith("/")) name = "/fonts/" + name;
                        arr.add(name);
                    }
                }
                entry.close();
                entry = dir.openNextFile();
            }
            dir.close();
            }
            xSemaphoreGive(sdMutex);
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: List GIF Playlists (reads /gifs/playlists.json from SD)
    server.on("/api/playlists", HTTP_GET, [](AsyncWebServerRequest *request){
        bool exists = false;
        String content = "";
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            FsFile f = sd.open("/gifs/playlists.json", O_READ);
            if (f) {
                exists = true;
                content = f.readString();
                f.close();
            }
            xSemaphoreGive(sdMutex);
        }
        if (exists && content.length() > 0) {
            request->send(200, "application/json", content);
        } else {
            request->send(404, "application/json", "{\"error\":\"playlists.json not found. Run generate_index.sh\"}");
        }
    });

    // API: Play GIF Playlists immediately
    AsyncCallbackJsonWebHandler* playHandler = new AsyncCallbackJsonWebHandler("/api/playlists/play", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        JsonArray playlistsArray = doc["playlists"].as<JsonArray>();
        
        std::vector<String> paths;
        for (JsonVariant v : playlistsArray) {
            paths.push_back(v.as<String>());
        }
        
        extern GifEngine gifEngine;
        gifEngine.playPlaylists(paths);
        
        request->send(200, "application/json", "{\"success\":true}");
    }, 4096);
    server.addHandler(playHandler);
    
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        // Reduce allocation from 4096 to 2048 to prevent heap fragmentation crashes
        AsyncJsonResponse * response = new AsyncJsonResponse(false, 2048);
        JsonObject doc = response->getRoot().as<JsonObject>();
        
        if (doc.isNull()) {
            request->send(500, "text/plain", "OOM JSON");
            delete response;
            return;
        }

        // Matrix
        doc["brightness_limit"] = config.matrix.powerLimitPercent;
        doc["color_depth"] = config.matrix.colorDepth;

        // Idle rotation
        doc["rotation"] = config.idle.rotation;
        doc["clock_duration_sec"] = config.idle.clock_duration_sec;
        doc["date_duration_sec"] = config.idle.date_duration_sec;
        doc["weather_duration_sec"] = config.idle.weather_duration_sec;
        doc["gifs_count"] = config.idle.gifs_count;
        doc["sprite_count"] = config.idle.sprite_count;
        doc["fighter_interval_sec"] = config.idle.fighter_interval_sec;

        // Clock
        doc["clock_font"] = config.time.clock_font;
        doc["clock_size"] = config.time.clock_size;
        doc["clock_theme"] = config.time.clock_theme;
        doc["clock_offset_x"] = config.time.clock_offset_x;
        doc["clock_offset_y"] = config.time.clock_offset_y;
        doc["clock_color_1"] = config.time.clock_color_1;
        doc["clock_color_2"] = config.time.clock_color_2;
        doc["clock_font_path"] = config.time.clock_font_path;

        // Date
        doc["date_font"] = config.dateSettings.date_font;
        doc["date_size"] = config.dateSettings.date_size;
        doc["date_theme"] = config.dateSettings.theme;
        doc["date_offset_x"] = config.dateSettings.date_offset_x;
        doc["date_offset_y"] = config.dateSettings.date_offset_y;
        doc["date_format"] = config.dateSettings.format;
        doc["date_sprite"] = config.dateSettings.background_sprite;
        doc["date_color_1"] = config.dateSettings.date_color_1;
        doc["date_color_2"] = config.dateSettings.date_color_2;
        doc["date_font_path"] = config.dateSettings.date_font_path;

        // Weather
        doc["weather_api_key"] = config.weather.api_key;
        doc["weather_city"] = config.weather.city;
        doc["weather_lang"] = config.weather.lang;
        doc["weather_offset_x"] = config.weather.weather_offset_x;
        doc["weather_offset_y"] = config.weather.weather_offset_y;

        // Time / NTP
        doc["ntp_server"] = config.time.ntpServer;
        doc["timezone"] = config.time.timezone;
        doc["format_24h"] = config.time.format24h;

        // Standby
        doc["night_mode_enabled"] = config.standby.night_mode_enabled;
        doc["turn_off_at"] = config.standby.turn_off_at;
        doc["wake_up_at"] = config.standby.wake_up_at;
        doc["matrix_brightness_night"] = config.standby.night_brightness;
        doc["matrix_power"] = config.standby.matrix_power;

        // WiFi
        doc["wifi_ssid"] = config.wifi.ssid;
        doc["wifi_hostname"] = config.wifi.hostname;
        // password not sent for security

        // MQTT
        doc["mqtt_enabled"] = config.mqtt.enabled;
        doc["mqtt_broker"] = config.mqtt.broker;
        doc["mqtt_port"] = config.mqtt.port;
        doc["mqtt_user"] = config.mqtt.user;
        doc["mqtt_pass"] = config.mqtt.pass;
        doc["mqtt_topic_bato"] = config.mqtt.topic_batocera;
        doc["mqtt_topic_recal"] = config.mqtt.topic_recalbox;
        doc["mqtt_device"] = config.mqtt.deviceName;

        response->setLength();
        request->send(response);
    });

    // API: Settings (POST) — saves immediately to SD
    AsyncCallbackJsonWebHandler* settingsHandler = new AsyncCallbackJsonWebHandler("/api/settings", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        
        // Matrix
        if (!doc["brightness_limit"].isNull()) {
            config.matrix.powerLimitPercent = doc["brightness_limit"].as<int>();
            extern MatrixEngine matrixEngine;
            matrixEngine.setBrightness(config.matrix.powerLimitPercent);
        }
        if (!doc["color_depth"].isNull()) config.matrix.colorDepth = doc["color_depth"].as<int>();
        
        // Idle rotation
        bool rotationChanged = false;
        if (!doc["rotation"].isNull()) { config.idle.rotation = doc["rotation"].as<String>(); rotationChanged = true; }
        if (!doc["clock_duration_sec"].isNull()) config.idle.clock_duration_sec = doc["clock_duration_sec"].as<int>();
        if (!doc["date_duration_sec"].isNull()) config.idle.date_duration_sec = doc["date_duration_sec"].as<int>();
        if (!doc["weather_duration_sec"].isNull()) config.idle.weather_duration_sec = doc["weather_duration_sec"].as<int>();
        if (!doc["gifs_count"].isNull()) config.idle.gifs_count = doc["gifs_count"].as<int>();
        if (!doc["sprite_count"].isNull()) config.idle.sprite_count = doc["sprite_count"].as<int>();
        if (!doc["fighter_interval_sec"].isNull()) config.idle.fighter_interval_sec = doc["fighter_interval_sec"].as<int>();
        
        // Clock
        bool clockChanged = false;
        if (!doc["clock_font"].isNull()) { config.time.clock_font = doc["clock_font"].as<int>(); clockChanged = true; }
        if (!doc["clock_size"].isNull()) { config.time.clock_size = doc["clock_size"].as<int>(); clockChanged = true; }
        if (!doc["clock_offset_x"].isNull()) { config.time.clock_offset_x = doc["clock_offset_x"].as<int>(); clockChanged = true; }
        if (!doc["clock_offset_y"].isNull()) { config.time.clock_offset_y = doc["clock_offset_y"].as<int>(); clockChanged = true; }
        if (!doc["clock_color_1"].isNull()) { config.time.clock_color_1 = doc["clock_color_1"].as<String>(); clockChanged = true; }
        if (!doc["clock_color_2"].isNull()) { config.time.clock_color_2 = doc["clock_color_2"].as<String>(); clockChanged = true; }
        if (!doc["clock_font_path"].isNull()) { config.time.clock_font_path = doc["clock_font_path"].as<String>(); clockChanged = true; }
        if (!doc["clock_theme"].isNull()) { config.time.clock_theme = doc["clock_theme"].as<int>(); clockChanged = true; }
        
        if (clockChanged) {
            extern ClockEngine* clockEngine;
            if (clockEngine) clockEngine->setTheme((PublisherTheme)config.time.clock_theme);
        }
        // Date
        if (!doc["date_font"].isNull()) config.dateSettings.date_font = doc["date_font"].as<int>();
        if (!doc["date_size"].isNull()) config.dateSettings.date_size = doc["date_size"].as<int>();
        if (!doc["date_offset_x"].isNull()) config.dateSettings.date_offset_x = doc["date_offset_x"].as<int>();
        if (!doc["date_offset_y"].isNull()) config.dateSettings.date_offset_y = doc["date_offset_y"].as<int>();
        if (!doc["date_format"].isNull()) config.dateSettings.format = doc["date_format"].as<String>();
        if (!doc["date_sprite"].isNull()) config.dateSettings.background_sprite = doc["date_sprite"].as<String>();
        if (!doc["date_color_1"].isNull()) config.dateSettings.date_color_1 = doc["date_color_1"].as<String>();
        if (!doc["date_color_2"].isNull()) config.dateSettings.date_color_2 = doc["date_color_2"].as<String>();
        if (!doc["date_font_path"].isNull()) {
            config.dateSettings.date_font_path = doc["date_font_path"].as<String>();
            extern DateEngine* dateEngine;
            if (dateEngine) dateEngine->reloadCustomFont();
        }
        if (!doc["date_theme"].isNull()) {
            config.dateSettings.theme = doc["date_theme"].as<int>();
            extern DateEngine* dateEngine;
            if (dateEngine) dateEngine->setTheme((PublisherTheme)config.dateSettings.theme);
        }

        // Weather
        if (!doc["weather_api_key"].isNull()) config.weather.api_key = doc["weather_api_key"].as<String>();
        if (!doc["weather_city"].isNull()) config.weather.city = doc["weather_city"].as<String>();
        if (!doc["weather_lang"].isNull()) config.weather.lang = doc["weather_lang"].as<String>();
        if (!doc["weather_offset_x"].isNull()) config.weather.weather_offset_x = doc["weather_offset_x"].as<int>();
        if (!doc["weather_offset_y"].isNull()) config.weather.weather_offset_y = doc["weather_offset_y"].as<int>();

        // Time / NTP
        if (!doc["ntp_server"].isNull()) config.time.ntpServer = doc["ntp_server"].as<String>();
        if (!doc["timezone"].isNull()) config.time.timezone = doc["timezone"].as<String>();
        if (!doc["ntp_server"].isNull() || !doc["timezone"].isNull()) {
            configTzTime(config.time.timezone.c_str(), config.time.ntpServer.c_str());
        }
        if (!doc["format_24h"].isNull()) config.time.format24h = doc["format_24h"].as<bool>();

        // Standby
        if (!doc["night_mode_enabled"].isNull()) config.standby.night_mode_enabled = doc["night_mode_enabled"].as<bool>();
        if (!doc["turn_off_at"].isNull()) config.standby.turn_off_at = doc["turn_off_at"].as<String>();
        if (!doc["wake_up_at"].isNull()) config.standby.wake_up_at = doc["wake_up_at"].as<String>();
        if (!doc["matrix_brightness_night"].isNull()) config.standby.night_brightness = doc["matrix_brightness_night"].as<int>();

        // WiFi
        if (!doc["wifi_ssid"].isNull()) config.wifi.ssid = doc["wifi_ssid"].as<String>();
        if (!doc["wifi_hostname"].isNull()) config.wifi.hostname = doc["wifi_hostname"].as<String>();
        if (!doc["wifi_pass"].isNull() && doc["wifi_pass"].as<String>() != "") config.wifi.password = doc["wifi_pass"].as<String>();

        // MQTT
        if (!doc["mqtt_enable"].isNull()) config.mqtt.enabled = doc["mqtt_enable"].as<bool>();
        if (!doc["mqtt_broker"].isNull()) config.mqtt.broker = doc["mqtt_broker"].as<String>();
        if (!doc["mqtt_port"].isNull()) config.mqtt.port = doc["mqtt_port"].as<int>();
        if (!doc["mqtt_user"].isNull()) config.mqtt.user = doc["mqtt_user"].as<String>();
        if (!doc["mqtt_pass"].isNull() && doc["mqtt_pass"].as<String>() != "") config.mqtt.pass = doc["mqtt_pass"].as<String>();

        // Save to SD immediately
        bool saved = config.saveToSD("/conf.ini");

        if (rotationChanged) {
            extern RotationManager* rotationManager;
            if (rotationManager) rotationManager->begin(config);
        }

        // If reboot requested, save and restart
        if (!doc["reboot"].isNull() && doc["reboot"].as<bool>()) {
            request->send(200, "application/json", "{\"success\":true}");
            delay(500);
            ESP.restart();
            return;
        }
        
        if (!saved) {
            // Settings were applied in RAM (so the display updates immediately) but could NOT be
            // written to the SD card - they will be lost on the next reboot/power cycle. Surface
            // this clearly instead of silently reporting success, so the user knows to retry
            // (usually a transient SD card glitch) rather than assume everything was saved.
            request->send(200, "application/json", "{\"success\":true,\"sd_saved\":false,\"warning\":\"Settings applied but could not be saved to the SD card (conf.ini). They will be lost on reboot - please try saving again.\"}");
            return;
        }

        request->send(200, "application/json", "{\"success\":true,\"sd_saved\":true}");
    }, 4096);
    server.addHandler(settingsHandler);
    
    // API: Get Selected GIF Playlists
    server.on("/api/playlists/selected", HTTP_GET, [](AsyncWebServerRequest *request){
        bool exists = false;
        String content = "";
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            FsFile f = sd.open("/playlists_selected.json", O_READ);
            if (f) {
                exists = true;
                content = f.readString();
                f.close();
            }
            xSemaphoreGive(sdMutex);
        }
        if (exists && content.length() > 0) {
            request->send(200, "application/json", content);
        } else {
            // Return empty array instead of 404 to avoid UI crash
            request->send(200, "application/json", "{\"playlists\":[]}");
        }
    });
    
    // API: Save Selected GIF Playlists — write directly to SD
    AsyncCallbackJsonWebHandler* savePlaylistsHandler = new AsyncCallbackJsonWebHandler("/api/playlists/save", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        JsonArray playlistsArray = doc["playlists"].as<JsonArray>();
        
        std::vector<String> paths;
        for (JsonVariant v : playlistsArray) {
            paths.push_back(v.as<String>());
        }
        
        // Apply immediately
        extern GifEngine gifEngine;
        gifEngine.setDefaultPlaylists(paths);

        // Write directly to SD (no async flag — prevents data loss on reboot)
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            if (sd.exists("/playlists_selected.json")) sd.remove("/playlists_selected.json");
            FsFile f = sd.open("/playlists_selected.json", O_WRITE | O_CREAT | O_TRUNC);
            if (f) {
                DynamicJsonDocument saveDoc(1024);
                JsonArray arr = saveDoc["playlists"].to<JsonArray>();
                for (const String& p : paths) arr.add(p);
                serializeJson(saveDoc, f);
                f.close();
                Serial.println("GIF playlists saved to SD.");
            } else {
                Serial.println("ERROR: Failed to write playlists_selected.json");
            }
            xSemaphoreGive(sdMutex);
        }

        request->send(200, "application/json", "{\"success\":true}");
    }, 4096);
    server.addHandler(savePlaylistsHandler);

    // API: Send Marquee Message
    AsyncCallbackJsonWebHandler* msgHandler = new AsyncCallbackJsonWebHandler("/api/message", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        
        MessageConfig cfg;
        cfg.text = doc["text"] | "Hello";
        cfg.color = doc["color"] | 63488;
        cfg.size = doc["size"] | 2;
        cfg.direction = doc["direction"] | "rtl";
        cfg.speed = doc["speed"] | 30;
        cfg.timeoutSeconds = doc["timeoutSeconds"] | 30;
        
        if (msg) msg->displayMessage(cfg);
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(msgHandler);
    
    // API: Change Clock Theme (also updates config + saves to SD)
    AsyncCallbackJsonWebHandler* clockHandler = new AsyncCallbackJsonWebHandler("/api/clock", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        
        if (clock) {
            int themeId = doc["clock_theme"] | doc["characterId"] | 0;
            clock->setTheme(static_cast<PublisherTheme>(themeId));
            extern ConfigLoader config;
            config.time.clock_theme = themeId;
            bool saved = config.saveToSD("/conf.ini");
            if (!saved) {
                request->send(200, "application/json", "{\"success\":true,\"sd_saved\":false,\"warning\":\"Theme applied but could not be saved to the SD card - it will revert on reboot.\"}");
                return;
            }
        }
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(clockHandler);

    // API: Toggle Panel Power
    AsyncCallbackJsonWebHandler* powerHandler = new AsyncCallbackJsonWebHandler("/api/system/power", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        
        if (!doc["state"].isNull()) {
            config.standby.matrix_power = doc["state"].as<bool>();
            if (!config.standby.matrix_power) {
                extern MatrixEngine matrixEngine;
                matrixEngine.getDisplay()->fillScreen(0);
                matrixEngine.getDisplay()->flipDMABuffer();
                matrixEngine.getDisplay()->fillScreen(0);
                matrixEngine.getDisplay()->flipDMABuffer();
            }
        }
        DynamicJsonDocument resp(1024);
        resp["status"] = "success";
        resp["matrix_power"] = config.standby.matrix_power;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });
    server.addHandler(powerHandler);
    
    // API: System commands (Reboot / Shutdown)
    server.on("/api/system/shutdown", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        delay(500);
        ESP.restart(); // ESP cannot truly shutdown via software, it just restarts or deep sleeps. We map shutdown to restart here.
    });
    server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        delay(500);
        ESP.restart();
    });
    
    // API: OTA Firmware Update
    server.on("/api/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!index) {
            Serial.printf("Update Start: %s\n", filename.c_str());
            if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
                Update.printError(Serial);
            }
        }
        if (!Update.hasError()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
        }
        if (final) {
            if (Update.end(true)) {
                Serial.printf("Update Success: %uB\nRebooting...\n", index+len);
                delay(500);
                ESP.restart();
            } else {
                Update.printError(Serial);
            }
        }
    });

    // API: Wi-Fi (re)configuration with an immediate connection attempt (parity with the RPi's
    // /api/wifi). Unlike the generic /api/settings handler, this persists the new credentials to
    // SD *and* tries to associate right away, reporting success/failure synchronously instead of
    // requiring a full reboot to find out if the new SSID/password actually work.
    AsyncCallbackJsonWebHandler* wifiHandler = new AsyncCallbackJsonWebHandler("/api/wifi", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            return;
        }
        JsonObject body = json.as<JsonObject>();
        if (body["ssid"].isNull() || body["password"].isNull()) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing ssid or password\"}");
            return;
        }

        extern ConfigLoader config;
        String newSsid = body["ssid"].as<String>();
        String newPass = body["password"].as<String>();

        config.wifi.ssid = newSsid;
        config.wifi.password = newPass;
        bool saved = config.saveToSD("/conf.ini");

        WiFi.disconnect(true);
        delay(100);
        WiFi.begin(newSsid.c_str(), newPass.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            String msg = "Connected! IP: " + WiFi.localIP().toString();
            if (!saved) msg += " (Warning: credentials could NOT be saved to SD - will be lost on reboot)";
            request->send(200, "application/json", "{\"success\":true,\"message\":\"" + msg + "\"}");
        } else if (!saved) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to connect AND could not save credentials to SD - nothing was persisted.\"}");
        } else {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to connect to the new network. Credentials were still saved to SD for the next reboot.\"}");
        }
    });
    server.addHandler(wifiHandler);

    // API: Live marquee/box-art image (raw RGB565, little-endian, row-major, matching the
    // configured panel resolution exactly - see tools/mugen_extractor for the same wire format
    // convention used by fighter sprites/date backgrounds). Parity feature with the RPi's
    // /api/marquee, adapted to what an MCU with no general image decoder can realistically do.
    server.on("/api/marquee", HTTP_POST,
        [this](AsyncWebServerRequest *request) {
            // The body handler below always sends the actual response once the full body has
            // arrived; this only fires as a fallback for a genuinely empty POST (no body at all).
            if (!request->_tempObject) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"No image data received\"}");
            }
        },
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!marquee) {
                if (index == 0) request->send(503, "application/json", "{\"success\":false,\"message\":\"Marquee engine not initialized\"}");
                return;
            }
            // Buffer the whole payload before validating, following the same request->_tempObject
            // pattern used by this library's own AsyncCallbackJsonWebHandler (auto-freed by
            // AsyncWebServerRequest's destructor, so no manual cleanup/leak risk here).
            if (index == 0 && total > 0) {
                request->_tempObject = malloc(total);
            }
            if (request->_tempObject) {
                memcpy((uint8_t*)request->_tempObject + index, data, len);
            }
            if (index + len == total) {
                if (request->_tempObject && total == marquee->expectedBufferBytes()) {
                    marquee->show((uint8_t*)request->_tempObject, total);
                    request->send(200, "application/json", "{\"success\":true,\"message\":\"Marquee image received and displayed\"}");
                } else {
                    char msgBuf[128];
                    snprintf(msgBuf, sizeof(msgBuf), "{\"success\":false,\"message\":\"Expected exactly %u bytes of raw RGB565 (panel resolution), got %u\"}", (unsigned)marquee->expectedBufferBytes(), (unsigned)total);
                    request->send(400, "application/json", msgBuf);
                }
            }
        }
    );

    // Handle Preflight CORS
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });
}
