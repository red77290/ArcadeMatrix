#include "ConfigLoader.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <SD.h>

ConfigLoader::ConfigLoader() {
    setDefaults();
}

void ConfigLoader::setDefaults() {
    instances.clear();
    rotation.clear();
    
    // Setup default instances
    auto addInstance = [&](String inst_id, String eng_id) {
        EngineInstance inst;
        inst.instance_id = inst_id;
        inst.engine_id = eng_id;
        instances.push_back(inst);
        return &instances.back();
    };
    
    addInstance("clock_main", "clock");
    addInstance("date_main", "date");
    addInstance("weather_main", "weather");
    addInstance("temp_main", "temp");
    addInstance("decibel_main", "decibel");
    addInstance("crypto_main", "crypto");
    addInstance("stock_main", "stock");
    addInstance("visualizer_main", "visualizer");
    addInstance("fighter_main", "fighter");
    
    RotationEntry re;
    re.instance_id = "clock_main"; re.duration_sec = 15; rotation.push_back(re);
    re.instance_id = "date_main"; re.duration_sec = 10; rotation.push_back(re);
    re.instance_id = "weather_main"; re.duration_sec = 10; rotation.push_back(re);
    re.instance_id = "crypto_main"; re.duration_sec = 10; rotation.push_back(re);
    re.instance_id = "stock_main"; re.duration_sec = 10; rotation.push_back(re);


    matrix.width = 64;
    matrix.height = 32;
    matrix.panelType = "SHIFTREG";
    matrix.chainLength = 1;
    matrix.powerLimitPercent = 50;
    matrix.forceSingleBuffer = false;
    matrix.colorDepth = 8;
    matrix.rgbSequence = "RGB";
    matrix.limitRefreshRateHz = 60;
    matrix.driverChip = "SHIFTREG";
    matrix.clkPhase = false;
    matrix.latchBlanking = 1;
    matrix.rowAddressMode = 0;
    matrix.matrix_power = true;

    wifi.ssid = "";
    wifi.password = "";
    wifi.hostname = "arcadematrix";

    mqtt.enabled = false;
    mqtt.broker = "";
    mqtt.port = 1883;
    mqtt.user = "";
    mqtt.pass = "";
    mqtt.deviceName = "ArcadeMatrix";
    mqtt.topic_batocera = "batocera";
    mqtt.topic_recalbox = "recalbox";

    system.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    system.format24h = true;
    system.lang = "en";
    system.unit = "C";
    system.temp_offset = 0.0f;
    system.night_mode_enabled = false;
    system.turn_off_at = "22:00";
    system.wake_up_at = "08:00";
    system.night_brightness = 10;
}

