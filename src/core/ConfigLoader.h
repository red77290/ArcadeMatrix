#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
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

struct EngineInstanceSnapshot {
    String instance_id;
    String engine_id;
    DictionaryEngineConfig config;
};

struct ConfigSnapshot {
    uint32_t version = 1;
    MatrixConfig matrix;
    WifiConfig wifi;
    MqttConfig mqtt;
    SystemConfig system;
    std::vector<RotationEntry> rotation;
    std::vector<EngineInstanceSnapshot> instances;

    const EngineInstanceSnapshot* getInstance(const String& instanceId) const {
        for (const auto& inst : instances) {
            if (inst.instance_id == instanceId) {
                return &inst;
            }
        }
        return nullptr;
    }
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

    friend class ConfigSanitizer;

    /**
     * @brief Triple-buffered lock-free snapshot reader for Core 1 (Zero allocations, zero mutex, zero copy).
     */
    inline const ConfigSnapshot& getSnapshot() const {
        uint8_t idx = _readIndex.load(std::memory_order_acquire);
        return _snapshots[idx];
    }

    /**
     * @brief Returns current configuration generation version lock-free.
     */
    inline uint32_t getVersion() const {
        return _configVersion.load(std::memory_order_relaxed);
    }

    void publishSnapshot();

    /**
     * @brief Thread-safe querying of instance snapshot.
     */
    bool getInstanceSnapshot(const String& instanceId, EngineInstanceSnapshot& out) const {
        const ConfigSnapshot& snap = getSnapshot();
        const EngineInstanceSnapshot* inst = snap.getInstance(instanceId);
        if (inst) {
            out = *inst;
            return true;
        }
        return false;
    }

    bool hasInstance(const String& instanceId) const {
        const ConfigSnapshot& snap = getSnapshot();
        return snap.getInstance(instanceId) != nullptr;
    }

    /**
     * @brief Thread-safe transactional mutation block. Locks mutex, runs mutation, and publishes.
     */
    template <typename F>
    void mutate(F&& mutator) {
        std::lock_guard<std::mutex> lock(_mutex);
        mutator(*this);
        publishSnapshot_locked();
    }

    bool addInstance(const String& instanceId, const String& engineId) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& inst : instances) {
            if (inst.instance_id == instanceId) return false;
        }
        instances.push_back({instanceId, engineId, {}});
        publishSnapshot_locked();
        return true;
    }

    bool removeInstance(const String& instanceId) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto it = instances.begin(); it != instances.end(); ++it) {
            if (it->instance_id == instanceId) {
                instances.erase(it);
                publishSnapshot_locked();
                return true;
            }
        }
        return false;
    }

private:
    mutable std::mutex _mutex;
    std::atomic<uint32_t> _configVersion{1};
    std::atomic<uint8_t> _readIndex{0};
    ConfigSnapshot _snapshots[3];

    void publishSnapshot_locked();
};
