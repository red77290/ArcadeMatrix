#include "ConfigLoader.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include "SDUtils.h"

extern SemaphoreHandle_t sdMutex;

ConfigLoader::ConfigLoader() {
    setDefaults();
}

void ConfigLoader::publishSnapshot() {
    std::lock_guard<std::mutex> lock(_mutex);
    publishSnapshot_locked();
}

void ConfigLoader::publishSnapshot_locked() {
    uint8_t published = _publishedSlot.load(std::memory_order_relaxed);
    uint8_t reading = _readingSlot.load(std::memory_order_acquire);

    // Deterministically select a targetSlot in {0, 1, 2} that is NEITHER published NOR actively being read
    uint8_t targetSlot = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        if (i != published && i != reading) {
            targetSlot = i;
            break;
        }
    }

    ConfigSnapshot& snap = _snapshots[targetSlot];

    uint32_t newVer = _configVersion.fetch_add(1, std::memory_order_relaxed) + 1;
    snap.version = newVer;
    snap.matrix = matrix;
    snap.wifi = wifi;
    snap.mqtt = mqtt;
    snap.system = system;
    snap.rotation = rotation;
    snap.instances.clear();
    snap.instances.reserve(instances.size());
    for (const auto& inst : instances) {
        EngineInstanceSnapshot s;
        s.instance_id = inst.instance_id;
        s.engine_id = inst.engine_id;
        s.config = inst.config;
        snap.instances.push_back(s);
    }
    // Compute checksum for linearizability verification
    snap.checksum = (newVer ^ 0x5A5A5A5A) + (uint32_t)instances.size();

    _publishedSlot.store(targetSlot, std::memory_order_release);
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

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    matrix.width = 256;
    matrix.height = 64;
    matrix.chainLength = 4;
#else
    matrix.width = 64;
    matrix.height = 32;
    matrix.chainLength = 1;
#endif
    matrix.panelType = "SHIFTREG";
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
    matrix.rotation_transition = "vortex";
    matrix.rotation_transition_duration_ms = 400;

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
    publishSnapshot_locked();
}

bool ConfigLoader::parseFromJson(const char* jsonContent) {
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, jsonContent);

    if (error) {
        LOGE("ConfigLoader", "JSON parse failed: %s", error.c_str());
        return false;
    }
    return parseFromJsonDoc(doc);
}