bool ConfigLoader::parseFromJson(const char* jsonContent) {
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, jsonContent);

    if (error) {
        LOGE("ConfigLoader", "JSON parse failed: %s", error.c_str());
        return false;
    }

    if (doc.containsKey("system")) {
        system.timezone = doc["system"]["timezone"] | system.timezone;
        system.format24h = doc["system"]["format24h"] | system.format24h;
        system.lang = doc["system"]["lang"] | system.lang;
        system.unit = doc["system"]["unit"] | system.unit;
        system.temp_offset = doc["system"]["temp_offset"] | system.temp_offset;
        system.night_mode_enabled = doc["system"]["night_mode_enabled"] | system.night_mode_enabled;
        system.turn_off_at = doc["system"]["turn_off_at"] | system.turn_off_at;
        system.wake_up_at = doc["system"]["wake_up_at"] | system.wake_up_at;
        system.night_brightness = doc["system"]["night_brightness"] | system.night_brightness;
    }

    if (doc.containsKey("display")) {
        matrix.width = doc["display"]["width"] | matrix.width;
        matrix.height = doc["display"]["height"] | matrix.height;
        matrix.panelType = doc["display"]["panelType"] | matrix.panelType;
        matrix.chainLength = doc["display"]["chainLength"] | matrix.chainLength;
        matrix.powerLimitPercent = doc["display"]["powerLimitPercent"] | matrix.powerLimitPercent;
        matrix.forceSingleBuffer = doc["display"]["forceSingleBuffer"] | matrix.forceSingleBuffer;
        matrix.colorDepth = doc["display"]["colorDepth"] | matrix.colorDepth;
        matrix.rgbSequence = doc["display"]["rgbSequence"] | matrix.rgbSequence;
        matrix.limitRefreshRateHz = doc["display"]["limitRefreshRateHz"] | matrix.limitRefreshRateHz;
        matrix.driverChip = doc["display"]["driverChip"] | matrix.driverChip;
        matrix.clkPhase = doc["display"]["clkPhase"] | matrix.clkPhase;
        matrix.latchBlanking = doc["display"]["latchBlanking"] | matrix.latchBlanking;
        matrix.rowAddressMode = doc["display"]["rowAddressMode"] | matrix.rowAddressMode;
    }

    if (doc.containsKey("wifi")) {
        wifi.ssid = doc["wifi"]["ssid"] | wifi.ssid;
        wifi.password = doc["wifi"]["password"] | wifi.password;
        wifi.hostname = doc["wifi"]["hostname"] | wifi.hostname;
    }

    if (doc.containsKey("mqtt")) {
        mqtt.enabled = doc["mqtt"]["enabled"] | mqtt.enabled;
        mqtt.broker = doc["mqtt"]["broker"] | mqtt.broker;
        mqtt.port = doc["mqtt"]["port"] | mqtt.port;
        mqtt.user = doc["mqtt"]["user"] | mqtt.user;
        mqtt.pass = doc["mqtt"]["pass"] | mqtt.pass;
        mqtt.deviceName = doc["mqtt"]["deviceName"] | mqtt.deviceName;
        mqtt.topic_batocera = doc["mqtt"]["topic_batocera"] | mqtt.topic_batocera;
        mqtt.topic_recalbox = doc["mqtt"]["topic_recalbox"] | mqtt.topic_recalbox;
    }

    instances.clear();
    rotation.clear();

    if (doc.containsKey("rotation")) {
        JsonArray rotArr = doc["rotation"].as<JsonArray>();
        for (JsonObject rotObj : rotArr) {
            RotationEntry entry;
            entry.instance_id = rotObj["instance_id"] | "";
            entry.duration_sec = rotObj["duration_sec"] | 15;
            rotation.push_back(entry);
        }
    }

    if (doc.containsKey("engines")) {
        JsonObject engObj = doc["engines"].as<JsonObject>();
        for (JsonPair kv : engObj) {
            String instanceId = kv.key().c_str();
            
            EngineInstance inst;
            inst.instance_id = instanceId;
            inst.engine_id = kv.value()["engine_id"] | "";
            
            JsonObject confObj = kv.value()["config"];
            for (JsonPair configPair : confObj) {
                if (configPair.value().is<int>()) {
                    inst.config.setInt(configPair.key().c_str(), configPair.value().as<int>());
                } else if (configPair.value().is<float>()) {
                    inst.config.setString(configPair.key().c_str(), String(configPair.value().as<float>()));
                } else if (configPair.value().is<bool>()) {
                    inst.config.setBool(configPair.key().c_str(), configPair.value().as<bool>());
                } else {
                    inst.config.setString(configPair.key().c_str(), configPair.value().as<String>());
                }
            }
            instances.push_back(inst);
        }
    }

    return true;
}

