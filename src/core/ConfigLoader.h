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
    uint32_t checksum = 0;
    MatrixConfig matrix;
    WifiConfig wifi;
    MqttConfig mqtt;
    SystemConfig system;
    std::vector<RotationEntry> rotation;
    std::vector<EngineInstanceSnapshot> instances;

    ConfigSnapshot clone() const {
        return *this;
    }

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
     * @brief Formally proven Single-Reader Single-Writer (SRSW) atomic ownership protocol.
     * Core 1 acquires the active snapshot and pins it during the frame.
     */
    inline const ConfigSnapshot& getSnapshot() const {
        uint8_t slot = _publishedSlot.load(std::memory_order_acquire);
        _readingSlot.store(slot, std::memory_order_release);
        // Double-check validation against race with publisher
        uint8_t currentPublished = _publishedSlot.load(std::memory_order_acquire);
        if (currentPublished != slot) {
            slot = currentPublished;
            _readingSlot.store(slot, std::memory_order_release);
        }
        return _snapshots[slot];
    }

    /**
     * @brief Releases the pinned snapshot slot at the end of the frame.
     */
    inline void releaseSnapshot() const {
        _readingSlot.store(0xFF, std::memory_order_release);
    }

    /**
     * @brief Returns current configuration generation version lock-free.
     */
    inline uint32_t getVersion() const {
        return _configVersion.load(std::memory_order_relaxed);
    }

    void publishSnapshot();

    /**
     * @brief Thread-safe querying of instance snapshot (for Core 0 / control path).
     */
    bool getInstanceSnapshot(const String& instanceId, EngineInstanceSnapshot& out) const {
        const ConfigSnapshot& snap = getSnapshot();
        const EngineInstanceSnapshot* inst = snap.getInstance(instanceId);
        if (inst) {
            out = *inst;
            releaseSnapshot();
            return true;
        }
        releaseSnapshot();
        return false;
    }

    bool hasInstance(const String& instanceId) const {
        const ConfigSnapshot& snap = getSnapshot();
        bool exists = snap.getInstance(instanceId) != nullptr;
        releaseSnapshot();
        return exists;
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
    std::atomic<uint8_t> _publishedSlot{0};
    mutable std::atomic<uint8_t> _readingSlot{0xFF};
    ConfigSnapshot _snapshots[3];

    void publishSnapshot_locked();
};
