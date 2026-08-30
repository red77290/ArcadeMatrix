#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "DictionaryEngineConfig.h"

struct EngineInstance {
    String instance_id;
    String engine_id;
    DictionaryEngineConfig config;
};

struct OverlayConfig {
    bool fighter;

    OverlayConfig() : fighter(false) {}
    explicit OverlayConfig(bool f) : fighter(f) {}
};

struct RotationEntry {
    String instance_id;
    int duration_sec;
    OverlayConfig overlays;

    RotationEntry() : instance_id(""), duration_sec(15), overlays() {}
    RotationEntry(String id, int dur = 15, OverlayConfig ov = OverlayConfig{})
        : instance_id(id), duration_sec(dur), overlays(ov) {}
};

struct MatrixConfig {
    int width;
    int height;
    String panelType;
    int chainLength;
    int powerLimitPercent;
    bool forceSingleBuffer;
    int colorDepth;
    String rgbSequence;
    int limitRefreshRateHz;
    String driverChip;
    bool clkPhase;
    int latchBlanking;
    int rowAddressMode;
    bool matrix_power = true;
    int rotation_offset = 0;
    bool auto_rotate = true;
    String rotation_transition = "vortex";
    int rotation_transition_duration_ms = 400;
};

struct WifiConfig {
    String ssid;
    String password;
    String hostname;
};

struct MqttConfig {
    bool enabled;
    String broker;
    int port;
    String user;
    String pass;
    String deviceName;
    String topic_batocera;
    String topic_recalbox;
};

struct SystemConfig {
    String timezone;
    bool format24h;
    String lang;
    String unit;
    float temp_offset;
    bool night_mode_enabled;
    String turn_off_at;
    String wake_up_at;
    int night_brightness;
    bool idle_fighter_enabled = false;
    int idle_fighter_interval = 60;
};

class ConfigLoader {
public:
    ConfigLoader();
    
    void setDefaults();
    
    bool parseFromJson(const char* jsonContent);
    bool parseFromJsonDoc(const DynamicJsonDocument& doc);
    String serializeToJson(bool pretty = false) const;
    
    bool loadFromSD(const char* filepath);
    bool saveToSD(const char* filepath);
    
    std::vector<EngineInstance> instances;
    std::vector<RotationEntry> rotation;

    MatrixConfig matrix;
    WifiConfig wifi;
    MqttConfig mqtt;
    SystemConfig system;
    
    EngineInstance* getInstance(const String& instanceId) {
        for (auto& inst : instances) {
            if (inst.instance_id == instanceId) {
                return &inst;
            }
        }
        return nullptr;
    }

    EngineInstance* addInstance(const String& instanceId, const String& engineId) {
        EngineInstance* existing = getInstance(instanceId);
        if (existing) return existing;
        instances.push_back({instanceId, engineId, {}});
        return &instances.back();
    }

    bool removeInstance(const String& instanceId) {
        for (auto it = instances.begin(); it != instances.end(); ++it) {
            if (it->instance_id == instanceId) {
                instances.erase(it);
                return true;
            }
        }
        return false;
    }
};
