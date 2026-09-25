/**
 * @file WorkingSetCache.h
 * @brief Flyweight Working-Set RAM Cache for active engine instances.
 *
 * Enforces bounded DRAM footprint on classic ESP32 (no PSRAM) by caching ONLY
 * instances actively referenced in the rotation playlist. Zero SD card reads
 * occur during playback.
 */
#pragma once

#include <Arduino.h>
#include <vector>
#include <memory>
#include "../ConfigLoader.h"
#include "IConfigStorage.h"

class WorkingSetCache {
public:
    explicit WorkingSetCache(IConfigStorage& storage, size_t maxActiveInstances = 12);
    ~WorkingSetCache() = default;

    /**
     * @brief Synchronize the RAM cache with the active playlist.
     * Loads newly added instances from SD and evicts unused ones to free DRAM.
     *
     * @param playlist Active rotation sequence.
     * @return true if all required instances were successfully loaded or cached.
     */
    bool syncWithPlaylist(const std::vector<RotationEntry>& playlist);

    /**
     * @brief Get a cached instance by ID.
     * @param instanceId The instance identifier.
     * @return Pointer to instance, or nullptr if not in working set.
     */
    const EngineInstance* getInstance(const String& instanceId) const;

    /**
     * @brief Get a mutable pointer to a cached instance by ID.
     */
    EngineInstance* getMutableInstance(const String& instanceId);

    /**
     * @brief Put or update an instance directly in cache and persist to storage.
     */
    bool saveAndCacheInstance(const EngineInstance& instance);

    /**
     * @brief Delete an instance from cache and storage.
     */
    bool deleteInstance(const String& instanceId);

    /**
     * @brief Load a transient instance for priority alerts / preemption without polluting playlist.
     */
    bool loadTransientInstance(const String& instanceId, EngineInstance& outInstance);

    /**
     * @brief Export all cached instances as snapshots for the SRSW Triple-Buffer.
     */
    void exportSnapshots(std::vector<EngineInstanceSnapshot>& outSnapshots) const;

    /**
     * @brief Get all currently cached instances.
     */
    const std::vector<EngineInstance>& getCachedInstances() const { return _cache; }

    /**
     * @brief Clear all cached instances in RAM.
     */
    void clearCache() { _cache.clear(); }

private:
    IConfigStorage& _storage;
    size_t _maxActiveInstances;
    std::vector<EngineInstance> _cache;

    bool loadInstanceFromStorage(const String& instanceId, EngineInstance& outInstance);
    bool writeInstanceToStorage(const EngineInstance& instance);
};
