#include "WebServerAPI.h"
#include <core/EngineRegistry.h>
#include <ArduinoJson.h>
#include "../core/SDUtils.h"
#include <Update.h>
#include "../core/MatrixEngine.h"
#include "../engines/GifEngine.h"
#include "../core/RotationManager.h"
#include "WebUI.h"
#include "../core/Globals.h"
#include "../core/Logger.h"
#include "../core/BuildInfo.h"
#include "../core/ConfigSanitizer.h"
#include "../engines/EngineRegistrar.h"

extern RotationManager* rotationManager;

// Helper class to stream large files from SdFat to ESPAsyncWebServer
class AsyncSdFatResponse : public AsyncAbstractResponse {
private:
    FsFile _content;
public:
    AsyncSdFatResponse(const String& path, const String& contentType) {
        _code = 200;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            _content = sd.open(path.c_str(), FILE_OPEN_READ);
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
            if (_content && _content.available()) {
                // SdSpiConfig and DMA SPI transfers expect 32-bit aligned memory located in internal RAM.
                // The buffer passed by ESPAsyncWebServer (`buf`) is sometimes allocated dynamically by AsyncTCP
                // on PSRAM or non-word-aligned boundaries, causing SdFat SPI read to crash with a LoadProhibited/StoreProhibited panic.
                // We bounce reads through a dedicated word-aligned heap buffer if direct DMA isn't guaranteed.
                uint8_t* bounceBuf = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (bounceBuf) {
                    size_t toRead = (maxLen > 4096) ? 4096 : maxLen;
                    size_t offset = 0;
                    while (toRead > 0) {
                        size_t chunk = (toRead > 512) ? 512 : toRead;
                        size_t r = _content.read(bounceBuf, chunk);
                        if (r > 0) memcpy(buf + offset, bounceBuf, r);
                        if (r == 0) break;
                        offset += r;
                        toRead -= r;
                        bytesRead += r;
                    }
                    heap_caps_free(bounceBuf);
                } else {
                    // Fallback if DMA memory is completely exhausted (might crash, but at least we tried)
                    bytesRead = _content.read(buf, maxLen);
                }
            }
            xSemaphoreGive(sdMutex);
        }
        return bytesRead;
    }
};


WebServerAPI::WebServerAPI(uint16_t port, MessageEngine* msgEngine) : server(port), msg(msgEngine) {}

void WebServerAPI::setMarqueeEngine(MarqueeEngine* engine) {
    marquee = engine;
}

void WebServerAPI::setVisualizerEngine(VisualizerEngine* engine) {
    visualizer = engine;
}


extern String getPosixTimezone(String tz);

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
    LOGI("WebServer", "Web Server Started.");
}