bool ConfigLoader::parseFromJsonDoc(const DynamicJsonDocument& doc) {
    if (doc.containsKey("system")) {
        JsonObjectConst sys = doc["system"];
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

    JsonObjectConst disp;
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
        
        if (disp.containsKey("rotation_transition")) matrix.rotation_transition = disp["rotation_transition"].as<String>();
        else if (disp.containsKey("rotationTransition")) matrix.rotation_transition = disp["rotationTransition"].as<String>();
        
        if (disp.containsKey("rotation_transition_duration_ms")) matrix.rotation_transition_duration_ms = disp["rotation_transition_duration_ms"].as<int>();
        else if (disp.containsKey("rotationTransitionDurationMs")) matrix.rotation_transition_duration_ms = disp["rotationTransitionDurationMs"].as<int>();
    }

    if (doc.containsKey("wifi")) {
        JsonObjectConst w = doc["wifi"];
        wifi.ssid = w["ssid"] | wifi.ssid;
        wifi.password = w["password"] | wifi.password;
        wifi.hostname = w["hostname"] | wifi.hostname;
    }

    if (doc.containsKey("mqtt")) {
        JsonObjectConst m = doc["mqtt"];
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

    if (doc.containsKey("rotation")) {
        rotation.clear();
        JsonArrayConst rotArr = doc["rotation"].as<JsonArrayConst>();
        for (JsonObjectConst rotObj : rotArr) {
            RotationEntry entry;
            entry.instance_id = rotObj["instance_id"] | "";
            entry.duration_sec = rotObj["duration_sec"] | 15;
            if (rotObj.containsKey("overlays") && rotObj["overlays"].is<JsonObjectConst>()) {
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
        instances.clear();
        JsonArrayConst instArr = doc["instances"].as<JsonArrayConst>();
        for (JsonObjectConst instObj : instArr) {
            EngineInstance inst;
            inst.instance_id = instObj["instance_id"] | "";
            inst.engine_id = instObj["engine_id"] | "";
            if (instObj.containsKey("config")) {
                JsonObjectConst confObj = instObj["config"];
                for (JsonPairConst configPair : confObj) {
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
        instances.clear();
        JsonObjectConst engObj = doc["engines"].as<JsonObjectConst>();
        for (JsonPairConst kv : engObj) {
            String instanceId = kv.key().c_str();
            
            EngineInstance inst;
            inst.instance_id = instanceId;
            inst.engine_id = kv.value()["engine_id"] | "";
            
            JsonObjectConst confObj = kv.value()["config"];
            for (JsonPairConst configPair : confObj) {
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

    publishSnapshot_locked();
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
    dispObj["rotation_transition"] = matrix.rotation_transition;
    dispObj["rotation_transition_duration_ms"] = matrix.rotation_transition_duration_ms;
    dispObj["matrix_power"] = matrix.matrix_power;

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
    if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        LOGE("ConfigLoader", "Cannot load %s: SD busy (mutex timeout)", filepath);
        return false;
    }

    auto tryLoad = [this](const char* path) -> bool {
        if (!sd.exists(path)) return false;
        FsFile f = sd.open(path, FILE_OPEN_READ);
        if (!f) return false;
        DynamicJsonDocument doc(32768);
        DeserializationError error = deserializeJson(doc, f);
        f.close();
        if (error) {
            LOGE("ConfigLoader", "JSON parse error in %s: %s", path, error.c_str());
            return false;
        }
        return this->parseFromJsonDoc(doc);
    };

    bool loaded = tryLoad(filepath);
    if (!loaded) {
        String bakPath = String(filepath) + ".bak";
        if (sd.exists(bakPath.c_str())) {
            LOGW("ConfigLoader", "Primary file %s failed. Recovering from %s...", filepath, bakPath.c_str());
            loaded = tryLoad(bakPath.c_str());
            if (loaded) {
                LOGI("ConfigLoader", "RECOVERY SUCCESS: Restored configuration from %s", bakPath.c_str());
            }
        }
    }

    if (sdMutex) xSemaphoreGive(sdMutex);

    if (loaded) {
        LOGI("ConfigLoader", "Configuration loaded successfully from %s", filepath);
        return true;
    } else {
        LOGE("ConfigLoader", "Failed to load %s and backup. Using defaults.", filepath);
        return false;
    }
}

bool ConfigLoader::saveToSD(const char* filepath) {
    publishSnapshot();
    if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        LOGE("ConfigLoader", "Cannot save %s: SD busy (mutex timeout)", filepath);
        return false;
    }

    String jsonStr = serializeToJson(true);
    if (jsonStr.length() < 30) {
        LOGE("ConfigLoader", "Refusing to save truncated JSON to %s", filepath);
        if (sdMutex) xSemaphoreGive(sdMutex);
        return false;
    }

    // 1. Remove previous backup if exists
    String bakPath = String(filepath) + ".bak";
    if (sd.exists(bakPath.c_str())) {
        sd.remove(bakPath.c_str());
    }

    // 2. If current config exists, back it up to .bak
    if (sd.exists(filepath)) {
        sd.rename(filepath, bakPath.c_str());
    }

    // 3. Write new config directly to filepath
    FsFile f = sd.open(filepath, FILE_OPEN_WRITE);
    if (!f) {
        LOGE("ConfigLoader", "Failed to open %s for writing, restoring backup...", filepath);
        if (sd.exists(bakPath.c_str())) {
            sd.rename(bakPath.c_str(), filepath);
        }
        if (sdMutex) xSemaphoreGive(sdMutex);
        return false;
    }

    size_t written = f.print(jsonStr);
    f.flush();
    f.close();

    if (sdMutex) xSemaphoreGive(sdMutex);

    if (written >= jsonStr.length()) {
        LOGI("ConfigLoader", "Configuration saved successfully to %s (%d bytes)", filepath, written);
        return true;
    } else {
        LOGE("ConfigLoader", "Incomplete write to %s (%d/%d bytes)", filepath, written, jsonStr.length());
        return false;
    }
}
