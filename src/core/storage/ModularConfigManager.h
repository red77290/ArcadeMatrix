/**
 * @file ModularConfigManager.h
 * @brief Coordinates domain-partitioned persistence and legacy config migration.
 */
#pragma once

#include "IConfigStorage.h"
#include "WorkingSetCache.h"
#include "../ConfigLoader.h"
#include <memory>

class ModularConfigManager {
public:
    explicit ModularConfigManager(IConfigStorage& storage);
    ~ModularConfigManager() = default;

    /**
     * @brief Check for legacy /config.json and migrate to /config/ domain files if needed.
     * Prevents breaking changes during OTA upgrades and splits monolithic configs cleanly.
     */
    bool checkAndMigrateLegacy(ConfigLoader& config, const char* legacyPath = "/config.json");

    /**
     * @brief Load all domain configurations and active working set from /config/.
     */
    bool loadAll(ConfigLoader& config);

    /**
     * @brief Save all domain configurations and active working set to /config/.
     */
    bool saveAll(const ConfigLoader& config);

    // Individual domain load/save methods
    bool loadHardware(MatrixConfig& outMatrix);
    bool saveHardware(const MatrixConfig& matrix);

    bool loadSystem(SystemConfig& outSystem);
    bool saveSystem(const SystemConfig& system);

    bool loadNetwork(WifiConfig& outWifi, MqttConfig& outMqtt);
    bool saveNetwork(const WifiConfig& wifi, const MqttConfig& mqtt);

    bool loadPlaylist(std::vector<RotationEntry>& outRotation);
    bool savePlaylist(const std::vector<RotationEntry>& rotation);

    WorkingSetCache& workingSet() { return _workingSet; }
    IConfigStorage& storage() { return _storage; }

private:
    IConfigStorage& _storage;
    WorkingSetCache _workingSet;
};
