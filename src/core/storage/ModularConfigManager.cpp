/**
 * @file ModularConfigManager.cpp
 * @brief Implementation of ModularConfigManager.
 */
#include "ModularConfigManager.h"
#include "../ConfigSanitizer.h"
#include "../Logger.h"
#include <ArduinoJson.h>

ModularConfigManager::ModularConfigManager(IConfigStorage& storage)
    : _storage(storage)
    , _workingSet(storage)
{
}

bool ModularConfigManager::checkAndMigrateLegacy(ConfigLoader& config, const char* legacyPath) {
    if (!_storage.exists(legacyPath)) {
        return false; // No legacy file to migrate
    }

    bool hasModular = _storage.exists("/config/hardware.json") && _storage.exists("/config/system.json");
    if (hasModular) {
        return false; // Already migrated
    }

    LOGI("ModularConfigManager", "Legacy config detected (%s). Migrating to modular /config/ structure...", legacyPath);

    String legacyContent;
    if (!_storage.readString(legacyPath, legacyContent)) {
        LOGE("ModularConfigManager", "Failed to read legacy %s for migration", legacyPath);
        return false;
    }

    // Parse into temporary config
    if (!config.parseFromJson(legacyContent.c_str())) {
        LOGE("ModularConfigManager", "Failed to parse legacy JSON during migration");
        return false;
    }

    // Sanitize in-memory model prior to splitting
    SanitizeResult san = ConfigSanitizer::sanitize(config, false);
    LOGI("ModularConfigManager", "Sanitized legacy config: modified=%d, defaults=%d, clamped=%d",
         san.modified, san.defaults_injected, san.values_clamped);

    // Save out all domain files and instance micro-files
    if (!saveAll(config)) {
        LOGE("ModularConfigManager", "Failed to write modular configuration files");
        return false;
    }

    // Backup legacy file
    String backupPath = String(legacyPath) + ".bak";
    if (_storage.writeStringAtomic(backupPath.c_str(), legacyContent)) {
        LOGI("ModularConfigManager", "Legacy config safely backed up to %s", backupPath.c_str());
    }

    LOGI("ModularConfigManager", "Migration complete! All configurations partitioned cleanly.");
    return true;
}

bool ModularConfigManager::loadAll(ConfigLoader& config) {
    _storage.mkdir("/config");
    _storage.mkdir("/config/instances");

    // Load domain files
    bool hwOk = loadHardware(config.matrix);
    bool sysOk = loadSystem(config.system);
    bool netOk = loadNetwork(config.wifi, config.mqtt);
    bool rotOk = loadPlaylist(config.rotation);

    if (!hwOk || !sysOk || !netOk || !rotOk) {
        LOGW("ModularConfigManager", "One or more domain configs missing in /config/; using defaults where needed.");
    }

    // Sync working set with active playlist
    _workingSet.syncWithPlaylist(config.rotation);
    config.instances = _workingSet.getCachedInstances();

    return true;
}

bool ModularConfigManager::saveAll(const ConfigLoader& config) {
    _storage.mkdir("/config");
    _storage.mkdir("/config/instances");

    bool hwOk = saveHardware(config.matrix);
    bool sysOk = saveSystem(config.system);
    bool netOk = saveNetwork(config.wifi, config.mqtt);
    bool rotOk = savePlaylist(config.rotation);

    // Persist all instances
    bool instOk = true;
    for (const auto& inst : config.instances) {
        if (!_workingSet.saveAndCacheInstance(inst)) {
            instOk = false;
        }
    }

    return hwOk && sysOk && netOk && rotOk && instOk;
}

