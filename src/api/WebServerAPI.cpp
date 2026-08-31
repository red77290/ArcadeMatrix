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
#include "../core/AudioHub.h"
#include "../services/WebRadioService.h"
#include "../services/BluetoothAudioService.h"
#include "../services/DLNAService.h"
#include "../core/DisplayOrientationManager.h"
#include "../hal/GyroHAL.h"

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
                if (strlen(field.options) > 0) {
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
        
        bool found = false;
        String oldEngineId = "";
        {
            EngineInstanceSnapshot snap;
            if (config.getInstanceSnapshot(instanceId, snap)) {
                found = true;
                oldEngineId = snap.engine_id;
            }
        }
        bool isNew = !found;
        String targetEngineId = engineId.isEmpty() ? (found ? oldEngineId : instanceId) : engineId;

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
        
        DictionaryEngineConfig activeConfig;
        config.mutate([&](ConfigLoader& cfg) {
            if (isNew) {
                cfg.instances.push_back({instanceId, targetEngineId, {}});
            }
            for (auto& inst : cfg.instances) {
                if (inst.instance_id == instanceId) {
                    if (doc.containsKey("engine_id") && !engineId.isEmpty()) {
                        inst.engine_id = engineId;
                    }
                    if (doc.containsKey("config") && doc["config"].is<JsonObject>()) {
                        JsonObject cfgObj = doc["config"].as<JsonObject>();
                        for (JsonPair kv : cfgObj) {
                            inst.config.setString(kv.key().c_str(), kv.value().as<String>());
                        }
                    }
                    activeConfig = inst.config;
                    break;
                }
            }
        });
        
        bool structuralChange = isNew || (oldEngineId != targetEngineId);

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
        
        if (targetEngineId == "audiovisualizer" && visualizer) {
            visualizer->onConfigChanged(&activeConfig);
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

    // API: System Info & Stats (Dashboard metrics compatibility)
    auto sendSysStats = [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(1024);
        float tempC = 0.0f;
        float humidity = 0.0f;
        if (hardwareHAL.capabilities().hasTempSensor) {
            EnvironmentData env = hardwareHAL.readEnvironment();
            tempC = env.temperatureC;
            humidity = env.humidity;
        } else {
            tempC = temperatureRead();
        }
        
        doc["cpu_load"] = 0.0f;
        doc["temperature_c"] = tempC;
        doc["humidity"] = humidity;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["free_heap_kb"] = ESP.getFreeHeap() / 1024;
        doc["total_heap_kb"] = ESP.getHeapSize() / 1024;
        doc["ram_used_mb"] = (float)(ESP.getHeapSize() - ESP.getFreeHeap()) / (1024.0f * 1024.0f);
        doc["ram_total_mb"] = (float)ESP.getHeapSize() / (1024.0f * 1024.0f);
        doc["psram_found"] = hardwareHAL.capabilities().hasPsram;
        doc["psram_free_mb"] = (float)ESP.getFreePsram() / (1024.0f * 1024.0f);
        doc["psram_total_mb"] = (float)ESP.getPsramSize() / (1024.0f * 1024.0f);
        doc["disk_free_gb"] = 0.0f;
        doc["uptime_sec"] = millis() / 1000;
        doc["has_temp_sensor"] = hardwareHAL.capabilities().hasTempSensor;
        doc["has_microphone"] = hardwareHAL.capabilities().hasMicrophone;
        doc["has_dac"] = hardwareHAL.capabilities().audio.output;
        doc["has_gyro"] = gyroHAL.isAvailable();
        doc["gyro_sensor"] = gyroHAL.getOrientation().sensorName;
        doc["hardware_profile"] = "Waveshare ESP32-S3 RGB Matrix";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    };
    server.on("/api/system_info", HTTP_GET, sendSysStats);
    server.on("/api/stats", HTTP_GET, sendSysStats);

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
        config.mutate([&](ConfigLoader& cfg) {
            for (auto& inst : cfg.instances) {
                if (inst.instance_id == "visualizer_main") {
                    if (!doc["enabled"].isNull()) {
                        inst.config.setBool("enabled", (bool)doc["enabled"]);
                        if (visualizer) {
                            if (inst.config.getBool("enabled")) visualizer->activate();
                            else visualizer->deactivate();
                        }
                    }
                    if (!doc["style"].isNull()) {
                        inst.config.setString("style", (const char*)doc["style"]);
                    }
                    if (!doc["mode"].isNull()) {
                        inst.config.setString("style", (const char*)doc["mode"]);
                    }
                    if (visualizer) {
                        visualizer->onConfigChanged(&inst.config);
                    }
                    if (!doc["gain"].isNull()) {
                        float g = (float)doc["gain"];
                        inst.config.setString("gain", String(g));
                        hardwareHAL.setMicGain(g);
                    }
                    if (!doc["sensitivity"].isNull()) {
                        int s = (int)doc["sensitivity"];
                        inst.config.setString("sensitivity", String(s));
                    }
                    break;
                }
            }
        });
        config.saveToSD("/config.json");
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(visHandler);

    // API: List GIF Playlists (Direct SD streaming with zero heap allocation, falls back to dynamic directory scan)
    server.on("/api/playlists", HTTP_GET, [](AsyncWebServerRequest *request){
        String reqType = "";
        if (request->hasParam("type")) reqType = request->getParam("type")->value();
        else if (request->hasParam("folder") && request->getParam("folder")->value().indexOf("tate") != -1) reqType = "tate";

        auto readOrScan = [](const String& rootDir) -> String {
            String jsonPath = rootDir + "/playlists.json";
            if (!sd.exists(jsonPath.c_str()) && rootDir.startsWith("/")) {
                jsonPath = rootDir.substring(1) + "/playlists.json";
            }
            if (sd.exists(jsonPath.c_str())) {
                FsFile f = sd.open(jsonPath.c_str(), FILE_OPEN_READ);
                if (f) {
                    size_t sz = f.size();
                    if (sz > 0 && sz < 65536) {
                        char* buf = (char*)malloc(sz + 1);
                        if (buf) {
                            size_t n = f.read((uint8_t*)buf, sz);
                            buf[n] = '\0';
                            f.close();
                            char* jsonStart = buf;
                            while (*jsonStart && *jsonStart != '{') jsonStart++;
                            char* jsonEnd = buf + n - 1;
                            while (jsonEnd > jsonStart && *jsonEnd != '}') jsonEnd--;
                            if (*jsonStart == '{' && *jsonEnd == '}' && jsonEnd > jsonStart) {
                                *(jsonEnd + 1) = '\0';
                                String s(jsonStart);
                                free(buf);
                                return s;
                            }
                            free(buf);
                        } else {
                            f.close();
                        }
                    } else {
                        f.close();
                    }
                }
            }
            // Fallback directory scan
            String content = "{";
            bool first = true;
            FsFile dir = sd.open(rootDir.c_str(), FILE_OPEN_READ);
            if (!dir || !isDirectory(dir)) {
                if (rootDir.startsWith("/")) dir = sd.open(rootDir.substring(1).c_str(), FILE_OPEN_READ);
            }
            if (dir && isDirectory(dir)) {
                FsFile file;
                while (getNextFile(dir, file)) {
                    if (isDirectory(file)) {
                        String name = getFileName(file);
                        int lastSlash = name.lastIndexOf('/');
                        if (lastSlash >= 0) name = name.substring(lastSlash + 1);
                        if (name.length() > 0 && !isMacJunk(name)) {
                            int count = 0;
                            String indexPath = rootDir + "/" + name + "/index.txt";
                            if (!sd.exists(indexPath.c_str()) && rootDir.startsWith("/")) {
                                indexPath = rootDir.substring(1) + "/" + name + "/index.txt";
                            }
                            if (sd.exists(indexPath.c_str())) {
                                FsFile idx = sd.open(indexPath.c_str(), FILE_OPEN_READ);
                                if (idx) {
                                    while (idx.available()) {
                                        String l = idx.readStringUntil('\n');
                                        l.trim();
                                        if (l.length() > 0 && !isMacJunk(l)) count++;
                                    }
                                    idx.close();
                                }
                            }
                            if (!first) content += ",";
                            content += "\"" + name + "\":{\"path\":\"" + rootDir + "/" + name + "\",\"count\":" + String(count) + "}";
                            first = false;
                        }
                    }
                }
                dir.close();
            }
            content += "}";
            return content;
        };

        if (reqType.equalsIgnoreCase("tate")) {
            String tateContent = "{}";
            if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
                tateContent = readOrScan("/gifs_tate");
                if (tateContent == "{}" && (sd.exists("/gifs/tate") || sd.exists("gifs/tate"))) {
                    tateContent = readOrScan("/gifs/tate");
                }
                if (tateContent == "{}" && (sd.exists("/tate") || sd.exists("tate"))) {
                    tateContent = readOrScan("/tate");
                }
                xSemaphoreGive(sdMutex);
            }
            request->send(200, "application/json", tateContent);
            return;
        } else if (reqType.equalsIgnoreCase("yoko") || reqType.equalsIgnoreCase("horizontal")) {
            String yokoContent = "{}";
            if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
                yokoContent = readOrScan("/gifs");
                xSemaphoreGive(sdMutex);
            }
            request->send(200, "application/json", yokoContent);
            return;
        }

        String jsonCombined = "{\"yoko\":{},\"tate\":{}}";
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            String yoko = readOrScan("/gifs");
            String tate = readOrScan("/gifs_tate");
            if (tate == "{}" && (sd.exists("/gifs/tate") || sd.exists("gifs/tate"))) {
                tate = readOrScan("/gifs/tate");
            }
            if (tate == "{}" && (sd.exists("/tate") || sd.exists("tate"))) {
                tate = readOrScan("/tate");
            }
            xSemaphoreGive(sdMutex);
            jsonCombined = "{\"yoko\":" + yoko + ",\"tate\":" + tate + "}";
        }
        request->send(200, "application/json", jsonCombined);
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

        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const ConfigSnapshot& snap = guard.get();

        // Matrix
        doc["brightness_limit"] = snap.matrix.powerLimitPercent;
        doc["color_depth"] = snap.matrix.colorDepth;
        doc["matrix_chain"] = snap.matrix.chainLength;
        doc["matrix_rows"] = snap.matrix.height;
        doc["matrix_cols"] = snap.matrix.width;
        doc["matrix_rgb_sequence"] = snap.matrix.rgbSequence;
        doc["matrix_force_single_buffer"] = snap.matrix.forceSingleBuffer;
        doc["matrix_driver_chip"] = snap.matrix.driverChip;
        doc["matrix_clk_phase"] = snap.matrix.clkPhase;
        doc["matrix_latch_blanking"] = snap.matrix.latchBlanking;
        doc["matrix_row_address_mode"] = snap.matrix.rowAddressMode;
        doc["matrix_limit_refresh_rate_hz"] = snap.matrix.limitRefreshRateHz;
        doc["rotation_offset"] = snap.matrix.rotation_offset;
        doc["auto_rotate"] = snap.matrix.auto_rotate;
        doc["rotation_transition"] = snap.matrix.rotation_transition;
        doc["rotation_transition_duration_ms"] = snap.matrix.rotation_transition_duration_ms;

        auto getInst = [&](const String& id) { return snap.getInstance(id); };
        
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
        for (const auto& r : snap.rotation) rotStr += r.instance_id + ",";
        if (rotStr.endsWith(",")) rotStr.remove(rotStr.length()-1);
        doc["rotation"] = rotStr;
        
        auto getRot = [&](const String& id) {
            for (const auto& r : snap.rotation) if (r.instance_id == id) return r.duration_sec;
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
        doc["temp_unit"] = snap.system.unit;
        doc["temp_offset"] = snap.system.temp_offset;
        
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
        doc["lang"] = snap.system.lang;
        doc["timezone"] = snap.system.timezone;
        doc["format_24h"] = snap.system.format24h;

        // Standby
        doc["night_mode_enabled"] = snap.system.night_mode_enabled;
        doc["turn_off_at"] = snap.system.turn_off_at;
        doc["wake_up_at"] = snap.system.wake_up_at;
        doc["night_brightness"] = snap.system.night_brightness;
        doc["idle_fighter_enabled"] = snap.system.idle_fighter_enabled;
        doc["idle_fighter_interval"] = snap.system.idle_fighter_interval;
        doc["matrix_power"] = snap.matrix.matrix_power;

        // WiFi
        doc["wifi_ssid"] = snap.wifi.ssid;
        doc["wifi_hostname"] = snap.wifi.hostname;

        // MQTT
        doc["mqtt_enabled"] = snap.mqtt.enabled;
        doc["mqtt_broker"] = snap.mqtt.broker;
        doc["mqtt_port"] = snap.mqtt.port;
        doc["mqtt_user"] = snap.mqtt.user;
        doc["mqtt_pass"] = snap.mqtt.pass;
        doc["mqtt_topic_bato"] = snap.mqtt.topic_batocera;
        doc["mqtt_topic_recal"] = snap.mqtt.topic_recalbox;
        doc["mqtt_device"] = snap.mqtt.deviceName;

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
        bool willReboot = (!doc["reboot"].isNull() && doc["reboot"].as<bool>());
        bool cryptoChanged = false;
        bool stockChanged = false;
        bool fighterChanged = false;

        config.mutate([&](ConfigLoader& cfg) {
            // Matrix
            if (!doc["brightness_limit"].isNull()) {
                cfg.matrix.powerLimitPercent = doc["brightness_limit"].as<int>();
                extern MatrixEngine matrixEngine;
                matrixEngine.setBrightness(cfg.matrix.powerLimitPercent);
            }
            if (!doc["color_depth"].isNull()) cfg.matrix.colorDepth = doc["color_depth"].as<int>();
            if (!doc["matrix_chain"].isNull()) cfg.matrix.chainLength = doc["matrix_chain"].as<int>();
            if (!doc["matrix_rows"].isNull()) cfg.matrix.height = doc["matrix_rows"].as<int>();
            if (!doc["matrix_cols"].isNull()) cfg.matrix.width = doc["matrix_cols"].as<int>();
            if (!doc["matrix_rgb_sequence"].isNull()) cfg.matrix.rgbSequence = doc["matrix_rgb_sequence"].as<String>();
            if (!doc["matrix_force_single_buffer"].isNull()) cfg.matrix.forceSingleBuffer = doc["matrix_force_single_buffer"].as<bool>();
            if (!doc["matrix_limit_refresh_rate_hz"].isNull()) cfg.matrix.limitRefreshRateHz = doc["matrix_limit_refresh_rate_hz"].as<int>();
            if (!doc["matrix_driver_chip"].isNull()) cfg.matrix.driverChip = doc["matrix_driver_chip"].as<String>();
            if (!doc["matrix_clk_phase"].isNull()) cfg.matrix.clkPhase = doc["matrix_clk_phase"].as<bool>();
            if (!doc["matrix_latch_blanking"].isNull()) cfg.matrix.latchBlanking = doc["matrix_latch_blanking"].as<int>();
            if (!doc["matrix_row_address_mode"].isNull()) cfg.matrix.rowAddressMode = doc["matrix_row_address_mode"].as<int>();
            if (!doc["rotation_offset"].isNull()) {
                cfg.matrix.rotation_offset = doc["rotation_offset"].as<int>();
                displayOrientationManager.setRotationOffset(cfg.matrix.rotation_offset);
            }
            if (!doc["auto_rotate"].isNull()) {
                cfg.matrix.auto_rotate = doc["auto_rotate"].as<bool>();
            }
            if (!doc["rotation_transition"].isNull()) {
                cfg.matrix.rotation_transition = doc["rotation_transition"].as<String>();
                displayOrientationManager.setTransitionEffect(cfg.matrix.rotation_transition);
            }
            if (!doc["rotation_transition_duration_ms"].isNull()) {
                cfg.matrix.rotation_transition_duration_ms = doc["rotation_transition_duration_ms"].as<int>();
                displayOrientationManager.setTransitionDuration(cfg.matrix.rotation_transition_duration_ms);
            }

            auto getInst = [&](const String& id) -> EngineInstance* {
                for (auto& inst : cfg.instances) {
                    if (inst.instance_id == id) return &inst;
                }
                return nullptr;
            };

            auto cryptoInst = getInst("crypto_main");
            if (cryptoInst) {
                if (!doc["crypto_enabled"].isNull()) cryptoInst->config.setBool("enabled", doc["crypto_enabled"].as<bool>());
                if (!doc["crypto_symbols"].isNull()) cryptoInst->config.setString("symbols", doc["crypto_symbols"].as<String>());
                if (!doc["crypto_duration_sec"].isNull()) cryptoInst->config.setInt("duration_sec", doc["crypto_duration_sec"].as<int>());
                if (!doc["crypto_cache_ttl_min"].isNull()) cryptoInst->config.setInt("cache_ttl_min", doc["crypto_cache_ttl_min"].as<int>());
                if (!doc["crypto_currency"].isNull()) cryptoInst->config.setString("currency", doc["crypto_currency"].as<String>());
                cryptoChanged = true;
            }

            auto stockInst = getInst("stock_main");
            if (stockInst) {
                if (!doc["stock_enabled"].isNull()) stockInst->config.setBool("enabled", doc["stock_enabled"].as<bool>());
                if (!doc["stock_symbols"].isNull()) stockInst->config.setString("symbols", doc["stock_symbols"].as<String>());
                if (!doc["stock_duration_sec"].isNull()) stockInst->config.setInt("duration_sec", doc["stock_duration_sec"].as<int>());
                if (!doc["stock_cache_ttl_min"].isNull()) stockInst->config.setInt("cache_ttl_min", doc["stock_cache_ttl_min"].as<int>());
                stockChanged = true;
            }

            auto setRot = [&](const String& id, int dur) {
                for (auto& r : cfg.rotation) if (r.instance_id == id) { r.duration_sec = dur; return; }
            };
            if (!doc["clock_duration_sec"].isNull()) setRot("clock_main", doc["clock_duration_sec"].as<int>());
            if (!doc["date_duration_sec"].isNull()) setRot("date_main", doc["date_duration_sec"].as<int>());
            if (!doc["weather_duration_sec"].isNull()) setRot("weather_main", doc["weather_duration_sec"].as<int>());
            if (!doc["temp_duration_sec"].isNull()) setRot("temp_main", doc["temp_duration_sec"].as<int>());
            if (!doc["decibel_duration_sec"].isNull()) setRot("decibel_main", doc["decibel_duration_sec"].as<int>());
            
            auto fighterInst = getInst("fighter_main");
            if (fighterInst) {
                if (!doc["fighter_interval_sec"].isNull()) fighterInst->config.setInt("fighter_interval_sec", doc["fighter_interval_sec"].as<int>());
                fighterChanged = true;
            }

            if (!doc["temp_unit"].isNull()) cfg.system.unit = doc["temp_unit"].as<String>();
            if (!doc["temp_offset"].isNull()) cfg.system.temp_offset = doc["temp_offset"].as<float>();

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

            if (!doc["lang"].isNull()) {
                String newLang = doc["lang"].as<String>();
                if (newLang != cfg.system.lang) {
                    cfg.system.lang = newLang;
                    if (rotationManager) {
                        for (const auto& inst : cfg.instances) {
                            rotationManager->notifyConfigChanged(inst.instance_id);
                        }
                    }
                }
            }
            if (!doc["night_mode_enabled"].isNull()) cfg.system.night_mode_enabled = doc["night_mode_enabled"].as<bool>();
            if (!doc["turn_off_at"].isNull()) cfg.system.turn_off_at = doc["turn_off_at"].as<String>();
            if (!doc["wake_up_at"].isNull()) cfg.system.wake_up_at = doc["wake_up_at"].as<String>();
            if (!doc["night_brightness"].isNull()) cfg.system.night_brightness = doc["night_brightness"].as<int>();
            if (!doc["idle_fighter_enabled"].isNull()) cfg.system.idle_fighter_enabled = doc["idle_fighter_enabled"].as<bool>();
            if (!doc["idle_fighter_interval"].isNull()) cfg.system.idle_fighter_interval = doc["idle_fighter_interval"].as<int>();

            if (!doc["timezone"].isNull()) {
                cfg.system.timezone = doc["timezone"].as<String>();
                configTzTime(getPosixTimezone(cfg.system.timezone).c_str(), "pool.ntp.org");
            }
            if (!doc["format_24h"].isNull()) cfg.system.format24h = doc["format_24h"].as<bool>();

            if (!doc["wifi_ssid"].isNull()) cfg.wifi.ssid = (const char*)doc["wifi_ssid"];
            if (!doc["wifi_password"].isNull() && String((const char*)doc["wifi_password"]) != "") cfg.wifi.password = (const char*)doc["wifi_password"];
            if (!doc["wifi_hostname"].isNull()) cfg.wifi.hostname = (const char*)doc["wifi_hostname"];

            if (!doc["mqtt_enabled"].isNull()) {
                bool newMqtt = (bool)doc["mqtt_enabled"];
                if (newMqtt != cfg.mqtt.enabled) willReboot = true;
                cfg.mqtt.enabled = newMqtt;
            }
            if (!doc["mqtt_broker"].isNull()) {
                String newBroker = (const char*)doc["mqtt_broker"];
                if (newBroker != cfg.mqtt.broker) willReboot = true;
                cfg.mqtt.broker = newBroker;
            }
            if (!doc["mqtt_port"].isNull()) {
                int newPort = (int)doc["mqtt_port"];
                if (newPort != cfg.mqtt.port) willReboot = true;
                cfg.mqtt.port = newPort;
            }
            if (!doc["mqtt_user"].isNull()) cfg.mqtt.user = (const char*)doc["mqtt_user"];
            if (!doc["mqtt_pass"].isNull()) cfg.mqtt.pass = (const char*)doc["mqtt_pass"];
            if (!doc["mqtt_topic_bato"].isNull()) cfg.mqtt.topic_batocera = (const char*)doc["mqtt_topic_bato"];
            if (!doc["mqtt_topic_recal"].isNull()) cfg.mqtt.topic_recalbox = (const char*)doc["mqtt_topic_recal"];
            if (!doc["mqtt_device"].isNull()) cfg.mqtt.deviceName = (const char*)doc["mqtt_device"];
        });

        // Sanitize all instances before persisting
        ConfigSanitizer::sanitizeInstances(config);
        config.saveToSD("/config.json");

        if (rotationManager && !willReboot) {
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            for (const auto& inst : guard->instances) {
                rotationManager->notifyConfigChanged(inst.instance_id);
            }
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
        config.mutate([&](ConfigLoader& cfg) {
            for (auto& inst : cfg.instances) {
                if (inst.instance_id == "clock_main") {
                    inst.config.setInt("clock_theme", themeId);
                    break;
                }
            }
        });
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
            config.mutate([&](ConfigLoader& cfg) {
                cfg.matrix.matrix_power = doc["state"].as<bool>();
            });
        }
        DynamicJsonDocument resp(1024);
        resp["status"] = "success";
        resp["matrix_power"] = config.acquireSnapshot()->matrix.matrix_power;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });
    server.addHandler(powerHandler);

    // API: System settings (GET /api/system)
    server.on("/api/system", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const ConfigSnapshot& snap = guard.get();
        DynamicJsonDocument doc(4096);
        JsonObject sys = doc.createNestedObject("system");
        sys["lang"] = snap.system.lang.length() > 0 ? snap.system.lang : "fr";
        sys["timezone"] = snap.system.timezone;
        sys["format_24h"] = snap.system.format24h;
        sys["unit"] = snap.system.unit;
        sys["temp_offset"] = snap.system.temp_offset;
        sys["night_mode_enabled"] = snap.system.night_mode_enabled;
        sys["turn_off_at"] = snap.system.turn_off_at;
        sys["wake_up_at"] = snap.system.wake_up_at;
        sys["night_brightness"] = snap.system.night_brightness;
        sys["day_brightness"] = snap.matrix.powerLimitPercent;
        sys["brightness_limit"] = snap.matrix.powerLimitPercent;
        sys["idle_fighter_enabled"] = snap.system.idle_fighter_enabled;
        sys["idle_fighter_interval"] = snap.system.idle_fighter_interval;

        JsonObject mat = doc.createNestedObject("matrix");
        mat["height"] = snap.matrix.height;
        mat["width"] = snap.matrix.width;
        mat["chain_length"] = snap.matrix.chainLength;
        mat["parallel"] = 1;
        mat["driver_chip"] = snap.matrix.driverChip;
        mat["row_address_mode"] = snap.matrix.rowAddressMode;
        mat["multiplexing"] = 0;
        mat["mapping"] = "regular";
        mat["rgb_sequence"] = snap.matrix.rgbSequence;
        mat["slowdown"] = 1;
        mat["pwm_bits"] = snap.matrix.colorDepth;
        mat["pwm_lsb_nanoseconds"] = 130;
        mat["disable_hardware_pulsing"] = false;
        mat["limit_refresh_rate_hz"] = snap.matrix.limitRefreshRateHz;
        mat["clk_phase"] = snap.matrix.clkPhase;
        mat["latch_blanking"] = snap.matrix.latchBlanking;
        mat["rotation_offset"] = snap.matrix.rotation_offset;
        mat["auto_rotate"] = snap.matrix.auto_rotate;
        mat["rotation_transition"] = snap.matrix.rotation_transition;
        mat["rotation_transition_duration_ms"] = snap.matrix.rotation_transition_duration_ms;

        JsonObject mqtt = doc.createNestedObject("mqtt");
        mqtt["enabled"] = snap.mqtt.enabled;
        mqtt["broker"] = snap.mqtt.broker;
        mqtt["port"] = snap.mqtt.port;
        mqtt["user"] = snap.mqtt.user;
        mqtt["pass"] = snap.mqtt.pass;
        mqtt["topic_batocera"] = snap.mqtt.topic_batocera;
        mqtt["topic_recalbox"] = snap.mqtt.topic_recalbox;
        mqtt["device_name"] = snap.mqtt.deviceName;

        JsonObject wifi = doc.createNestedObject("wifi");
        wifi["ssid"] = snap.wifi.ssid;
        wifi["hostname"] = snap.wifi.hostname;

        doc["api_auth_enabled"] = false;
        doc["api_token"] = "";

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: System settings update (POST /api/system)
    AsyncCallbackJsonWebHandler* sysHandler = new AsyncCallbackJsonWebHandler("/api/system", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        bool changed = false;
        bool willReboot = false;
        if (doc.containsKey("reboot") && doc["reboot"].as<bool>()) willReboot = true;
        bool langChanged = false;

        config.mutate([&](ConfigLoader& cfg) {
            JsonObject sys = doc.containsKey("system") ? doc["system"].as<JsonObject>() : doc;
            if (!sys["lang"].isNull()) {
                String newLang = sys["lang"].as<String>();
                if (newLang != cfg.system.lang) {
                    cfg.system.lang = newLang;
                    changed = true;
                    langChanged = true;
                }
            }
            if (!sys["timezone"].isNull()) {
                cfg.system.timezone = sys["timezone"].as<String>();
                configTzTime(getPosixTimezone(cfg.system.timezone).c_str(), "pool.ntp.org");
                changed = true;
            }
            if (!sys["format_24h"].isNull()) {
                cfg.system.format24h = sys["format_24h"].as<bool>();
                changed = true;
            }
            if (!sys["unit"].isNull()) {
                cfg.system.unit = sys["unit"].as<String>();
                changed = true;
            }
            if (!sys["temp_offset"].isNull()) {
                cfg.system.temp_offset = sys["temp_offset"].as<float>();
                changed = true;
            }
            if (!sys["night_mode_enabled"].isNull()) {
                cfg.system.night_mode_enabled = sys["night_mode_enabled"].as<bool>();
                changed = true;
            }
            if (!sys["turn_off_at"].isNull()) {
                cfg.system.turn_off_at = sys["turn_off_at"].as<String>();
                changed = true;
            }
            if (!sys["wake_up_at"].isNull()) {
                cfg.system.wake_up_at = sys["wake_up_at"].as<String>();
                changed = true;
            }
            if (!sys["night_brightness"].isNull()) {
                cfg.system.night_brightness = sys["night_brightness"].as<int>();
                changed = true;
            }
            if (!sys["brightness_limit"].isNull() || !sys["brightness"].isNull() || !sys["day_brightness"].isNull()) {
                int b = !sys["brightness_limit"].isNull() ? sys["brightness_limit"].as<int>() : (!sys["brightness"].isNull() ? sys["brightness"].as<int>() : sys["day_brightness"].as<int>());
                if (b < 1) b = 1;
                if (b > 100) b = 100;
                cfg.matrix.powerLimitPercent = b;
                extern MatrixEngine matrixEngine;
                matrixEngine.setBrightness(b);
                changed = true;
            }
            if (!sys["idle_fighter_enabled"].isNull()) {
                cfg.system.idle_fighter_enabled = sys["idle_fighter_enabled"].as<bool>();
                changed = true;
            }
            if (!sys["idle_fighter_interval"].isNull()) {
                cfg.system.idle_fighter_interval = sys["idle_fighter_interval"].as<int>();
                changed = true;
            }

            if (doc.containsKey("matrix")) {
                JsonObject mat = doc["matrix"].as<JsonObject>();
                if (!mat["height"].isNull()) cfg.matrix.height = mat["height"].as<int>();
                if (!mat["width"].isNull()) cfg.matrix.width = mat["width"].as<int>();
                if (!mat["chain_length"].isNull()) cfg.matrix.chainLength = mat["chain_length"].as<int>();
                if (!mat["driver_chip"].isNull()) cfg.matrix.driverChip = mat["driver_chip"].as<String>();
                if (!mat["row_address_mode"].isNull()) cfg.matrix.rowAddressMode = mat["row_address_mode"].as<int>();
                if (!mat["rgb_sequence"].isNull()) cfg.matrix.rgbSequence = mat["rgb_sequence"].as<String>();
                if (!mat["pwm_bits"].isNull()) cfg.matrix.colorDepth = mat["pwm_bits"].as<int>();
                else if (!mat["color_depth"].isNull()) cfg.matrix.colorDepth = mat["color_depth"].as<int>();
                if (!mat["limit_refresh_rate_hz"].isNull()) cfg.matrix.limitRefreshRateHz = mat["limit_refresh_rate_hz"].as<int>();
                if (!mat["clk_phase"].isNull()) cfg.matrix.clkPhase = mat["clk_phase"].as<bool>();
                else if (!mat["clkPhase"].isNull()) cfg.matrix.clkPhase = mat["clkPhase"].as<bool>();
                if (!mat["latch_blanking"].isNull()) cfg.matrix.latchBlanking = mat["latch_blanking"].as<int>();
                else if (!mat["latchBlanking"].isNull()) cfg.matrix.latchBlanking = mat["latchBlanking"].as<int>();
                if (!mat["rotation_offset"].isNull()) {
                    cfg.matrix.rotation_offset = mat["rotation_offset"].as<int>();
                    displayOrientationManager.setRotationOffset(cfg.matrix.rotation_offset);
                }
                if (!mat["auto_rotate"].isNull()) cfg.matrix.auto_rotate = mat["auto_rotate"].as<bool>();
                if (!mat["rotation_transition"].isNull()) {
                    cfg.matrix.rotation_transition = mat["rotation_transition"].as<String>();
                    displayOrientationManager.setTransitionEffect(cfg.matrix.rotation_transition);
                }
                if (!mat["rotation_transition_duration_ms"].isNull()) {
                    cfg.matrix.rotation_transition_duration_ms = mat["rotation_transition_duration_ms"].as<int>();
                    displayOrientationManager.setTransitionDuration(cfg.matrix.rotation_transition_duration_ms);
                }
                changed = true;
                willReboot = true;
            }

            if (doc.containsKey("mqtt")) {
                JsonObject mq = doc["mqtt"].as<JsonObject>();
                bool prevMqtt = cfg.mqtt.enabled;
                if (!mq["enabled"].isNull()) cfg.mqtt.enabled = mq["enabled"].as<bool>();
                if (!mq["broker"].isNull()) cfg.mqtt.broker = mq["broker"].as<String>();
                if (!mq["port"].isNull()) cfg.mqtt.port = mq["port"].as<int>();
                if (!mq["user"].isNull()) cfg.mqtt.user = mq["user"].as<String>();
                if (!mq["pass"].isNull()) cfg.mqtt.pass = mq["pass"].as<String>();
                if (!mq["topic_batocera"].isNull()) cfg.mqtt.topic_batocera = mq["topic_batocera"].as<String>();
                if (!mq["topic_recalbox"].isNull()) cfg.mqtt.topic_recalbox = mq["topic_recalbox"].as<String>();
                if (!mq["device_name"].isNull()) cfg.mqtt.deviceName = mq["device_name"].as<String>();
                changed = true;
                if (prevMqtt != cfg.mqtt.enabled) {
                    willReboot = true;
                }
            }
        });

        if (changed) {
            ConfigSanitizer::sanitize(config);
            config.saveToSD("/config.json");
        }

        if ((changed || langChanged) && rotationManager && !willReboot) {
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            for (const auto& inst : guard->instances) {
                rotationManager->notifyConfigChanged(inst.instance_id);
            }
        }

        DynamicJsonDocument resp(512);
        resp["status"] = willReboot ? "rebooting" : "success";
        resp["lang"] = config.acquireSnapshot()->system.lang;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);

        if (willReboot) {
            xTaskCreate([](void *param) {
                vTaskDelay(pdMS_TO_TICKS(500));
                ESP.restart();
            }, "config_reboot_task", 2048, NULL, 1, NULL);
        }
    });
    sysHandler->setFilter([](AsyncWebServerRequest *request) {
        return request->url() == "/api/system";
    });
    server.addHandler(sysHandler);
    
    // API: System commands (Reboot / Shutdown / Restart)
    server.on("/api/system/shutdown", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        xTaskCreate([](void *param) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }, "shutdown_task", 2048, NULL, 1, NULL);
    });
    server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        xTaskCreate([](void *param) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }, "reboot_task", 2048, NULL, 1, NULL);
    });
    server.on("/api/system/restart", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        xTaskCreate([](void *param) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }, "restart_task", 2048, NULL, 1, NULL);
    });
    server.on("/api/system/restart_app", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        xTaskCreate([](void *param) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }, "restart_app_task", 2048, NULL, 1, NULL);
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

    // API: MQTT SSH helpers (Parity stubs for ESP32)
    server.on("/api/mqtt/install", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":false,\"message\":\"SSH install is only available on Raspberry Pi. On ESP32, configure Batocera/Recalbox manually to send MQTT to this device's IP.\"}");
    });
    server.on("/api/mqtt/logs", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true,\"logs\":\"SSH logs are only available on Raspberry Pi.\"}");
    });

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

    // API: GET /api/audio/status — Returns current audio playback snapshot
    server.on("/api/audio/status", HTTP_GET, [](AsyncWebServerRequest *request){
        auto st = audioHub.getPlaybackStateSnapshot();
        DynamicJsonDocument doc(512);
        doc["source"] = AudioHub::getSourceName(st.source);
        doc["status"] = (int)st.status;
        doc["title"] = st.title;
        doc["artist"] = st.artist;
        doc["album"] = st.album;
        doc["duration_ms"] = st.durationMs;
        doc["position_ms"] = st.positionMs;
        doc["volume"] = st.volume;
        doc["artwork_id"] = st.artworkId;
        doc["generation"] = st.generation;
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // API: POST /api/audio/volume — Adjusts master volume (0-100%)
    AsyncCallbackJsonWebHandler* audioVolHandler = new AsyncCallbackJsonWebHandler("/api/audio/volume", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject obj = json.as<JsonObject>();
        if (!obj["volume"].isNull()) {
            uint8_t vol = obj["volume"].as<uint8_t>();
            audioHub.setVolume(vol);
        }
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(audioVolHandler);

    // API: POST /api/audio/radio — Controls WebRadio playback
    AsyncCallbackJsonWebHandler* radioHandler = new AsyncCallbackJsonWebHandler("/api/audio/radio", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject obj = json.as<JsonObject>();
        String url = obj["url"] | "";
        String name = obj["name"] | "Web Radio";
        if (url.isEmpty()) {
            webRadioService.stop();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Radio stopped\"}");
        } else {
            bool ok = webRadioService.play(url, name);
            request->send(ok ? 200 : 500, "application/json", ok ? "{\"success\":true}" : "{\"error\":\"Failed to connect to radio stream\"}");
        }
    });
    server.addHandler(radioHandler);

    // API: POST /api/audio/stop — Stops active audio stream
    server.on("/api/audio/stop", HTTP_POST, [](AsyncWebServerRequest *request){
        webRadioService.stop();
        bluetoothAudioService.stop();
        request->send(200, "application/json", "{\"success\":true}");
    });

    // API: POST /api/audio/test — Plays a short diagnostic test tone (880 Hz) on the onboard speaker
    server.on("/api/audio/test", HTTP_POST, [](AsyncWebServerRequest *request){
        audioOutputHAL.playSine(880.0f, 500);
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Test tone played\"}");
    });

    // API: GET /api/gyro/status — Returns gravity vector and suggested orientation
    server.on("/api/gyro/status", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(512);
        GyroOrientation orient = gyroHAL.getOrientation();
        doc["available"] = gyroHAL.isAvailable();
        doc["sensor"] = orient.sensorName;
        doc["ax"] = orient.ax;
        doc["ay"] = orient.ay;
        doc["az"] = orient.az;
        doc["gx"] = orient.gx;
        doc["gy"] = orient.gy;
        doc["gz"] = orient.gz;
        doc["suggested_rotation"] = orient.suggestedRotation;
        doc["rotation_offset"] = displayOrientationManager.getRotationOffset();
        doc["current_rotation"] = displayOrientationManager.getRotation();
        doc["transition_effect"] = RotationTransitionFX::effectToString(displayOrientationManager.getTransitionEffect());
        doc["transition_duration_ms"] = displayOrientationManager.getTransitionDuration();
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // API: POST /api/gyro/calibrate — Calibrates current physical position as 0° reference
    server.on("/api/gyro/calibrate", HTTP_POST, [](AsyncWebServerRequest *request){
        displayOrientationManager.calibrateZeroReference();
        extern ConfigLoader config;
        config.matrix.rotation_offset = displayOrientationManager.getRotationOffset();
        ConfigSanitizer::sanitize(config);
        config.saveToSD("/config.json");
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        doc["rotation_offset"] = displayOrientationManager.getRotationOffset();
        doc["current_rotation"] = displayOrientationManager.getRotation();
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // API: POST /api/display/test-transition — Triggers a preview of the rotation transition FX
    AsyncCallbackJsonWebHandler* testFxHandler = new AsyncCallbackJsonWebHandler("/api/display/test-transition", [](AsyncWebServerRequest *request, JsonVariant &json) {
        RotationEffect eff = displayOrientationManager.getTransitionEffect();
        if (json.is<JsonObject>()) {
            JsonObject obj = json.as<JsonObject>();
            if (!obj["effect"].isNull()) {
                eff = RotationTransitionFX::parseEffect(obj["effect"].as<String>());
            }
        }
        displayOrientationManager.triggerTestTransition(eff);
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(testFxHandler);

    // API: POST /api/display/orientation — Sets manual rotation index, rotation offset, or transition effect
    AsyncCallbackJsonWebHandler* orientHandler = new AsyncCallbackJsonWebHandler("/api/display/orientation", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject obj = json.as<JsonObject>();
        extern ConfigLoader config;
        bool changed = false;
        if (!obj["manual_rotation"].isNull()) {
            displayOrientationManager.setRotation(obj["manual_rotation"].as<uint8_t>());
        }
        if (!obj["auto_rotate"].isNull()) {
            config.matrix.auto_rotate = obj["auto_rotate"].as<bool>();
            changed = true;
        }
        if (!obj["rotation_offset"].isNull()) {
            config.matrix.rotation_offset = obj["rotation_offset"].as<int>();
            displayOrientationManager.setRotationOffset((uint8_t)config.matrix.rotation_offset);
            changed = true;
        }
        if (!obj["transition_effect"].isNull()) {
            config.matrix.rotation_transition = obj["transition_effect"].as<String>();
            displayOrientationManager.setTransitionEffect(config.matrix.rotation_transition);
            changed = true;
        }
        if (!obj["transition_duration_ms"].isNull()) {
            config.matrix.rotation_transition_duration_ms = obj["transition_duration_ms"].as<int>();
            displayOrientationManager.setTransitionDuration((uint32_t)config.matrix.rotation_transition_duration_ms);
            changed = true;
        }
        if (changed) {
            ConfigSanitizer::sanitize(config);
            config.saveToSD("/config.json");
        }
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(orientHandler);

    // Register DLNA MediaRenderer description, SCPD and SOAP endpoints
    dlnaService.registerRoutes(&server);

    // Handle Preflight CORS
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });
}
