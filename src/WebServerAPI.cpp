#include "WebServerAPI.h"
#include <SD.h>
#include <Update.h>
#include "MatrixEngine.h"
#include "GifEngine.h"
#include "RotationManager.h"
#include "WebUI.h"

WebServerAPI::WebServerAPI(uint16_t port, MessageEngine* msgEngine, ClockEngine* clkEngine) : server(port), msg(msgEngine), clock(clkEngine) {}

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
        request->send(response);
    });
    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", WebUI_html, WebUI_html_len);
        response->addHeader("Content-Encoding", "identity");
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
        DynamicJsonDocument doc(4096);
        doc["status"] = "online";
        doc["uptime"] = millis();
        doc["free_heap"] = ESP.getFreeHeap();
        sendJsonResponse(request, doc);
    });

    // API: List GIF Playlists (reads /gifs/playlists.json from SD)
    server.on("/api/playlists", HTTP_GET, [this](AsyncWebServerRequest *request){
        if (SD.exists("/gifs/playlists.json")) {
            request->send(SD, "/gifs/playlists.json", "application/json");
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
    });
    server.addHandler(playHandler);
    
    // API: Settings (GET)
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        DynamicJsonDocument doc(4096);

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

        // Weather
        doc["weather_api_key"] = config.weather.api_key;
        doc["weather_city"] = config.weather.city;
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

        // WiFi
        doc["wifi_ssid"] = config.wifi.ssid;
        doc["wifi_hostname"] = config.wifi.hostname;
        // password not sent for security

        // MQTT
        doc["mqtt_enable"] = config.mqtt.enabled;
        doc["mqtt_broker"] = config.mqtt.broker;
        doc["mqtt_port"] = config.mqtt.port;
        doc["mqtt_user"] = config.mqtt.user;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
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
        if (!doc["date_theme"].isNull()) {
            config.dateSettings.theme = doc["date_theme"].as<int>();
            extern DateEngine* dateEngine;
            if (dateEngine) dateEngine->setTheme((PublisherTheme)config.dateSettings.theme);
        }

        // Weather
        if (!doc["weather_api_key"].isNull()) config.weather.api_key = doc["weather_api_key"].as<String>();
        if (!doc["weather_city"].isNull()) config.weather.city = doc["weather_city"].as<String>();
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
        config.saveToSD("/conf.ini");

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
        
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(settingsHandler);
    
    // API: Get Selected GIF Playlists
    server.on("/api/playlists/selected", HTTP_GET, [](AsyncWebServerRequest *request){
        if (SD.exists("/playlists_selected.json")) {
            request->send(SD, "/playlists_selected.json", "application/json");
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
        if (SD.exists("/playlists_selected.json")) SD.remove("/playlists_selected.json");
        File f = SD.open("/playlists_selected.json", FILE_WRITE);
        if (f) {
            DynamicJsonDocument saveDoc(4096);
            JsonArray arr = saveDoc["playlists"].to<JsonArray>();
            for (const String& p : paths) arr.add(p);
            serializeJson(saveDoc, f);
            f.close();
            Serial.println("GIF playlists saved to SD.");
        } else {
            Serial.println("ERROR: Failed to write playlists_selected.json");
        }

        request->send(200, "application/json", "{\"success\":true}");
    });
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
            config.saveToSD("/conf.ini");
        }
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(clockHandler);
    
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

    // Handle Preflight CORS
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });
}