bool ModularConfigManager::loadHardware(MatrixConfig& outMatrix) {
    String content;
    if (!_storage.readString("/config/hardware.json", content)) {
        return false;
    }
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, content)) return false;

    outMatrix.width = doc["width"] | outMatrix.width;
    outMatrix.height = doc["height"] | outMatrix.height;
    if (doc.containsKey("panel_type")) outMatrix.panelType = doc["panel_type"].as<String>();
    else if (doc.containsKey("panelType")) outMatrix.panelType = doc["panelType"].as<String>();

    if (doc.containsKey("chain_length")) outMatrix.chainLength = doc["chain_length"].as<int>();
    else if (doc.containsKey("chainLength")) outMatrix.chainLength = doc["chainLength"].as<int>();

    if (doc.containsKey("power_limit_percent")) outMatrix.powerLimitPercent = doc["power_limit_percent"].as<int>();
    else if (doc.containsKey("powerLimitPercent")) outMatrix.powerLimitPercent = doc["powerLimitPercent"].as<int>();

    if (doc.containsKey("force_single_buffer")) outMatrix.forceSingleBuffer = doc["force_single_buffer"].as<bool>();
    else if (doc.containsKey("forceSingleBuffer")) outMatrix.forceSingleBuffer = doc["forceSingleBuffer"].as<bool>();

    if (doc.containsKey("color_depth")) outMatrix.colorDepth = doc["color_depth"].as<int>();
    else if (doc.containsKey("colorDepth")) outMatrix.colorDepth = doc["colorDepth"].as<int>();

    if (doc.containsKey("rgb_sequence")) outMatrix.rgbSequence = doc["rgb_sequence"].as<String>();
    else if (doc.containsKey("rgbSequence")) outMatrix.rgbSequence = doc["rgbSequence"].as<String>();

    if (doc.containsKey("limit_refresh_rate_hz")) outMatrix.limitRefreshRateHz = doc["limit_refresh_rate_hz"].as<int>();
    else if (doc.containsKey("limitRefreshRateHz")) outMatrix.limitRefreshRateHz = doc["limitRefreshRateHz"].as<int>();

    if (doc.containsKey("driver_chip")) outMatrix.driverChip = doc["driver_chip"].as<String>();
    else if (doc.containsKey("driverChip")) outMatrix.driverChip = doc["driverChip"].as<String>();

    if (doc.containsKey("clk_phase")) outMatrix.clkPhase = doc["clk_phase"].as<bool>();
    else if (doc.containsKey("clkPhase")) outMatrix.clkPhase = doc["clkPhase"].as<bool>();

    if (doc.containsKey("latch_blanking")) outMatrix.latchBlanking = doc["latch_blanking"].as<int>();
    else if (doc.containsKey("latchBlanking")) outMatrix.latchBlanking = doc["latchBlanking"].as<int>();

    if (doc.containsKey("row_address_mode")) outMatrix.rowAddressMode = doc["row_address_mode"].as<int>();
    else if (doc.containsKey("rowAddressMode")) outMatrix.rowAddressMode = doc["rowAddressMode"].as<int>();

    if (doc.containsKey("rotation_offset")) outMatrix.rotation_offset = doc["rotation_offset"].as<int>();
    else if (doc.containsKey("rotationOffset")) outMatrix.rotation_offset = doc["rotationOffset"].as<int>();

    if (doc.containsKey("auto_rotate")) outMatrix.auto_rotate = doc["auto_rotate"].as<bool>();
    else if (doc.containsKey("autoRotate")) outMatrix.auto_rotate = doc["autoRotate"].as<bool>();

    if (doc.containsKey("rotation_transition")) outMatrix.rotation_transition = doc["rotation_transition"].as<String>();
    else if (doc.containsKey("rotationTransition")) outMatrix.rotation_transition = doc["rotationTransition"].as<String>();

    if (doc.containsKey("rotation_transition_duration_ms")) outMatrix.rotation_transition_duration_ms = doc["rotation_transition_duration_ms"].as<int>();
    else if (doc.containsKey("rotationTransitionDurationMs")) outMatrix.rotation_transition_duration_ms = doc["rotationTransitionDurationMs"].as<int>();

    if (doc.containsKey("matrix_power")) outMatrix.matrix_power = doc["matrix_power"].as<bool>();
    else if (doc.containsKey("matrixPower")) outMatrix.matrix_power = doc["matrixPower"].as<bool>();

    if (doc.containsKey("render_pipeline")) outMatrix.render_pipeline = doc["render_pipeline"].as<String>();
    else if (doc.containsKey("renderPipeline")) outMatrix.render_pipeline = doc["renderPipeline"].as<String>();

    return true;
}

bool ModularConfigManager::saveHardware(const MatrixConfig& matrix) {
    StaticJsonDocument<1024> doc;
    doc["width"] = matrix.width;
    doc["height"] = matrix.height;
    doc["panelType"] = matrix.panelType;
    doc["chainLength"] = matrix.chainLength;
    doc["powerLimitPercent"] = matrix.powerLimitPercent;
    doc["forceSingleBuffer"] = matrix.forceSingleBuffer;
    doc["colorDepth"] = matrix.colorDepth;
    doc["rgbSequence"] = matrix.rgbSequence;
    doc["limitRefreshRateHz"] = matrix.limitRefreshRateHz;
    doc["driverChip"] = matrix.driverChip;
    doc["clkPhase"] = matrix.clkPhase;
    doc["latchBlanking"] = matrix.latchBlanking;
    doc["rowAddressMode"] = matrix.rowAddressMode;
    doc["rotation_offset"] = matrix.rotation_offset;
    doc["auto_rotate"] = matrix.auto_rotate;
    doc["rotation_transition"] = matrix.rotation_transition;
    doc["rotation_transition_duration_ms"] = matrix.rotation_transition_duration_ms;
    doc["matrix_power"] = matrix.matrix_power;
    doc["render_pipeline"] = matrix.render_pipeline;

    String out;
    serializeJsonPretty(doc, out);
    return _storage.writeStringAtomic("/config/hardware.json", out);
}

