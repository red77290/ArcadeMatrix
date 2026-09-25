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
    outMatrix.chainLength = doc["chain_length"] | outMatrix.chainLength;
    outMatrix.powerLimitPercent = doc["power_limit_percent"] | outMatrix.powerLimitPercent;
    outMatrix.forceSingleBuffer = doc["force_single_buffer"] | outMatrix.forceSingleBuffer;
    outMatrix.colorDepth = doc["color_depth"] | outMatrix.colorDepth;
    if (doc.containsKey("rgb_sequence")) outMatrix.rgbSequence = doc["rgb_sequence"].as<String>();
    else if (doc.containsKey("rgbSequence")) outMatrix.rgbSequence = doc["rgbSequence"].as<String>();
    outMatrix.limitRefreshRateHz = doc["limit_refresh_rate_hz"] | outMatrix.limitRefreshRateHz;
    if (doc.containsKey("driver_chip")) outMatrix.driverChip = doc["driver_chip"].as<String>();
    else if (doc.containsKey("driverChip")) outMatrix.driverChip = doc["driverChip"].as<String>();
    outMatrix.clkPhase = doc["clk_phase"] | outMatrix.clkPhase;
    outMatrix.latchBlanking = doc["latch_blanking"] | outMatrix.latchBlanking;
    outMatrix.rowAddressMode = doc["row_address_mode"] | outMatrix.rowAddressMode;
    outMatrix.rotation_offset = doc["rotation_offset"] | outMatrix.rotation_offset;
    outMatrix.auto_rotate = doc["auto_rotate"] | outMatrix.auto_rotate;
    if (doc.containsKey("rotation_transition")) outMatrix.rotation_transition = doc["rotation_transition"].as<String>();
    outMatrix.rotation_transition_duration_ms = doc["rotation_transition_duration_ms"] | outMatrix.rotation_transition_duration_ms;
    outMatrix.matrix_power = doc["matrix_power"] | outMatrix.matrix_power;
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
    outSystem.format24h = doc["format24h"] | outSystem.format24h;
    if (doc.containsKey("lang")) outSystem.lang = doc["lang"].as<String>();
    if (doc.containsKey("unit")) outSystem.unit = doc["unit"].as<String>();
    outSystem.temp_offset = doc["temp_offset"] | outSystem.temp_offset;
    outSystem.night_mode_enabled = doc["night_mode_enabled"] | outSystem.night_mode_enabled;
    if (doc.containsKey("turn_off_at")) outSystem.turn_off_at = doc["turn_off_at"].as<String>();
    if (doc.containsKey("wake_up_at")) outSystem.wake_up_at = doc["wake_up_at"].as<String>();
    outSystem.night_brightness = doc["night_brightness"] | outSystem.night_brightness;
    outSystem.idle_fighter_enabled = doc["idle_fighter_enabled"] | outSystem.idle_fighter_enabled;
    outSystem.idle_fighter_interval = doc["idle_fighter_interval"] | outSystem.idle_fighter_interval;
    outSystem.idle_fighter_speed = doc["idle_fighter_speed"] | outSystem.idle_fighter_speed;
    outSystem.api_auth_enabled = doc["api_auth_enabled"] | outSystem.api_auth_enabled;
    if (doc.containsKey("api_token")) outSystem.api_token = doc["api_token"].as<String>();
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
        if (m.containsKey("deviceName")) outMqtt.deviceName = m["deviceName"].as<String>();
        outMqtt.allow_overlay = m["allow_overlay"] | outMqtt.allow_overlay;
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
        entry.instance_id = rObj["instance_id"] | "";
        entry.duration_sec = rObj["duration_sec"] | 15;
        if (rObj.containsKey("overlays") && rObj["overlays"].is<JsonObjectConst>() && rObj["overlays"].containsKey("fighter")) {
            entry.overlays.fighter = rObj["overlays"]["fighter"].as<bool>() ? FighterOverride::Enabled : FighterOverride::Disabled;
        } else if (rObj.containsKey("fighter_overlay")) {
            entry.overlays.fighter = rObj["fighter_overlay"].as<bool>() ? FighterOverride::Enabled : FighterOverride::Disabled;
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