String ConfigLoader::serializeToJson() const {
    DynamicJsonDocument doc(8192);

    JsonObject sysObj = doc.createNestedObject("system");
    sysObj["timezone"] = system.timezone;
    sysObj["format24h"] = system.format24h;
    sysObj["lang"] = system.lang;
    sysObj["unit"] = system.unit;
    sysObj["temp_offset"] = system.temp_offset;
    sysObj["night_mode_enabled"] = system.night_mode_enabled;
    sysObj["turn_off_at"] = system.turn_off_at;
    sysObj["wake_up_at"] = system.wake_up_at;
    sysObj["night_brightness"] = system.night_brightness;

    JsonObject dispObj = doc.createNestedObject("display");
    dispObj["width"] = matrix.width;
    dispObj["height"] = matrix.height;
    dispObj["panelType"] = matrix.panelType;
    dispObj["chainLength"] = matrix.chainLength;
    dispObj["powerLimitPercent"] = matrix.powerLimitPercent;
    dispObj["forceSingleBuffer"] = matrix.forceSingleBuffer;
    dispObj["colorDepth"] = matrix.colorDepth;
    dispObj["rgbSequence"] = matrix.rgbSequence;
    dispObj["limitRefreshRateHz"] = matrix.limitRefreshRateHz;
    dispObj["driverChip"] = matrix.driverChip;
    dispObj["clkPhase"] = matrix.clkPhase;
    dispObj["latchBlanking"] = matrix.latchBlanking;
    dispObj["rowAddressMode"] = matrix.rowAddressMode;

    JsonObject wObj = doc.createNestedObject("wifi");
    wObj["ssid"] = wifi.ssid;
    wObj["password"] = wifi.password;
    wObj["hostname"] = wifi.hostname;

    JsonObject mObj = doc.createNestedObject("mqtt");
    mObj["enabled"] = mqtt.enabled;
    mObj["broker"] = mqtt.broker;
    mObj["port"] = mqtt.port;
    mObj["user"] = mqtt.user;
    mObj["pass"] = mqtt.pass;
    mObj["deviceName"] = mqtt.deviceName;
    mObj["topic_batocera"] = mqtt.topic_batocera;
    mObj["topic_recalbox"] = mqtt.topic_recalbox;

    JsonArray rotArr = doc.createNestedArray("rotation");
    for (const auto& rot : rotation) {
        JsonObject rObj = rotArr.createNestedObject();
        rObj["instance_id"] = rot.instance_id;
        rObj["duration_sec"] = rot.duration_sec;
    }

    JsonObject engObj = doc.createNestedObject("engines");
    for (const auto& inst : instances) {
        JsonObject instNode = engObj.createNestedObject(inst.instance_id);
        instNode["engine_id"] = inst.engine_id;
        
        JsonObject confNode = instNode.createNestedObject("config");
        for (const auto& ckv : inst.config.getDictionary()) {
            confNode[ckv.first] = ckv.second;
        }
    }

    String output;
    serializeJson(doc, output);
    return output;
}

bool ConfigLoader::loadFromSD(const char* filepath) {
    if (!SD.exists(filepath)) {
        LOGE("ConfigLoader", "File not found: %s", filepath);
        return false;
    }

    File f = SD.open(filepath, FILE_READ);
    if (!f) {
        LOGE("ConfigLoader", "Failed to open file: %s", filepath);
        return false;
    }

    String content = f.readString();
    f.close();

    return parseFromJson(content.c_str());
}

bool ConfigLoader::saveToSD(const char* filepath) {
    String tempPath = String(filepath) + ".tmp";
    File f = SD.open(tempPath, FILE_WRITE);
    if (!f) {
        LOGE("ConfigLoader", "Failed to open temp file for writing: %s", tempPath.c_str());
        return false;
    }

    String jsonStr = serializeToJson();
    f.print(jsonStr);
    f.close();

    if (SD.exists(filepath)) {
        SD.remove(filepath);
    }
    
    if (SD.rename(tempPath, filepath)) {
        LOGI("ConfigLoader", "Configuration saved successfully to %s", filepath);
        return true;
    } else {
        LOGE("ConfigLoader", "Failed to rename temp file to %s", filepath);
        return false;
    }
}