bool ModularConfigManager::loadSystem(SystemConfig& outSystem) {
    String content;
    if (!_storage.readString("/config/system.json", content)) {
        return false;
    }
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, content)) return false;

    if (doc.containsKey("timezone")) outSystem.timezone = doc["timezone"].as<String>();

    if (doc.containsKey("format_24h")) outSystem.format24h = doc["format_24h"].as<bool>();
    else if (doc.containsKey("format24h")) outSystem.format24h = doc["format24h"].as<bool>();

    if (doc.containsKey("lang")) outSystem.lang = doc["lang"].as<String>();
    if (doc.containsKey("unit")) outSystem.unit = doc["unit"].as<String>();

    if (doc.containsKey("temp_offset")) outSystem.temp_offset = doc["temp_offset"].as<float>();
    else if (doc.containsKey("tempOffset")) outSystem.temp_offset = doc["tempOffset"].as<float>();

    if (doc.containsKey("night_mode_enabled")) outSystem.night_mode_enabled = doc["night_mode_enabled"].as<bool>();
    else if (doc.containsKey("nightModeEnabled")) outSystem.night_mode_enabled = doc["nightModeEnabled"].as<bool>();

    if (doc.containsKey("turn_off_at")) outSystem.turn_off_at = doc["turn_off_at"].as<String>();
    else if (doc.containsKey("turnOffAt")) outSystem.turn_off_at = doc["turnOffAt"].as<String>();

    if (doc.containsKey("wake_up_at")) outSystem.wake_up_at = doc["wake_up_at"].as<String>();
    else if (doc.containsKey("wakeUpAt")) outSystem.wake_up_at = doc["wakeUpAt"].as<String>();

    if (doc.containsKey("night_brightness")) outSystem.night_brightness = doc["night_brightness"].as<int>();
    else if (doc.containsKey("nightBrightness")) outSystem.night_brightness = doc["nightBrightness"].as<int>();

    if (doc.containsKey("idle_fighter_enabled")) outSystem.idle_fighter_enabled = doc["idle_fighter_enabled"].as<bool>();
    else if (doc.containsKey("idleFighterEnabled")) outSystem.idle_fighter_enabled = doc["idleFighterEnabled"].as<bool>();

    if (doc.containsKey("idle_fighter_interval")) outSystem.idle_fighter_interval = doc["idle_fighter_interval"].as<int>();
    else if (doc.containsKey("idleFighterInterval")) outSystem.idle_fighter_interval = doc["idleFighterInterval"].as<int>();

    if (doc.containsKey("idle_fighter_speed")) outSystem.idle_fighter_speed = doc["idle_fighter_speed"].as<int>();
    else if (doc.containsKey("idleFighterSpeed")) outSystem.idle_fighter_speed = doc["idleFighterSpeed"].as<int>();

    if (doc.containsKey("api_auth_enabled")) outSystem.api_auth_enabled = doc["api_auth_enabled"].as<bool>();
    else if (doc.containsKey("apiAuthEnabled")) outSystem.api_auth_enabled = doc["apiAuthEnabled"].as<bool>();

    if (doc.containsKey("api_token")) outSystem.api_token = doc["api_token"].as<String>();
    else if (doc.containsKey("apiToken")) outSystem.api_token = doc["apiToken"].as<String>();

    return true;
}

bool ModularConfigManager::saveSystem(const SystemConfig& system) {
    StaticJsonDocument<1024> doc;
    doc["timezone"] = system.timezone;
    doc["format24h"] = system.format24h;
    doc["lang"] = system.lang;
    doc["unit"] = system.unit;
    doc["temp_offset"] = system.temp_offset;
    doc["night_mode_enabled"] = system.night_mode_enabled;
    doc["turn_off_at"] = system.turn_off_at;
    doc["wake_up_at"] = system.wake_up_at;
    doc["night_brightness"] = system.night_brightness;
    doc["idle_fighter_enabled"] = system.idle_fighter_enabled;
    doc["idle_fighter_interval"] = system.idle_fighter_interval;
    doc["idle_fighter_speed"] = system.idle_fighter_speed;
    doc["api_auth_enabled"] = system.api_auth_enabled;
    doc["api_token"] = system.api_token;

    String out;
    serializeJsonPretty(doc, out);
    return _storage.writeStringAtomic("/config/system.json", out);
}