void WebServerAPI::sendJsonResponse(AsyncWebServerRequest *request, JsonDocument& doc) {
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServerAPI::setupRoutes() {

    // API: GET /api/hardware (Hardware Profile & Runtime Capabilities)
    server.on("/api/hardware", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(512);
        const auto& caps = hardwareHAL.capabilities();
        doc["profile"] = (caps.profile == HwProfile::WAVESHARE_S3) ? "WAVESHARE_S3" : "ESP32_STD";
        JsonObject psramObj = doc.createNestedObject("psram");
        psramObj["available"] = caps.hasPsram;
        psramObj["bytes"] = caps.psramBytes;
        doc["microphone"] = caps.hasMicrophone;
        doc["temperature_sensor"] = caps.hasTempSensor;
        doc["gyroscope"] = caps.hasGyroscope;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: GET /api/engines (Schema-driven engine descriptors)
    // API: GET /api/engines (Streamed engine descriptors with minimal RAM)
    server.on("/api/engines", HTTP_GET, [](AsyncWebServerRequest *request){
        size_t count = 0;
        const EngineDescriptor* descriptors = EngineRegistry::getAllDescriptors(count);
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->print("[");
        
        for (size_t i = 0; i < count; i++) {
            DynamicJsonDocument doc(2048);
            JsonObject obj = doc.to<JsonObject>();
            obj["metadata"]["id"] = descriptors[i].metadata.id;
            obj["metadata"]["name"] = descriptors[i].metadata.name;
            obj["metadata"]["category"] = descriptors[i].metadata.category;
            obj["metadata"]["version"] = descriptors[i].metadata.version;
            
            JsonObject capObj = obj.createNestedObject("capabilities");
            capObj["supports_128x32"] = descriptors[i].capabilities.supports_128x32;
            capObj["supports_256x64"] = descriptors[i].capabilities.supports_256x64;
            capObj["realtime"] = descriptors[i].capabilities.realtime;
            capObj["interruptible"] = descriptors[i].capabilities.interruptible;
            capObj["selfPaced"] = descriptors[i].capabilities.selfPaced;

            JsonObject reqObj = obj.createNestedObject("requirements");
            reqObj["needs_psram"] = descriptors[i].requirements.needsPsram;
            reqObj["needs_audio"] = descriptors[i].requirements.needsAudio;
            reqObj["needs_temp_sensor"] = descriptors[i].requirements.needsTempSensor;
            reqObj["needs_gyroscope"] = descriptors[i].requirements.needsGyroscope;
            reqObj["needs_network"] = descriptors[i].requirements.needsNetwork;
            reqObj["needs_sd"] = descriptors[i].requirements.needsSd;

            auto reqCheck = EngineRegistrar::checkRequirements(descriptors[i].requirements);
            obj["available"] = reqCheck.satisfied;
            if (!reqCheck.satisfied) {
                obj["reason"] = reqCheck.reason;
            }
            
            JsonArray schema = obj.createNestedArray("schema");
            for (const auto& field : descriptors[i].schema.fields) {
                JsonObject fieldObj = schema.createNestedObject();
                fieldObj["id"] = field.id;
                fieldObj["field_type"] = (int)field.type;
                fieldObj["label"] = field.label;
                fieldObj["description"] = field.description;
                fieldObj["default_value"] = field.default_value;
                if (field.type == ConfigType::ENUM || field.type == ConfigType::LIST) {
                    fieldObj["options"] = field.options;
                }
                if (strlen(field.options_endpoint) > 0) {
                    fieldObj["options_endpoint"] = field.options_endpoint;
                }
                if (field.multiple) {
                    fieldObj["multiple"] = true;
                }
                if (strlen(field.visible_when) > 0) {
                    fieldObj["visible_when"] = field.visible_when;
                }
                if (strlen(field.min_val) > 0) fieldObj["min_val"] = field.min_val;
                if (strlen(field.max_val) > 0) fieldObj["max_val"] = field.max_val;
                if (strlen(field.step) > 0) fieldObj["step"] = field.step;
            }
            
            if (i > 0) response->print(",");
            serializeJson(doc, *response);
        }
        
        response->print("]");
        request->send(response);
    });

    // API: GET /api/themes (Dynamic options endpoint for themes)
    server.on("/api/themes", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.to<JsonArray>();
        struct ThemeItem { int id; const char* name; };
        static const ThemeItem themes[] = {
            {0, "Nintendo"}, {1, "Capcom"}, {2, "Taito"}, {3, "Sega"},
            {4, "Cave"}, {5, "Konami"}, {6, "SNK"}, {7, "Technos"},
            {8, "IGS"}, {9, "Hudson"}, {10, "Banpresto"}, {11, "Namco"},
            {12, "Street Fighter (Ryu)"}, {13, "Super Mario"}, {14, "Metal Slug (Marco)"},
            {15, "Mega Man"}, {16, "Space Invaders"}, {17, "Bubble Bobble (Bub)"},
            {18, "Cyberpunk"}, {19, "Flip Clock"}, {20, "Custom Gradient"},
            {21, "True Matrix"}, {22, "Pong Clock"}, {23, "Tetris Clock"},
            {24, "Word Clock"}, {25, "Binary Clock"}, {26, "Pac-Man Clock"},
            {27, "Versus Clock"}, {28, "Slot Machine Clock"}, {29, "Tetris Game Boy"}
        };
        for (const auto& t : themes) {
            JsonObject obj = arr.createNestedObject();
            obj["id"] = t.id;
            obj["name"] = t.name;
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: GET /api/timezones (Dynamic options endpoint for timezones)
    server.on("/api/timezones", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.to<JsonArray>();
        struct TzItem { const char* value; const char* label; };
        static const TzItem timezones[] = {
            {"Europe/Paris", "Europe/Paris (UTC+1/+2)"},
            {"Europe/London", "Europe/London (UTC+0/+1)"},
            {"Europe/Dublin", "Europe/Dublin (UTC+0/+1)"},
            {"Europe/Lisbon", "Europe/Lisbon (UTC+0/+1)"},
            {"Europe/Berlin", "Europe/Berlin (UTC+1/+2)"},
            {"Europe/Madrid", "Europe/Madrid (UTC+1/+2)"},
            {"Europe/Rome", "Europe/Rome (UTC+1/+2)"},
            {"Europe/Brussels", "Europe/Brussels (UTC+1/+2)"},
            {"Europe/Amsterdam", "Europe/Amsterdam (UTC+1/+2)"},
            {"Europe/Zurich", "Europe/Zurich (UTC+1/+2)"},
            {"Europe/Vienna", "Europe/Vienna (UTC+1/+2)"},
            {"Europe/Warsaw", "Europe/Warsaw (UTC+1/+2)"},
            {"Europe/Prague", "Europe/Prague (UTC+1/+2)"},
            {"Europe/Stockholm", "Europe/Stockholm (UTC+1/+2)"},
            {"Europe/Oslo", "Europe/Oslo (UTC+1/+2)"},
            {"Europe/Copenhagen", "Europe/Copenhagen (UTC+1/+2)"},
            {"Europe/Athens", "Europe/Athens (UTC+2/+3)"},
            {"Europe/Helsinki", "Europe/Helsinki (UTC+2/+3)"},
            {"Europe/Bucharest", "Europe/Bucharest (UTC+2/+3)"},
            {"Europe/Kyiv", "Europe/Kyiv (UTC+2/+3)"},
            {"Europe/Moscow", "Europe/Moscow (UTC+3)"},
            {"Europe/Istanbul", "Europe/Istanbul (UTC+3)"},
            {"Atlantic/Reykjavik", "Atlantic/Reykjavik (UTC+0)"},
            {"Atlantic/Azores", "Atlantic/Azores (UTC-1/+0)"},
            {"America/New_York", "America/New_York (EST/EDT, UTC-5/-4)"},
            {"America/Detroit", "America/Detroit (EST/EDT, UTC-5/-4)"},
            {"America/Indiana/Indianapolis", "America/Indiana/Indianapolis (EST/EDT, UTC-5/-4)"},
            {"America/Montreal", "America/Montreal (EST/EDT, UTC-5/-4)"},
            {"America/Toronto", "America/Toronto (EST/EDT, UTC-5/-4)"},
            {"America/Chicago", "America/Chicago (CST/CDT, UTC-6/-5)"},
            {"America/Mexico_City", "America/Mexico_City (CST, UTC-6)"},
            {"America/Denver", "America/Denver (MST/MDT, UTC-7/-6)"},
            {"America/Boise", "America/Boise (MST/MDT, UTC-7/-6)"},
            {"America/Phoenix", "America/Phoenix (MST, UTC-7, no DST)"},
            {"America/Los_Angeles", "America/Los_Angeles (PST/PDT, UTC-8/-7)"},
            {"America/Vancouver", "America/Vancouver (PST/PDT, UTC-8/-7)"},
            {"America/Anchorage", "America/Anchorage (AKST/AKDT, UTC-9/-8)"},
            {"America/Halifax", "America/Halifax (AST/ADT, UTC-4/-3)"},
            {"America/St_Johns", "America/St_Johns (NST/NDT, UTC-3:30/-2:30)"},
            {"Pacific/Honolulu", "Pacific/Honolulu (HST, UTC-10)"},
            {"America/Sao_Paulo", "America/Sao_Paulo (BRT, UTC-3)"},
            {"America/Buenos_Aires", "America/Buenos_Aires (ART, UTC-3)"},
            {"America/Santiago", "America/Santiago (CLT/CLST, UTC-4/-3)"},
            {"America/Bogota", "America/Bogota (COT, UTC-5)"},
            {"America/Lima", "America/Lima (PET, UTC-5)"},
            {"Africa/Casablanca", "Africa/Casablanca (WEST, UTC+1)"},
            {"Africa/Cairo", "Africa/Cairo (EET/EEST, UTC+2/+3)"},
            {"Africa/Johannesburg", "Africa/Johannesburg (SAST, UTC+2)"},
            {"Africa/Nairobi", "Africa/Nairobi (EAT, UTC+3)"},
            {"Africa/Lagos", "Africa/Lagos (WAT, UTC+1)"},
            {"Asia/Jerusalem", "Asia/Jerusalem (IST/IDT, UTC+2/+3)"},
            {"Asia/Riyadh", "Asia/Riyadh (AST, UTC+3)"},
            {"Asia/Dubai", "Asia/Dubai (GST, UTC+4)"},
            {"Asia/Tehran", "Asia/Tehran (IRST, UTC+3:30)"},
            {"Asia/Karachi", "Asia/Karachi (PKT, UTC+5)"},
            {"Asia/Kolkata", "Asia/Kolkata (IST, UTC+5:30)"},
            {"Asia/Dhaka", "Asia/Dhaka (BST, UTC+6)"},
            {"Asia/Bangkok", "Asia/Bangkok (ICT, UTC+7)"},
            {"Asia/Jakarta", "Asia/Jakarta (WIB, UTC+7)"},
            {"Asia/Singapore", "Asia/Singapore (SGT, UTC+8)"},
            {"Asia/Hong_Kong", "Asia/Hong_Kong (HKT, UTC+8)"},
            {"Asia/Shanghai", "Asia/Shanghai (CST, UTC+8)"},
            {"Asia/Taipei", "Asia/Taipei (CST, UTC+8)"},
            {"Asia/Manila", "Asia/Manila (PST, UTC+8)"},
            {"Asia/Tokyo", "Asia/Tokyo (JST, UTC+9)"},
            {"Asia/Seoul", "Asia/Seoul (KST, UTC+9)"},
            {"Australia/Sydney", "Australia/Sydney (AEST/AEDT, UTC+10/+11)"},
            {"Australia/Melbourne", "Australia/Melbourne (AEST/AEDT, UTC+10/+11)"},
            {"Australia/Brisbane", "Australia/Brisbane (AEST, UTC+10)"},
            {"Australia/Adelaide", "Australia/Adelaide (ACST/ACDT, UTC+9:30/+10:30)"},
            {"Australia/Perth", "Australia/Perth (AWST, UTC+8)"},
            {"Pacific/Guam", "Pacific/Guam (ChST, UTC+10)"},
            {"Pacific/Auckland", "Pacific/Auckland (NZST/NZDT, UTC+12/+13)"},
            {"Pacific/Fiji", "Pacific/Fiji (FJT, UTC+12)"},
            {"UTC", "UTC (Coordinated Universal Time)"}
        };
        for (const auto& tz : timezones) {
            JsonObject obj = arr.createNestedObject();
            obj["value"] = tz.value;
            obj["label"] = tz.label;
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: GET /api/instances & POST /api/instances (CRUD instances)
    server.on("/api/instances", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->print("[");
        for (size_t i = 0; i < config.instances.size(); i++) {
            const auto& inst = config.instances[i];
            DynamicJsonDocument doc(1024);
            JsonObject obj = doc.to<JsonObject>();
            obj["instance_id"] = inst.instance_id;
            obj["engine_id"] = inst.engine_id;
            JsonObject cfgObj = obj.createNestedObject("config");
            for (const auto& kv : inst.config.getDictionary()) {
                cfgObj[kv.first] = kv.second;
            }
            if (i > 0) response->print(",");
            serializeJson(doc, *response);
        }
        response->print("]");
        request->send(response);
    });

    AsyncCallbackJsonWebHandler* instancesHandler = new AsyncCallbackJsonWebHandler("/api/instances", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        extern RotationManager* rotationManager;
        
        String instanceId = doc["instance_id"].as<String>();
        String engineId = doc["engine_id"].as<String>();
        if (instanceId.isEmpty()) {
            request->send(400, "application/json", "{\"error\":\"instance_id is required\"}");
            return;
        }
        
        EngineInstance* inst = config.getInstance(instanceId);
        bool isNew = (inst == nullptr);
        String oldEngineId = isNew ? "" : inst->engine_id;
        String targetEngineId = engineId.isEmpty() ? (inst ? inst->engine_id : instanceId) : engineId;

        const EngineDescriptor* desc = EngineRegistry::getDescriptor(targetEngineId.c_str());
        if (!desc) {
            request->send(400, "application/json", "{\"error\":\"Unknown engine_id\"}");
            return;
        }

        auto reqCheck = EngineRegistrar::checkRequirements(desc->requirements);
        if (!reqCheck.satisfied) {
            DynamicJsonDocument errDoc(256);
            errDoc["error"] = "engine_unavailable";
            errDoc["reason"] = reqCheck.reason;
            String errResp;
            serializeJson(errDoc, errResp);
            request->send(400, "application/json", errResp);
            return;
        }
        
        if (isNew) {
            inst = config.addInstance(instanceId, targetEngineId);
        }
        if (doc.containsKey("engine_id") && !engineId.isEmpty()) {
            inst->engine_id = engineId;
        }
        if (doc.containsKey("config") && doc["config"].is<JsonObject>()) {
            JsonObject cfg = doc["config"].as<JsonObject>();
            for (JsonPair kv : cfg) {
                inst->config.setString(kv.key().c_str(), kv.value().as<String>());
            }
        }
        
        bool structuralChange = isNew || (oldEngineId != inst->engine_id);

        // Sanitize and save
        ConfigSanitizer::sanitizeInstances(config);
        config.saveToSD("/config.json");
        
        if (rotationManager) {
            if (structuralChange) {
                rotationManager->recreateInstance(instanceId);
                rotationManager->resetRotation();
            } else {
                rotationManager->notifyConfigChanged(instanceId);
            }
        }
        
        if (inst->engine_id == "audiovisualizer" && visualizer) {
            visualizer->onConfigChanged(&inst->config);
        }
        
        request->send(200, "application/json", "{\"success\":true}");
    }, 4096);
    server.addHandler(instancesHandler);

    // API: DELETE /api/instances/{id} — Remove an instance by ID
    server.on("/api/instances", HTTP_DELETE, [](AsyncWebServerRequest *request){
        // ESPAsyncWebServer doesn't natively support path parameters,
        // so we look for ?id=xxx or parse the URL path manually.
        String instanceId = "";
        
        // Check query parameter first: DELETE /api/instances?id=xxx
        if (request->hasParam("id")) {
            instanceId = request->getParam("id")->value();
        }
        
        // Also support path-style: DELETE /api/instances/xxx (parsed from URL)
        String url = request->url();
        if (instanceId.isEmpty() && url.startsWith("/api/instances/")) {
            instanceId = url.substring(strlen("/api/instances/"));
            // URL-decode if needed (simple cases)
            instanceId.trim();
        }
        
        if (instanceId.isEmpty()) {
            request->send(400, "application/json", "{\"error\":\"instance_id is required (use ?id=xxx or /api/instances/xxx)\"}");
            return;
        }
        
        extern ConfigLoader config;
        extern RotationManager* rotationManager;
        
        bool removed = config.removeInstance(instanceId);
        if (!removed) {
            request->send(404, "application/json", "{\"error\":\"Instance not found\"}");
            return;
        }
        
        // Also remove from rotation if present
        for (auto it = config.rotation.begin(); it != config.rotation.end(); ) {
            if (it->instance_id == instanceId) {
                it = config.rotation.erase(it);
            } else {
                ++it;
            }
        }
        
        ConfigSanitizer::sanitizeInstances(config);
        config.saveToSD("/config.json");
        
        if (rotationManager) {
            rotationManager->recreateInstance(instanceId);
            rotationManager->resetRotation();
        }
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Also handle path-style DELETE: /api/instances/xxx (catchall for sub-paths)
    server.on("/api/instances/*", HTTP_DELETE, [](AsyncWebServerRequest *request){
        String url = request->url();
        String instanceId = "";
        if (url.startsWith("/api/instances/")) {
            instanceId = url.substring(strlen("/api/instances/"));
            instanceId.trim();
        }
        
        if (instanceId.isEmpty()) {
            request->send(400, "application/json", "{\"error\":\"instance_id is required\"}");
            return;
        }
        
        extern ConfigLoader config;
        extern RotationManager* rotationManager;
        
        bool removed = config.removeInstance(instanceId);
        if (!removed) {
            request->send(404, "application/json", "{\"error\":\"Instance not found\"}");
            return;
        }
        
        for (auto it = config.rotation.begin(); it != config.rotation.end(); ) {
            if (it->instance_id == instanceId) {
                it = config.rotation.erase(it);
            } else {
                ++it;
            }
        }
        
        ConfigSanitizer::sanitizeInstances(config);
        config.saveToSD("/config.json");
        
        if (rotationManager) {
            rotationManager->recreateInstance(instanceId);
            rotationManager->resetRotation();
        }
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // API: GET /api/rotation — Return the current rotation list
    server.on("/api/rotation", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& rot : config.rotation) {
            JsonObject obj = arr.createNestedObject();
            obj["instance_id"] = rot.instance_id;
            obj["duration_sec"] = rot.duration_sec;
            JsonObject ovObj = obj.createNestedObject("overlays");
            ovObj["fighter"] = rot.overlays.fighter;
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: POST /api/rotation — Replace the entire rotation list
    AsyncCallbackJsonWebHandler* rotationHandler = new AsyncCallbackJsonWebHandler("/api/rotation", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonArray>()) {
            request->send(400, "application/json", "{\"error\":\"Expected a JSON array of rotation entries\"}");
            return;
        }
        JsonArray arr = json.as<JsonArray>();
        extern ConfigLoader config;
        extern RotationManager* rotationManager;
        
        config.rotation.clear();
        for (JsonObject entry : arr) {
            RotationEntry re;
            re.instance_id = entry["instance_id"].as<String>();
            re.duration_sec = entry["duration_sec"] | 15;
            if (entry.containsKey("overlays") && entry["overlays"].is<JsonObject>()) {
                re.overlays.fighter = entry["overlays"]["fighter"] | false;
            } else if (entry.containsKey("fighter_overlay")) {
                re.overlays.fighter = entry["fighter_overlay"] | false;
            } else {
                re.overlays.fighter = false;
            }
            if (!re.instance_id.isEmpty()) {
                config.rotation.push_back(re);
            }
        }
        
        config.saveToSD("/config.json");
        
        if (rotationManager) {
            rotationManager->resetRotation();
        }
        
        request->send(200, "application/json", "{\"success\":true}");
    }, 4096);
    server.addHandler(rotationHandler);

    // API: Get Device Status
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(512);
        doc["status"] = "online";
        doc["uptime"] = millis();
        doc["free_heap"] = ESP.getFreeHeap();
        doc["min_free_heap"] = ESP.getMinFreeHeap();
        doc["max_alloc_heap"] = ESP.getMaxAllocHeap();
        doc["psram_found"] = hardwareHAL.capabilities().hasPsram;
        if (hardwareHAL.capabilities().hasPsram) {
            doc["free_psram"] = ESP.getFreePsram();
        }
        sendJsonResponse(request, doc);
    });

    // API: System Info (Dashboard metrics compatibility)
    server.on("/api/system_info", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(512);
        float tempC = 0.0f;
        if (hardwareHAL.capabilities().hasTempSensor) {
            tempC = hardwareHAL.readEnvironment().temperatureC;
        } else {
            tempC = temperatureRead();
        }
        
        doc["cpu_load"] = 0.0f;
        doc["temperature_c"] = tempC;
        doc["ram_used_mb"] = (float)(ESP.getHeapSize() - ESP.getFreeHeap()) / (1024.0f * 1024.0f);
        doc["ram_total_mb"] = (float)ESP.getHeapSize() / (1024.0f * 1024.0f);
        doc["disk_free_gb"] = 0.0f;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["uptime_sec"] = millis() / 1000;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: List fonts (Built-in + custom SD .amf files)
    server.on("/api/fonts", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.to<JsonArray>();
        
        arr.add("Default");
        arr.add("PressStart2P");
        arr.add("namco");
        arr.add("FreeSansBold");
        arr.add("FreeMonoBold");
        arr.add("RetroGaming");
        
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            FsFile dir = sd.open("/fonts", FILE_OPEN_READ);
            if (dir && isDirectory(dir)) {
                FsFile entry = dir.openNextFile();
            while (entry) {
                yield();
                if (!isDirectory(entry)) {
                    String name = getFileName(entry);
                    if (name.endsWith(".amf") || name.endsWith(".AMF")) {
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

    // API: Return version with Git commit and build timestamp
    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(256);
        doc["version"] = FIRMWARE_VERSION;
        doc["git_commit"] = BUILD_GIT_COMMIT;
        doc["build_timestamp"] = BUILD_TIMESTAMP;
        doc["arch"] = (hardwareHAL.capabilities().profile == HwProfile::WAVESHARE_S3) ? "esp32s3" : "esp32";
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Get Indoor Environment Sensor (Home Automation / REST Sensor)
    server.on("/api/sensor", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        EnvironmentData data = hardwareHAL.readEnvironment();
        DynamicJsonDocument doc(256);
        doc["available"] = data.available;
        doc["temperature_c"] = data.temperatureC;
        doc["temperature_f"] = data.temperatureF;
        doc["humidity"] = data.humidity;
        doc["unit"] = config.system.unit;
        doc["status"] = data.available ? "ok" : "not_detected";
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Music Visualizer Control (Priority Display Override)
    AsyncCallbackJsonWebHandler* visHandler = new AsyncCallbackJsonWebHandler("/api/visualizer", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        auto visInst = config.getInstance("visualizer_main");
        if (visInst) {
            if (!doc["enabled"].isNull()) {
                visInst->config.setBool("enabled", doc["enabled"].as<bool>());
                if (visualizer) {
                    if (visInst->config.getBool("enabled")) visualizer->activate();
                    else visualizer->deactivate();
                }
            }
            if (!doc["style"].isNull()) {
                visInst->config.setString("style", doc["style"].as<String>());
            }
            if (!doc["mode"].isNull()) {
                visInst->config.setString("style", doc["mode"].as<String>());
            }
            if (visualizer) {
                visualizer->onConfigChanged(&visInst->config);
            }
            if (!doc["gain"].isNull()) {
                visInst->config.setString("gain", String(doc["gain"].as<float>()));
                hardwareHAL.setMicGain(doc["gain"].as<float>());
            }
            if (!doc["sensitivity"].isNull()) {
                visInst->config.setString("sensitivity", String(doc["sensitivity"].as<int>()));
            }
        }
        config.saveToSD("/config.json");
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(visHandler);

    // API: List GIF Playlists (Direct SD streaming with zero heap allocation, falls back to dynamic directory scan)
    server.on("/api/playlists", HTTP_GET, [](AsyncWebServerRequest *request){
        bool hasJson = false;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            hasJson = sd.exists("/gifs/playlists.json");
            xSemaphoreGive(sdMutex);
        }
        
        if (hasJson) {
            // Stream the JSON file directly from SD to client in chunks (no full file malloc)
            request->send(new AsyncSdFatResponse("/gifs/playlists.json", "application/json"));
            return;
        }

        // Fallback: Dynamic directory scan if playlists.json is absent
        String content = "{";
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            bool first = true;
            FsFile dir = sd.open("/gifs");
            if (dir && isDirectory(dir)) {
                FsFile file;
                while (getNextFile(dir, file)) {
                    if (isDirectory(file)) {
                        String name = getFileName(file);
                        int lastSlash = name.lastIndexOf('/');
                        if (lastSlash >= 0) name = name.substring(lastSlash + 1);
                        
                        if (name.length() > 0 && name[0] != '.') {
                            if (!first) content += ",";
                            content += "\"" + name + "\":{";
                            content += "\"path\":\"/gifs/" + name + "\",";
                            content += "\"count\":0"; // No count in fallback to prevent WDT crashes
                            content += "}";
                            first = false;
                        }
                    }
                }
            }
            if (dir) dir.close();
            xSemaphoreGive(sdMutex);
        }
        content += "}";
        request->send(200, "application/json", content);
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
        
        extern GifEngine* gifEngine;
        if (gifEngine) gifEngine->playPlaylists(paths);
        
        request->send(200, "application/json", "{\"success\":true}");
    }, 4096);
    server.addHandler(playHandler);
    
        server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        AsyncJsonResponse * response = new AsyncJsonResponse(false, 4096);
        JsonObject doc = response->getRoot().as<JsonObject>();
        
        if (doc.isNull()) {
            request->send(500, "text/plain", "OOM JSON");
            delete response;
            return;
        }

        // Matrix
        doc["brightness_limit"] = config.matrix.powerLimitPercent;
        doc["color_depth"] = config.matrix.colorDepth;
        doc["matrix_chain"] = config.matrix.chainLength;
        doc["matrix_rows"] = config.matrix.height;
        doc["matrix_cols"] = config.matrix.width;
        doc["matrix_rgb_sequence"] = config.matrix.rgbSequence;
        doc["matrix_force_single_buffer"] = config.matrix.forceSingleBuffer;
        doc["matrix_driver_chip"] = config.matrix.driverChip;
        doc["matrix_clk_phase"] = config.matrix.clkPhase;
        doc["matrix_latch_blanking"] = config.matrix.latchBlanking;
        doc["matrix_row_address_mode"] = config.matrix.rowAddressMode;
        doc["matrix_limit_refresh_rate_hz"] = config.matrix.limitRefreshRateHz;

        auto getInst = [&](const String& id) { return config.getInstance(id); };
        
        // Crypto
        auto cryptoInst = getInst("crypto_main");
        if (cryptoInst) {
            doc["crypto_enabled"] = cryptoInst->config.getBool("enabled");
            doc["crypto_symbols"] = cryptoInst->config.getString("symbols");
            doc["crypto_duration_sec"] = cryptoInst->config.getInt("duration_sec");
            doc["crypto_cache_ttl_min"] = cryptoInst->config.getInt("cache_ttl_min");
            doc["crypto_currency"] = cryptoInst->config.getString("currency");
        }

        // Stock
        auto stockInst = getInst("stock_main");
        if (stockInst) {
            doc["stock_enabled"] = stockInst->config.getBool("enabled");
            doc["stock_symbols"] = stockInst->config.getString("symbols");
            doc["stock_duration_sec"] = stockInst->config.getInt("duration_sec");
            doc["stock_cache_ttl_min"] = stockInst->config.getInt("cache_ttl_min");
        }

        // Idle rotation
        String rotStr = "";
        for (const auto& r : config.rotation) rotStr += r.instance_id + ",";
        if (rotStr.endsWith(",")) rotStr.remove(rotStr.length()-1);
        doc["rotation"] = rotStr;
        
        auto getRot = [&](const String& id) {
            for (const auto& r : config.rotation) if (r.instance_id == id) return r.duration_sec;
            return 15;
        };
        doc["clock_duration_sec"] = getRot("clock_main");
        doc["date_duration_sec"] = getRot("date_main");
        doc["weather_duration_sec"] = getRot("weather_main");
        doc["temp_duration_sec"] = getRot("temp_main");
        doc["decibel_duration_sec"] = getRot("decibel_main");
        
        auto fighterInst = getInst("fighter_main");
        if (fighterInst) {
            doc["fighter_enabled"] = true;
            doc["fighter_interval_sec"] = fighterInst->config.getInt("fighter_interval_sec");
        }

        // Environment & Audio
        doc["temp_unit"] = config.system.unit;
        doc["temp_offset"] = config.system.temp_offset;
        
        auto visInst = getInst("visualizer_main");
        if (visInst) {
            doc["visualizer_enabled"] = visInst->config.getBool("enabled");
            doc["visualizer_mode"] = visInst->config.getString("mode");
            doc["mic_gain"] = visInst->config.getFloat("gain");
            doc["db_calibration"] = visInst->config.getFloat("db_calibration");
        }

        doc["sensor_available"] = hardwareHAL.isTempSensorAvailable();
        doc["audio_available"] = hardwareHAL.isAudioAvailable();
        doc["psram_available"] = hardwareHAL.capabilities().hasPsram;

        // Clock
        auto clockInst = getInst("clock_main");
        if (clockInst) {
            doc["clock_font"] = clockInst->config.getInt("clock_font");
            doc["clock_size"] = clockInst->config.getInt("clock_size");
            doc["clock_theme"] = clockInst->config.getInt("clock_theme");
            doc["clock_offset_x"] = clockInst->config.getInt("clock_offset_x");
            doc["clock_offset_y"] = clockInst->config.getInt("clock_offset_y");
            doc["clock_color_1"] = clockInst->config.getString("clock_color_1");
            doc["clock_color_2"] = clockInst->config.getString("clock_color_2");
            doc["clock_font_path"] = clockInst->config.getString("clock_font_path");
        }

        // Date
        auto dateInst = getInst("date_main");
        if (dateInst) {
            doc["date_font"] = dateInst->config.getInt("date_font");
            doc["date_size"] = dateInst->config.getInt("date_size");
            doc["date_theme"] = dateInst->config.getInt("theme");
            doc["date_offset_x"] = dateInst->config.getInt("date_offset_x");
            doc["date_offset_y"] = dateInst->config.getInt("date_offset_y");
            doc["date_format"] = dateInst->config.getString("format");
            doc["date_sprite"] = dateInst->config.getString("background_sprite");
            doc["date_color_1"] = dateInst->config.getString("date_color_1");
            doc["date_color_2"] = dateInst->config.getString("date_color_2");
            doc["date_font_path"] = dateInst->config.getString("date_font_path");
        }

        // Weather
        auto weatherInst = getInst("weather_main");
        if (weatherInst) {
            doc["weather_api_key"] = weatherInst->config.getString("api_key");
            doc["weather_city"] = weatherInst->config.getString("city");
            doc["weather_lang"] = weatherInst->config.getString("lang");
            doc["weather_offset_x"] = weatherInst->config.getInt("weather_offset_x");
            doc["weather_offset_y"] = weatherInst->config.getInt("weather_offset_y");
        }

        // System / Time
        doc["timezone"] = config.system.timezone;
        doc["format_24h"] = config.system.format24h;

        // Standby
        doc["night_mode_enabled"] = config.system.night_mode_enabled;
        doc["turn_off_at"] = config.system.turn_off_at;
        doc["wake_up_at"] = config.system.wake_up_at;
        doc["night_brightness"] = config.system.night_brightness;
        doc["idle_fighter_enabled"] = config.system.idle_fighter_enabled;
        doc["idle_fighter_interval"] = config.system.idle_fighter_interval;
        doc["matrix_power"] = config.matrix.matrix_power;

        // WiFi
        doc["wifi_ssid"] = config.wifi.ssid;
        doc["wifi_hostname"] = config.wifi.hostname;

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
    AsyncCallbackJsonWebHandler* settingsHandler = new AsyncCallbackJsonWebHandler("/api/settings", [this](AsyncWebServerRequest *request, JsonVariant &json) {
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
        if (!doc["matrix_chain"].isNull()) config.matrix.chainLength = doc["matrix_chain"].as<int>();
        if (!doc["matrix_rows"].isNull()) config.matrix.height = doc["matrix_rows"].as<int>();
        if (!doc["matrix_cols"].isNull()) config.matrix.width = doc["matrix_cols"].as<int>();
        if (!doc["matrix_rgb_sequence"].isNull()) config.matrix.rgbSequence = doc["matrix_rgb_sequence"].as<String>();
        if (!doc["matrix_force_single_buffer"].isNull()) config.matrix.forceSingleBuffer = doc["matrix_force_single_buffer"].as<bool>();
        if (!doc["matrix_limit_refresh_rate_hz"].isNull()) config.matrix.limitRefreshRateHz = doc["matrix_limit_refresh_rate_hz"].as<int>();
        if (!doc["matrix_driver_chip"].isNull()) config.matrix.driverChip = doc["matrix_driver_chip"].as<String>();
        if (!doc["matrix_clk_phase"].isNull()) config.matrix.clkPhase = doc["matrix_clk_phase"].as<bool>();
        if (!doc["matrix_latch_blanking"].isNull()) config.matrix.latchBlanking = doc["matrix_latch_blanking"].as<int>();
        if (!doc["matrix_row_address_mode"].isNull()) config.matrix.rowAddressMode = doc["matrix_row_address_mode"].as<int>();

        auto getInst = [&](const String& id) { return config.getInstance(id); };

        auto cryptoInst = getInst("crypto_main");
        if (cryptoInst) {
            if (!doc["crypto_enabled"].isNull()) cryptoInst->config.setBool("enabled", doc["crypto_enabled"].as<bool>());
            if (!doc["crypto_symbols"].isNull()) cryptoInst->config.setString("symbols", doc["crypto_symbols"].as<String>());
            if (!doc["crypto_duration_sec"].isNull()) cryptoInst->config.setInt("duration_sec", doc["crypto_duration_sec"].as<int>());
            if (!doc["crypto_cache_ttl_min"].isNull()) cryptoInst->config.setInt("cache_ttl_min", doc["crypto_cache_ttl_min"].as<int>());
            if (!doc["crypto_currency"].isNull()) cryptoInst->config.setString("currency", doc["crypto_currency"].as<String>());
        }

        auto stockInst = getInst("stock_main");
        if (stockInst) {
            if (!doc["stock_enabled"].isNull()) stockInst->config.setBool("enabled", doc["stock_enabled"].as<bool>());
            if (!doc["stock_symbols"].isNull()) stockInst->config.setString("symbols", doc["stock_symbols"].as<String>());
            if (!doc["stock_duration_sec"].isNull()) stockInst->config.setInt("duration_sec", doc["stock_duration_sec"].as<int>());
            if (!doc["stock_cache_ttl_min"].isNull()) stockInst->config.setInt("cache_ttl_min", doc["stock_cache_ttl_min"].as<int>());
        }

        // We aren't fully handling custom rotation strings yet without parsing commas, so for now just update durations
        auto setRot = [&](const String& id, int dur) {
            for (auto& r : config.rotation) if (r.instance_id == id) { r.duration_sec = dur; return; }
        };
        if (!doc["clock_duration_sec"].isNull()) setRot("clock_main", doc["clock_duration_sec"].as<int>());
        if (!doc["date_duration_sec"].isNull()) setRot("date_main", doc["date_duration_sec"].as<int>());
        if (!doc["weather_duration_sec"].isNull()) setRot("weather_main", doc["weather_duration_sec"].as<int>());
        if (!doc["temp_duration_sec"].isNull()) setRot("temp_main", doc["temp_duration_sec"].as<int>());
        if (!doc["decibel_duration_sec"].isNull()) setRot("decibel_main", doc["decibel_duration_sec"].as<int>());
        
        auto fighterInst = getInst("fighter_main");
        if (fighterInst) {
            if (!doc["fighter_interval_sec"].isNull()) fighterInst->config.setInt("fighter_interval_sec", doc["fighter_interval_sec"].as<int>());
        }

        if (!doc["temp_unit"].isNull()) config.system.unit = doc["temp_unit"].as<String>();
        if (!doc["temp_offset"].isNull()) config.system.temp_offset = doc["temp_offset"].as<float>();

        auto visInst = getInst("visualizer_main");
        if (visInst) {
            if (!doc["visualizer_enabled"].isNull()) {
                visInst->config.setBool("enabled", doc["visualizer_enabled"].as<bool>());
                extern VisualizerEngine* visualizerEngine;
                if (visualizerEngine) {
                    if (visInst->config.getBool("enabled")) visualizerEngine->activate();
                    else visualizerEngine->deactivate();
                }
            }
            if (!doc["visualizer_mode"].isNull()) {
                visInst->config.setString("mode", doc["visualizer_mode"].as<String>());
                extern VisualizerEngine* visualizerEngine;
                if (visualizerEngine) visualizerEngine->onConfigChanged(&visInst->config);
            }
            if (!doc["mic_gain"].isNull()) {
                visInst->config.setString("gain", String(doc["mic_gain"].as<float>()));
                hardwareHAL.setMicGain(doc["mic_gain"].as<float>());
            }
            if (!doc["db_calibration"].isNull()) visInst->config.setString("db_calibration", String(doc["db_calibration"].as<float>()));
        }

        bool willReboot = (!doc["reboot"].isNull() && doc["reboot"].as<bool>());

        auto clockInst = getInst("clock_main");
        if (clockInst) {
            bool cChange = false;
            if (!doc["clock_font"].isNull()) { clockInst->config.setInt("clock_font", doc["clock_font"].as<int>()); cChange = true; }
            if (!doc["clock_size"].isNull()) { clockInst->config.setInt("clock_size", doc["clock_size"].as<int>()); cChange = true; }
            if (!doc["clock_offset_x"].isNull()) { clockInst->config.setInt("clock_offset_x", doc["clock_offset_x"].as<int>()); cChange = true; }
            if (!doc["clock_offset_y"].isNull()) { clockInst->config.setInt("clock_offset_y", doc["clock_offset_y"].as<int>()); cChange = true; }
            if (!doc["clock_color_1"].isNull()) { clockInst->config.setString("clock_color_1", doc["clock_color_1"].as<String>()); cChange = true; }
            if (!doc["clock_color_2"].isNull()) { clockInst->config.setString("clock_color_2", doc["clock_color_2"].as<String>()); cChange = true; }
            if (!doc["clock_font_path"].isNull()) { clockInst->config.setString("clock_font_path", doc["clock_font_path"].as<String>()); cChange = true; }
            if (!doc["clock_theme"].isNull()) { clockInst->config.setInt("clock_theme", doc["clock_theme"].as<int>()); cChange = true; }
            
            if (cChange && !willReboot && rotationManager) {
                rotationManager->notifyConfigChanged("clock_main");
            }
        }

        auto dateInst = getInst("date_main");
        if (dateInst) {
            bool dChange = false;
            if (!doc["date_font"].isNull()) { dateInst->config.setInt("date_font", doc["date_font"].as<int>()); dChange = true; }
            if (!doc["date_size"].isNull()) { dateInst->config.setInt("date_size", doc["date_size"].as<int>()); dChange = true; }
            if (!doc["date_offset_x"].isNull()) { dateInst->config.setInt("date_offset_x", doc["date_offset_x"].as<int>()); dChange = true; }
            if (!doc["date_offset_y"].isNull()) { dateInst->config.setInt("date_offset_y", doc["date_offset_y"].as<int>()); dChange = true; }
            if (!doc["date_format"].isNull()) { dateInst->config.setString("format", doc["date_format"].as<String>()); dChange = true; }
            if (!doc["date_sprite"].isNull()) { dateInst->config.setString("background_sprite", doc["date_sprite"].as<String>()); dChange = true; }
            if (!doc["date_color_1"].isNull()) { dateInst->config.setString("date_color_1", doc["date_color_1"].as<String>()); dChange = true; }
            if (!doc["date_color_2"].isNull()) { dateInst->config.setString("date_color_2", doc["date_color_2"].as<String>()); dChange = true; }
            if (!doc["date_font_path"].isNull()) { dateInst->config.setString("date_font_path", doc["date_font_path"].as<String>()); dChange = true; }
            if (!doc["date_theme"].isNull()) { dateInst->config.setInt("theme", doc["date_theme"].as<int>()); dChange = true; }
            
            if (dChange && !willReboot && rotationManager) {
                rotationManager->notifyConfigChanged("date_main");
            }
        }

        auto weatherInst = getInst("weather_main");
        if (weatherInst) {
            bool wChange = false;
            if (!doc["weather_api_key"].isNull()) { weatherInst->config.setString("api_key", doc["weather_api_key"].as<String>()); wChange = true; }
            if (!doc["weather_city"].isNull()) { weatherInst->config.setString("city", doc["weather_city"].as<String>()); wChange = true; }
            if (!doc["weather_lang"].isNull()) { weatherInst->config.setString("lang", doc["weather_lang"].as<String>()); wChange = true; }
            if (!doc["weather_offset_x"].isNull()) { weatherInst->config.setInt("weather_offset_x", doc["weather_offset_x"].as<int>()); wChange = true; }
            if (!doc["weather_offset_y"].isNull()) { weatherInst->config.setInt("weather_offset_y", doc["weather_offset_y"].as<int>()); wChange = true; }
            
            if (wChange && !willReboot && rotationManager) {
                rotationManager->notifyConfigChanged("weather_main");
            }
        }

        if (!doc["night_mode_enabled"].isNull()) config.system.night_mode_enabled = doc["night_mode_enabled"].as<bool>();
        if (!doc["turn_off_at"].isNull()) config.system.turn_off_at = doc["turn_off_at"].as<String>();
        if (!doc["wake_up_at"].isNull()) config.system.wake_up_at = doc["wake_up_at"].as<String>();
        if (!doc["night_brightness"].isNull()) config.system.night_brightness = doc["night_brightness"].as<int>();
        if (!doc["idle_fighter_enabled"].isNull()) config.system.idle_fighter_enabled = doc["idle_fighter_enabled"].as<bool>();
        if (!doc["idle_fighter_interval"].isNull()) config.system.idle_fighter_interval = doc["idle_fighter_interval"].as<int>();

        if (!doc["timezone"].isNull()) {
            config.system.timezone = doc["timezone"].as<String>();
            configTzTime(getPosixTimezone(config.system.timezone).c_str(), "pool.ntp.org");
        }
        if (!doc["format_24h"].isNull()) config.system.format24h = doc["format_24h"].as<bool>();

        if (!doc["wifi_ssid"].isNull()) config.wifi.ssid = doc["wifi_ssid"].as<String>();
        if (!doc["wifi_password"].isNull() && doc["wifi_password"].as<String>() != "") config.wifi.password = doc["wifi_password"].as<String>();
        if (!doc["wifi_hostname"].isNull()) config.wifi.hostname = doc["wifi_hostname"].as<String>();

        bool mqttStateChanged = false;
        if (!doc["mqtt_enabled"].isNull()) {
            bool newMqtt = doc["mqtt_enabled"].as<bool>();
            if (newMqtt != config.mqtt.enabled) mqttStateChanged = true;
            config.mqtt.enabled = newMqtt;
        }
        if (!doc["mqtt_broker"].isNull()) {
            String newBroker = doc["mqtt_broker"].as<String>();
            if (newBroker != config.mqtt.broker) mqttStateChanged = true;
            config.mqtt.broker = newBroker;
        }
        if (!doc["mqtt_port"].isNull()) {
            int newPort = doc["mqtt_port"].as<int>();
            if (newPort != config.mqtt.port) mqttStateChanged = true;
            config.mqtt.port = newPort;
        }
        if (!doc["mqtt_user"].isNull()) config.mqtt.user = doc["mqtt_user"].as<String>();
        if (!doc["mqtt_pass"].isNull()) config.mqtt.pass = doc["mqtt_pass"].as<String>();
        if (!doc["mqtt_topic_bato"].isNull()) config.mqtt.topic_batocera = doc["mqtt_topic_bato"].as<String>();
        if (!doc["mqtt_topic_recal"].isNull()) config.mqtt.topic_recalbox = doc["mqtt_topic_recal"].as<String>();
        if (!doc["mqtt_device"].isNull()) config.mqtt.deviceName = doc["mqtt_device"].as<String>();

        if (mqttStateChanged) {
            willReboot = true;
        }

        // Sanitize all instances before persisting
        ConfigSanitizer::sanitizeInstances(config);
        config.saveToSD("/config.json");

        if (rotationManager && !willReboot) {
            if (cryptoInst) rotationManager->notifyConfigChanged("crypto_main");
            if (stockInst) rotationManager->notifyConfigChanged("stock_main");
            if (fighterInst) rotationManager->notifyConfigChanged("fighter_main");
        }

        if (willReboot) {
            request->send(200, "application/json", "{\"status\":\"rebooting\"}");
            delay(500);
            ESP.restart();
        } else {
            request->send(200, "application/json", "{\"status\":\"success\"}");
        }
    });
  server.addHandler(settingsHandler);
    
    // API: Get Selected GIF Playlist
    server.on("/api/playlists/selected", HTTP_GET, [](AsyncWebServerRequest *request){
        bool exists = false;
        String content = "";
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            FsFile f = sd.open("/playlists_selected.json", FILE_OPEN_READ);
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
            request->send(200, "application/json", "{\"playlists\":[]}");
        }
    });
    
    // API: Save Selected GIF Playlists — write directly to SD


    // API: Send Marquee Message
    AsyncCallbackJsonWebHandler* msgHandler = new AsyncCallbackJsonWebHandler("/api/message", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        
        MessageConfig cfg;
        cfg.text = doc["text"] | "Hello";
        
        if (doc["color"].is<const char*>()) {
            const char* hex = doc["color"].as<const char*>();
            if (hex[0] == '#') hex++;
            long val = strtol(hex, NULL, 16);
            extern MatrixEngine matrixEngine;
            cfg.color = matrixEngine.getDisplay() ? matrixEngine.getDisplay()->color565((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF) : 0xFFFF;
        } else {
            cfg.color = doc["color"] | 63488;
        }
        
        cfg.size = doc["size"] | 2;
        
        String dir = doc["direction"] | "rtl";
        if (dir == "left") dir = "rtl";
        else if (dir == "right") dir = "ltr";
        else if (dir == "down") dir = "ttb";
        else if (dir == "up") dir = "btt";
        cfg.direction = dir;
        
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
        
        int themeId = doc["clock_theme"] | doc["characterId"] | 0;
        extern ConfigLoader config;
        auto clockInst = config.getInstance("clock_main");
        if (clockInst) clockInst->config.setInt("clock_theme", themeId);
        if (rotationManager) {
            rotationManager->notifyConfigChanged("clock_main");
        }
            bool saved = config.saveToSD("/config.json");
            if (!saved) {
                request->send(200, "application/json", "{\"success\":true,\"sd_saved\":false,\"warning\":\"Theme applied but could not be saved to the SD card - it will revert on reboot.\"}");
                return;
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
            config.matrix.matrix_power = doc["state"].as<bool>();
        }
        DynamicJsonDocument resp(1024);
        resp["status"] = "success";
        resp["matrix_power"] = config.matrix.matrix_power;
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
    server.on("/api/system/restart_app", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        delay(500);
        ESP.restart(); // On ESP32, "Restart App" is equivalent to a full reboot
    });
    
    // API: OTA Firmware Update
    server.on("/api/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);

        if (shouldReboot) {
            LOGI("OTA", "OTA Update successful! Rebooting in 1 second...");
            xTaskCreate([](void *param) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                extern MatrixEngine matrixEngine;
                if (matrixEngine.getDisplay()) {
                    matrixEngine.getDisplay()->fillScreen(0);
                    matrixEngine.getDisplay()->flipDMABuffer();
                    matrixEngine.getDisplay()->fillScreen(0);
                    matrixEngine.getDisplay()->flipDMABuffer();
                }
                ESP.restart();
            }, "ota_reboot", 2048, NULL, 1, NULL);
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!index) {
            LOGI("OTA", "Update Start: %s", filename.c_str());
            extern GifEngine* gifEngine;
            if (gifEngine) gifEngine->stop();
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
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
                LOGI("OTA", "Update Success: %uB written.", index + len);
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
        bool saved = config.saveToSD("/config.json");

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
