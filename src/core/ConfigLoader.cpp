#include "ConfigLoader.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include "SDUtils.h"

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
    addInstance("decibel_main", "decibelMeter");
    addInstance("crypto_main", "crypto");
    addInstance("stock_main", "stock");
    addInstance("visualizer_main", "audiovisualizer");
    addInstance("gifs_main", "gifs");
    addInstance("message_main", "message");

    
    RotationEntry re;
    re.instance_id = "clock_main"; re.duration_sec = 15; rotation.push_back(re);
    re.instance_id = "date_main"; re.duration_sec = 10; rotation.push_back(re);
    re.instance_id = "weather_main"; re.duration_sec = 10; rotation.push_back(re);
    re.instance_id = "crypto_main"; re.duration_sec = 10; rotation.push_back(re);
    re.instance_id = "stock_main"; re.duration_sec = 10; rotation.push_back(re);
    re.instance_id = "gifs_main"; re.duration_sec = 30; rotation.push_back(re);
    re.instance_id = "temp_main"; re.duration_sec = 10; rotation.push_back(re);
    re.instance_id = "decibel_main"; re.duration_sec = 15; rotation.push_back(re);
    re.instance_id = "message_main"; re.duration_sec = 15; rotation.push_back(re);
    matrix.width = 64;
    matrix.height = 32;
    matrix.panelType = "SHIFTREG";
    matrix.chainLength = 1;
    matrix.powerLimitPercent = 50;
    matrix.forceSingleBuffer = false;
    matrix.colorDepth = 8;
    matrix.rgbSequence = "RGB";
    matrix.limitRefreshRateHz = 90;
    matrix.driverChip = "SHIFTREG";
    matrix.clkPhase = true;
    matrix.latchBlanking = 4;
    matrix.rowAddressMode = 0;
    matrix.matrix_power = true;
    matrix.rotation_offset = 0;
    matrix.auto_rotate = true;

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
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, jsonContent);

    if (error) {
        LOGE("ConfigLoader", "JSON parse failed: %s", error.c_str());
        return false;
    }

    if (doc.containsKey("system")) {
        JsonObject sys = doc["system"];
        system.timezone = sys["timezone"] | system.timezone;
        if (sys.containsKey("format_24h")) system.format24h = sys["format_24h"].as<bool>();
        else if (sys.containsKey("format24h")) system.format24h = sys["format24h"].as<bool>();
        system.lang = sys["lang"] | system.lang;
        system.unit = sys["unit"] | system.unit;
        system.temp_offset = sys["temp_offset"] | system.temp_offset;
        system.night_mode_enabled = sys["night_mode_enabled"] | system.night_mode_enabled;
        system.turn_off_at = sys["turn_off_at"] | system.turn_off_at;
        system.wake_up_at = sys["wake_up_at"] | system.wake_up_at;
        system.night_brightness = sys["night_brightness"] | system.night_brightness;
        system.idle_fighter_enabled = sys["idle_fighter_enabled"] | system.idle_fighter_enabled;
        system.idle_fighter_interval = sys["idle_fighter_interval"] | system.idle_fighter_interval;
    }

    JsonObject disp;
    if (doc.containsKey("display")) {
        disp = doc["display"];
    } else if (doc.containsKey("matrix")) {
        disp = doc["matrix"];
    }

    if (!disp.isNull()) {
        matrix.width = disp["width"] | matrix.width;
        matrix.height = disp["height"] | matrix.height;
        if (disp.containsKey("panel_type")) matrix.panelType = disp["panel_type"].as<String>();
        else if (disp.containsKey("panelType")) matrix.panelType = disp["panelType"].as<String>();
        
        if (disp.containsKey("chain_length")) matrix.chainLength = disp["chain_length"].as<int>();
        else if (disp.containsKey("chainLength")) matrix.chainLength = disp["chainLength"].as<int>();
        
        if (disp.containsKey("power_limit_percent")) matrix.powerLimitPercent = disp["power_limit_percent"].as<int>();
        else if (disp.containsKey("powerLimitPercent")) matrix.powerLimitPercent = disp["powerLimitPercent"].as<int>();
        
        if (disp.containsKey("force_single_buffer")) matrix.forceSingleBuffer = disp["force_single_buffer"].as<bool>();
        else if (disp.containsKey("forceSingleBuffer")) matrix.forceSingleBuffer = disp["forceSingleBuffer"].as<bool>();
        
        if (disp.containsKey("color_depth")) matrix.colorDepth = disp["color_depth"].as<int>();
        else if (disp.containsKey("colorDepth")) matrix.colorDepth = disp["colorDepth"].as<int>();
        else if (disp.containsKey("pwm_bits")) matrix.colorDepth = disp["pwm_bits"].as<int>();
        
        if (disp.containsKey("rgb_sequence")) matrix.rgbSequence = disp["rgb_sequence"].as<String>();
        else if (disp.containsKey("rgbSequence")) matrix.rgbSequence = disp["rgbSequence"].as<String>();
        
        if (disp.containsKey("limit_refresh_rate_hz")) matrix.limitRefreshRateHz = disp["limit_refresh_rate_hz"].as<int>();
        else if (disp.containsKey("limitRefreshRateHz")) matrix.limitRefreshRateHz = disp["limitRefreshRateHz"].as<int>();
        
        if (disp.containsKey("driver_chip")) matrix.driverChip = disp["driver_chip"].as<String>();
        else if (disp.containsKey("driverChip")) matrix.driverChip = disp["driverChip"].as<String>();
        
        if (disp.containsKey("clk_phase")) matrix.clkPhase = disp["clk_phase"].as<bool>();
        else if (disp.containsKey("clkPhase")) matrix.clkPhase = disp["clkPhase"].as<bool>();
        
        if (disp.containsKey("latch_blanking")) matrix.latchBlanking = disp["latch_blanking"].as<int>();
        else if (disp.containsKey("latchBlanking")) matrix.latchBlanking = disp["latchBlanking"].as<int>();
        
        if (disp.containsKey("row_address_mode")) matrix.rowAddressMode = disp["row_address_mode"].as<int>();
        else if (disp.containsKey("rowAddressMode")) matrix.rowAddressMode = disp["rowAddressMode"].as<int>();
        
        if (disp.containsKey("rotation_offset")) matrix.rotation_offset = disp["rotation_offset"].as<int>();
        else if (disp.containsKey("rotationOffset")) matrix.rotation_offset = disp["rotationOffset"].as<int>();
        
        if (disp.containsKey("auto_rotate")) matrix.auto_rotate = disp["auto_rotate"].as<bool>();
        else if (disp.containsKey("autoRotate")) matrix.auto_rotate = disp["autoRotate"].as<bool>();
    }

    if (doc.containsKey("wifi")) {
        wifi.ssid = doc["wifi"]["ssid"] | wifi.ssid;
        wifi.password = doc["wifi"]["password"] | wifi.password;
        wifi.hostname = doc["wifi"]["hostname"] | wifi.hostname;
    }

    if (doc.containsKey("mqtt")) {
        JsonObject m = doc["mqtt"];
        mqtt.enabled = m["enabled"] | mqtt.enabled;
        mqtt.broker = m["broker"] | mqtt.broker;
        mqtt.port = m["port"] | mqtt.port;
        mqtt.user = m["user"] | mqtt.user;
        mqtt.pass = m["pass"] | mqtt.pass;
        if (m.containsKey("device_name")) mqtt.deviceName = m["device_name"].as<String>();
        else if (m.containsKey("deviceName")) mqtt.deviceName = m["deviceName"].as<String>();
        mqtt.topic_batocera = m["topic_batocera"] | mqtt.topic_batocera;
        mqtt.topic_recalbox = m["topic_recalbox"] | mqtt.topic_recalbox;
    }

    instances.clear();
    rotation.clear();

    if (doc.containsKey("rotation")) {
        JsonArray rotArr = doc["rotation"].as<JsonArray>();
        for (JsonObject rotObj : rotArr) {
            RotationEntry entry;
            entry.instance_id = rotObj["instance_id"] | "";
            entry.duration_sec = rotObj["duration_sec"] | 15;
            if (rotObj.containsKey("overlays") && rotObj["overlays"].is<JsonObject>()) {
                entry.overlays.fighter = rotObj["overlays"]["fighter"] | false;
            } else if (rotObj.containsKey("fighter_overlay")) {
                entry.overlays.fighter = rotObj["fighter_overlay"] | false;
            } else {
                entry.overlays.fighter = false;
            }
            rotation.push_back(entry);
        }
    }

    if (doc.containsKey("instances")) {
        JsonArray instArr = doc["instances"].as<JsonArray>();
        for (JsonObject instObj : instArr) {
            EngineInstance inst;
            inst.instance_id = instObj["instance_id"] | "";
            inst.engine_id = instObj["engine_id"] | "";
            if (instObj.containsKey("config")) {
                JsonObject confObj = instObj["config"];
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
            }
            instances.push_back(inst);
        }
    } else if (doc.containsKey("engines")) {
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

String ConfigLoader::serializeToJson(bool pretty) const {
    DynamicJsonDocument doc(32768);

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
    sysObj["idle_fighter_enabled"] = system.idle_fighter_enabled;
    sysObj["idle_fighter_interval"] = system.idle_fighter_interval;

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
    dispObj["rotation_offset"] = matrix.rotation_offset;
    dispObj["auto_rotate"] = matrix.auto_rotate;

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
        JsonObject ovObj = rObj.createNestedObject("overlays");
        ovObj["fighter"] = rot.overlays.fighter;
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
    if (pretty) {
        serializeJsonPretty(doc, output);
    } else {
        serializeJson(doc, output);
    }
    return output;
}

bool ConfigLoader::loadFromSD(const char* filepath) {
    if (!sd.exists(filepath)) {
        LOGE("ConfigLoader", "File not found: %s", filepath);
        return false;
    }

    FsFile f = sd.open(filepath, FILE_OPEN_READ);
    if (!f) {
        LOGE("ConfigLoader", "Failed to open file: %s", filepath);
        return false;
    }

    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error) {
        LOGE("ConfigLoader", "JSON parse failed from %s: %s", filepath, error.c_str());
        return false;
    }

    // Process system, display, wifi, mqtt, rotation, engines/instances
    if (doc.containsKey("system")) {
        JsonObject sys = doc["system"];
        system.timezone = sys["timezone"] | system.timezone;
        if (sys.containsKey("format_24h")) system.format24h = sys["format_24h"].as<bool>();
        else if (sys.containsKey("format24h")) system.format24h = sys["format24h"].as<bool>();
        system.lang = sys["lang"] | system.lang;
        system.unit = sys["unit"] | system.unit;
        system.temp_offset = sys["temp_offset"] | system.temp_offset;
        system.night_mode_enabled = sys["night_mode_enabled"] | system.night_mode_enabled;
        system.turn_off_at = sys["turn_off_at"] | system.turn_off_at;
        system.wake_up_at = sys["wake_up_at"] | system.wake_up_at;
        system.night_brightness = sys["night_brightness"] | system.night_brightness;
        system.idle_fighter_enabled = sys["idle_fighter_enabled"] | system.idle_fighter_enabled;
        system.idle_fighter_interval = sys["idle_fighter_interval"] | system.idle_fighter_interval;
    }

    JsonObject disp;
    if (doc.containsKey("display")) {
        disp = doc["display"];
    } else if (doc.containsKey("matrix")) {
        disp = doc["matrix"];
    }

    if (!disp.isNull()) {
        matrix.width = disp["width"] | matrix.width;
        matrix.height = disp["height"] | matrix.height;
        if (disp.containsKey("panel_type")) matrix.panelType = disp["panel_type"].as<String>();
        else if (disp.containsKey("panelType")) matrix.panelType = disp["panelType"].as<String>();
        if (disp.containsKey("chain_length")) matrix.chainLength = disp["chain_length"] | matrix.chainLength;
        else if (disp.containsKey("chainLength")) matrix.chainLength = disp["chainLength"] | matrix.chainLength;
        if (disp.containsKey("power_limit_percent")) matrix.powerLimitPercent = disp["power_limit_percent"] | matrix.powerLimitPercent;
        else if (disp.containsKey("powerLimitPercent")) matrix.powerLimitPercent = disp["powerLimitPercent"] | matrix.powerLimitPercent;
        if (disp.containsKey("force_single_buffer")) matrix.forceSingleBuffer = disp["force_single_buffer"].as<bool>();
        else if (disp.containsKey("forceSingleBuffer")) matrix.forceSingleBuffer = disp["forceSingleBuffer"].as<bool>();
        if (disp.containsKey("color_depth")) matrix.colorDepth = disp["color_depth"] | matrix.colorDepth;
        else if (disp.containsKey("colorDepth")) matrix.colorDepth = disp["colorDepth"] | matrix.colorDepth;
        if (disp.containsKey("rgb_sequence")) matrix.rgbSequence = disp["rgb_sequence"].as<String>();
        else if (disp.containsKey("rgbSequence")) matrix.rgbSequence = disp["rgbSequence"].as<String>();
        if (disp.containsKey("limit_refresh_rate_hz")) matrix.limitRefreshRateHz = disp["limit_refresh_rate_hz"] | matrix.limitRefreshRateHz;
        else if (disp.containsKey("limitRefreshRateHz")) matrix.limitRefreshRateHz = disp["limitRefreshRateHz"] | matrix.limitRefreshRateHz;
        if (disp.containsKey("driver_chip")) matrix.driverChip = disp["driver_chip"].as<String>();
        else if (disp.containsKey("driverChip")) matrix.driverChip = disp["driverChip"].as<String>();
        if (disp.containsKey("clk_phase")) matrix.clkPhase = disp["clk_phase"].as<bool>();
        else if (disp.containsKey("clkPhase")) matrix.clkPhase = disp["clkPhase"].as<bool>();
        if (disp.containsKey("latch_blanking")) matrix.latchBlanking = disp["latch_blanking"] | matrix.latchBlanking;
        else if (disp.containsKey("latchBlanking")) matrix.latchBlanking = disp["latchBlanking"] | matrix.latchBlanking;
        if (disp.containsKey("row_address_mode")) matrix.rowAddressMode = disp["row_address_mode"] | matrix.rowAddressMode;
        else if (disp.containsKey("rowAddressMode")) matrix.rowAddressMode = disp["rowAddressMode"] | matrix.rowAddressMode;
        if (disp.containsKey("matrix_power")) matrix.matrix_power = disp["matrix_power"].as<bool>();
    }

    if (doc.containsKey("wifi")) {
        JsonObject w = doc["wifi"];
        wifi.ssid = w["ssid"] | wifi.ssid;
        wifi.password = w["password"] | wifi.password;
        wifi.hostname = w["hostname"] | wifi.hostname;
    }

    if (doc.containsKey("mqtt")) {
        JsonObject m = doc["mqtt"];
        mqtt.enabled = m["enabled"] | mqtt.enabled;
        mqtt.broker = m["broker"] | mqtt.broker;
        mqtt.port = m["port"] | mqtt.port;
        mqtt.user = m["user"] | mqtt.user;
        mqtt.pass = m["pass"] | mqtt.pass;
        mqtt.deviceName = m["deviceName"] | mqtt.deviceName;
        mqtt.topic_batocera = m["topic_batocera"] | mqtt.topic_batocera;
        mqtt.topic_recalbox = m["topic_recalbox"] | mqtt.topic_recalbox;
    }

    if (doc.containsKey("rotation")) {
        rotation.clear();
        JsonArray rotArr = doc["rotation"];
        for (JsonObject rotItem : rotArr) {
            RotationEntry entry;
            entry.instance_id = rotItem["instance_id"].as<String>();
            entry.duration_sec = rotItem["duration_sec"] | 15;
            if (rotItem.containsKey("overlays") && rotItem["overlays"].is<JsonObject>()) {
                entry.overlays.fighter = rotItem["overlays"]["fighter"] | false;
            } else if (rotItem.containsKey("fighter_overlay")) {
                entry.overlays.fighter = rotItem["fighter_overlay"] | false;
            } else {
                entry.overlays.fighter = false;
            }
            rotation.push_back(entry);
        }
    }

    if (doc.containsKey("instances")) {
        instances.clear();
        JsonArray instArr = doc["instances"];
        for (JsonObject instObj : instArr) {
            EngineInstance inst;
            inst.instance_id = instObj["instance_id"].as<String>();
            inst.engine_id = instObj["engine_id"].as<String>();
            if (instObj.containsKey("config")) {
                JsonObject cfg = instObj["config"];
                for (JsonPair kv : cfg) {
                    inst.config.setString(kv.key().c_str(), kv.value().as<String>());
                }
            }
            instances.push_back(inst);
        }
    } else if (doc.containsKey("engines")) {
        instances.clear();
        JsonObject engObj = doc["engines"];
        for (JsonPair kv : engObj) {
            EngineInstance inst;
            inst.instance_id = kv.key().c_str();
            JsonObject instNode = kv.value().as<JsonObject>();
            inst.engine_id = instNode["engine_id"].as<String>();
            if (instNode.containsKey("config")) {
                JsonObject confNode = instNode["config"];
                for (JsonPair ckv : confNode) {
                    inst.config.setString(ckv.key().c_str(), ckv.value().as<String>());
                }
            }
            instances.push_back(inst);
        }
    }

    return true;
}

bool ConfigLoader::saveToSD(const char* filepath) {
    String tempPath = String(filepath) + ".tmp";
    FsFile f = sd.open(tempPath.c_str(), FILE_OPEN_WRITE);
    if (!f) {
        LOGE("ConfigLoader", "Failed to open temp file for writing: %s", tempPath.c_str());
        return false;
    }

    String jsonStr = serializeToJson(true); // Always save human-readable, indented JSON to SD card
    f.print(jsonStr);
    f.close();

    if (sd.exists(filepath)) {
        sd.remove(filepath);
    }
    
    if (sd.rename(tempPath.c_str(), filepath)) {
        LOGI("ConfigLoader", "Configuration saved successfully to %s", filepath);
        return true;
    } else {
        LOGE("ConfigLoader", "Failed to rename temp file to %s", filepath);
        return false;
    }
}