bool ModularConfigManager::loadNetwork(WifiConfig& outWifi, MqttConfig& outMqtt) {
    String content;
    if (!_storage.readString("/config/network.json", content)) {
        return false;
    }
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, content)) return false;

    if (doc.containsKey("wifi")) {
        JsonObjectConst w = doc["wifi"];
        if (w.containsKey("ssid")) outWifi.ssid = w["ssid"].as<String>();
        if (w.containsKey("password")) outWifi.password = w["password"].as<String>();
        if (w.containsKey("hostname")) outWifi.hostname = w["hostname"].as<String>();
    }
    if (doc.containsKey("mqtt")) {
        JsonObjectConst m = doc["mqtt"];
        outMqtt.enabled = m["enabled"] | outMqtt.enabled;
        if (m.containsKey("broker")) outMqtt.broker = m["broker"].as<String>();
        outMqtt.port = m["port"] | outMqtt.port;
        if (m.containsKey("user")) outMqtt.user = m["user"].as<String>();
        if (m.containsKey("pass")) outMqtt.pass = m["pass"].as<String>();
        if (m.containsKey("device_name")) outMqtt.deviceName = m["device_name"].as<String>();
        else if (m.containsKey("deviceName")) outMqtt.deviceName = m["deviceName"].as<String>();
        if (m.containsKey("allow_overlay")) outMqtt.allow_overlay = m["allow_overlay"].as<bool>();
        else if (m.containsKey("allowOverlay")) outMqtt.allow_overlay = m["allowOverlay"].as<bool>();
    }
    return true;
}

bool ModularConfigManager::saveNetwork(const WifiConfig& wifi, const MqttConfig& mqtt) {
    StaticJsonDocument<1024> doc;
    JsonObject w = doc.createNestedObject("wifi");
    w["ssid"] = wifi.ssid;
    w["password"] = wifi.password;
    w["hostname"] = wifi.hostname;

    JsonObject m = doc.createNestedObject("mqtt");
    m["enabled"] = mqtt.enabled;
    m["broker"] = mqtt.broker;
    m["port"] = mqtt.port;
    m["user"] = mqtt.user;
    m["pass"] = mqtt.pass;
    m["deviceName"] = mqtt.deviceName;
    m["allow_overlay"] = mqtt.allow_overlay;

    String out;
    serializeJsonPretty(doc, out);
    return _storage.writeStringAtomic("/config/network.json", out);
}

bool ModularConfigManager::loadPlaylist(std::vector<RotationEntry>& outRotation) {
    String content;
    if (!_storage.readString("/config/playlist.json", content)) {
        return false;
    }
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, content)) return false;

    outRotation.clear();
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    for (JsonObjectConst rObj : arr) {
        RotationEntry entry;
        if (rObj.containsKey("instance_id")) entry.instance_id = rObj["instance_id"].as<String>();
        else if (rObj.containsKey("instanceId")) entry.instance_id = rObj["instanceId"].as<String>();

        if (rObj.containsKey("duration_sec")) entry.duration_sec = rObj["duration_sec"].as<int>();
        else if (rObj.containsKey("durationSec")) entry.duration_sec = rObj["durationSec"].as<int>();

        if (rObj.containsKey("overlays") && rObj["overlays"].is<JsonObjectConst>() && rObj["overlays"].containsKey("fighter")) {
            entry.overlays.fighter = rObj["overlays"]["fighter"].as<bool>() ? FighterOverride::Enabled : FighterOverride::Disabled;
        } else if (rObj.containsKey("fighter_overlay")) {
            entry.overlays.fighter = rObj["fighter_overlay"].as<bool>() ? FighterOverride::Enabled : FighterOverride::Disabled;
        } else if (rObj.containsKey("fighterOverlay")) {
            entry.overlays.fighter = rObj["fighterOverlay"].as<bool>() ? FighterOverride::Enabled : FighterOverride::Disabled;
        } else {
            entry.overlays.fighter = FighterOverride::Unspecified;
        }
        outRotation.push_back(entry);
    }
    return true;
}

bool ModularConfigManager::savePlaylist(const std::vector<RotationEntry>& rotation) {
    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& rot : rotation) {
        JsonObject rObj = arr.createNestedObject();
        rObj["instance_id"] = rot.instance_id;
        rObj["duration_sec"] = rot.duration_sec;
        if (rot.overlays.fighter != FighterOverride::Unspecified) {
            JsonObject ov = rObj.createNestedObject("overlays");
            ov["fighter"] = (rot.overlays.fighter == FighterOverride::Enabled);
        }
    }

    String out;
    serializeJsonPretty(doc, out);
    return _storage.writeStringAtomic("/config/playlist.json", out);
}
