#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include "DictionaryEngineConfig.h"
#include "SpiRamJsonDocument.h"

struct EngineInstance {
    String instance_id;
    String engine_id;
    DictionaryEngineConfig config;
};

enum class FighterOverride : uint8_t {
    Unspecified = 0, ///< Field absent in playlist entry: inherit global setting
    Disabled    = 1, ///< Field explicitly "fighter": false in item
    Enabled     = 2  ///< Field explicitly "fighter": true in item
};

struct OverlayConfig {
    FighterOverride fighter;

    OverlayConfig() : fighter(FighterOverride::Unspecified) {}
    explicit OverlayConfig(FighterOverride f) : fighter(f) {}
    explicit OverlayConfig(bool f) : fighter(f ? FighterOverride::Enabled : FighterOverride::Disabled) {}
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
    bool allow_overlay = false;
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
    bool idle_fighter_enabled = true;
    int idle_fighter_interval = 60;
    int idle_fighter_speed = 100;
    bool api_auth_enabled = false;
    String api_token = "";
};

struct EngineInstanceSnapshot {
    String instance_id;
    String engine_id;
    DictionaryEngineConfig config;
};

struct ConfigSnapshot {
    static constexpr uint32_t MAGIC_START = 0x5A5A5A5A;
    static constexpr uint32_t MAGIC_END = 0xA5A5A5A5;

    uint32_t magic_start = MAGIC_START;
    uint32_t version = 1;
    uint32_t crc32 = 0;
    MatrixConfig matrix;
    WifiConfig wifi;
    MqttConfig mqtt;
    SystemConfig system;
    std::vector<RotationEntry> rotation;
    std::vector<EngineInstanceSnapshot> instances;
    uint32_t magic_end = MAGIC_END;

    static uint32_t calculateCRC32(uint32_t ver, size_t numInstances) {
        uint32_t val[2] = { ver, static_cast<uint32_t>(numInstances) };
        uint32_t crc = 0xFFFFFFFF;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(val);
        for (size_t i = 0; i < sizeof(val); ++i) {
            crc ^= p[i];
            for (int b = 0; b < 8; ++b) {
                crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
            }
        }
        return ~crc;
    }

    bool isValid() const {
        return magic_start == MAGIC_START && magic_end == MAGIC_END && crc32 == calculateCRC32(version, instances.size());
    }

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

/**
 * @enum SlotState
 * @brief Formal state model of the SRSW triple-buffer slots (Invariant 2).
 * Documents the linearizable atomic lifecycle: FREE -> WRITING -> PUBLISHED -> READING.
 */
enum class SlotState : uint8_t {
    FREE = 0,        // Available for writer reservation
    WRITING = 1,     // Reserved and in progress of construction by Core 0
    PUBLISHED = 2,   // Valid stable snapshot available for readers
    READING = 3      // Active pinned read by Core 1 SnapshotGuard
};

class ConfigLoader;

/**
 * @brief Move-only RAII Guard for borrowing immutable ConfigSnapshot.
 * Strictly non-copyable with explicit ownership disarming on move, guaranteeing linearizable lifetime.
 */
class ConfigSnapshotGuard {
public:
    inline ConfigSnapshotGuard(const ConfigLoader& loader, const ConfigSnapshot& snapshot, uint8_t slot)
        : _loader(&loader), _snapshot(&snapshot), _slot(slot) {}

    inline ConfigSnapshotGuard(ConfigSnapshotGuard&& other) noexcept
        : _loader(other._loader), _snapshot(other._snapshot), _slot(other._slot) {
        other._slot = 0xFF; // Disarm other
    }

    inline ~ConfigSnapshotGuard();

    inline const ConfigSnapshot& get() const { return *_snapshot; }
    inline const ConfigSnapshot* operator->() const { return _snapshot; }
    inline const ConfigSnapshot& operator*() const { return *_snapshot; }

    ConfigSnapshotGuard(const ConfigSnapshotGuard&) = delete;
    ConfigSnapshotGuard& operator=(const ConfigSnapshotGuard&) = delete;
    ConfigSnapshotGuard& operator=(ConfigSnapshotGuard&&) = delete;

private:
    const ConfigLoader* _loader = nullptr;
    const ConfigSnapshot* _snapshot = nullptr;
    uint8_t _slot = 0xFF;
};

class ConfigLoader {
public:
    ConfigLoader();
    
    void setDefaults();
    
    bool parseFromJson(const char* jsonContent);
    bool parseFromJsonDoc(const JsonDocument& doc);
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
    friend class ConfigSnapshotGuard;

    /**
     * @brief Linearizable atomic reservation of the current snapshot via CAS.
     * Core 1 dereferences the snapshot payload ONLY after CAS PUBLISHED -> READING succeeds.
     * This is the EXCLUSIVE public accessor to ConfigSnapshot.
     */
    ConfigSnapshotGuard acquireSnapshot() const;

    inline uint32_t getVersion() const {
        return _configVersion.load(std::memory_order_relaxed);
    }

    void publishSnapshot();

    /**
     * @brief Completes any pending deferred publication when executed on Core 0.
     * Complies with Invariant 2. Returns true if publication occurred.
     */
    bool checkDeferredPublish();

    bool getInstanceSnapshot(const String& instanceId, EngineInstanceSnapshot& out) const {
        ConfigSnapshotGuard guard = acquireSnapshot();
        const EngineInstanceSnapshot* inst = guard->getInstance(instanceId);
        if (inst) {
            out = *inst;
            return true;
        }
        return false;
    }

    bool hasInstance(const String& instanceId) const {
        ConfigSnapshotGuard guard = acquireSnapshot();
        return guard->getInstance(instanceId) != nullptr;
    }

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
    void releaseSnapshot(uint8_t slot) const;

    mutable std::mutex _mutex;
    std::atomic<uint32_t> _configVersion{1};
    mutable std::atomic<uint8_t> _publishedSlot{0};
    mutable std::atomic<uint32_t> _readers[3];
    mutable std::atomic<bool> _publishPending{false};
    ConfigSnapshot _snapshots[3];

    // Reused for every serializeToJson()/loadFromSD() call instead of allocating a fresh
    // 32KB DynamicJsonDocument each time. Every WebUI settings change ends up calling
    // saveToSD() -> serializeToJson(), and a transient 32KB heap allocation on every single
    // save (observed several times per second during WebUI editing sessions) was fragmenting
    // the ESP32 DRAM heap irrecoverably: freed 32KB blocks got carved up by unrelated
    // WebServer/JSON-parsing allocations before the next save needed the space again, so free
    // heap AND largest-contiguous-block both trended down permanently across a session. Both
    // saveToSD() (serialize) and loadFromSD() (deserialize) are only ever called from a single
    // context at a time (save is always under sdMutex, load only runs once at boot before any
    // other task touches config), so sharing one scratch buffer is safe.
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    mutable SpiRamJsonDocument _jsonScratch{32768};
#else
    mutable SpiRamJsonDocument _jsonScratch{6144};
#endif

    void publishSnapshot_locked();
};

inline ConfigSnapshotGuard::~ConfigSnapshotGuard() {
    if (_loader && _slot != 0xFF) {
        _loader->releaseSnapshot(_slot);
    }
}


