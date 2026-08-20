import os

# Update WebServerAPI.cpp
file_path = "src/api/WebServerAPI.cpp"
with open(file_path, "r") as f:
    content = f.read()

import_str = '#include "../core/EngineRegistry.h"\n#include <ArduinoJson.h>'
if "EngineRegistry.h" not in content:
    content = content.replace('#include "WebServerAPI.h"', f'#include "WebServerAPI.h"\n{import_str}')

route_code = """
    server.on("/api/engines", HTTP_GET, [](AsyncWebServerRequest *request){
        size_t count = 0;
        const EngineDescriptor* descriptors = EngineRegistry::getAllDescriptors(count);
        
        DynamicJsonDocument doc(4096);
        JsonArray array = doc.to<JsonArray>();
        for (size_t i = 0; i < count; i++) {
            JsonObject obj = array.createNestedObject();
            obj["metadata"]["id"] = descriptors[i].metadata.id;
            obj["metadata"]["name"] = descriptors[i].metadata.name;
            obj["metadata"]["category"] = descriptors[i].metadata.category;
            obj["metadata"]["version"] = descriptors[i].metadata.version;
            
            JsonObject caps = obj.createNestedObject("capabilities");
            caps["supports_128x32"] = descriptors[i].capabilities.supports_128x32;
            caps["supports_256x64"] = descriptors[i].capabilities.supports_256x64;
            caps["realtime"] = descriptors[i].capabilities.realtime;
            caps["interruptible"] = descriptors[i].capabilities.interruptible;
            
            JsonArray schema = obj.createNestedArray("schema");
            for (const auto& field : descriptors[i].schema.fields) {
                JsonObject fieldObj = schema.createNestedObject();
                fieldObj["id"] = field.id;
                fieldObj["field_type"] = (int)field.type;
                fieldObj["label"] = field.label;
                fieldObj["description"] = field.description;
                fieldObj["default_value"] = field.default_value;
                if (field.type == ConfigType::Options) {
                    JsonArray options = fieldObj.createNestedArray("options");
                    for (const auto& opt : field.options) {
                        JsonObject optObj = options.createNestedObject();
                        optObj["label"] = opt.label;
                        optObj["value"] = opt.value;
                    }
                }
            }
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
"""

if "/api/engines" not in content:
    # Insert in setupRoutes
    content = content.replace('void WebServerAPI::setupRoutes() {\n', 'void WebServerAPI::setupRoutes() {\n' + route_code)
    
with open(file_path, "w") as f:
    f.write(content)

print("WebServerAPI.cpp patched successfully")
