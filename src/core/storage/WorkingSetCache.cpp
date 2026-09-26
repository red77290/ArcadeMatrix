/**
 * @file WorkingSetCache.cpp
 * @brief Implementation of WorkingSetCache.
 */
#include "WorkingSetCache.h"
#include "../Logger.h"
#include <ArduinoJson.h>

WorkingSetCache::WorkingSetCache(IConfigStorage& storage, size_t maxActiveInstances)
    : _storage(storage)
    , _maxActiveInstances(maxActiveInstances)
{
    _cache.reserve(_maxActiveInstances);
}

bool WorkingSetCache::syncWithPlaylist(const std::vector<RotationEntry>& playlist) {
    std::vector<EngineInstance> newCache;
    newCache.reserve(_maxActiveInstances);

    for (const auto& rot : playlist) {
        if (rot.instance_id.isEmpty()) continue;
        if (newCache.size() >= _maxActiveInstances) {
            LOGW("WorkingSetCache", "Max active instances (%u) reached; skipping instance '%s'",
                 (unsigned)_maxActiveInstances, rot.instance_id.c_str());
            break;
        }

        // Check if already in newCache
        bool alreadyIncluded = false;
        for (const auto& inst : newCache) {
            if (inst.instance_id == rot.instance_id) {
                alreadyIncluded = true;
                break;
            }
        }
        if (alreadyIncluded) continue;

        // Check existing cache first
        const EngineInstance* existing = getInstance(rot.instance_id);
        if (existing) {
            newCache.push_back(*existing);
        } else {
            // Load from storage
            EngineInstance loaded;
            if (loadInstanceFromStorage(rot.instance_id, loaded)) {
                newCache.push_back(loaded);
            } else {
                // If file doesn't exist yet, construct empty default instance
                EngineInstance fallback;
                fallback.instance_id = rot.instance_id;
                int under = rot.instance_id.indexOf('_');
                fallback.engine_id = (under > 0) ? rot.instance_id.substring(0, under) : rot.instance_id;
                newCache.push_back(fallback);
                // Persist the default instance
                writeInstanceToStorage(fallback);
            }
        }
    }

    _cache = std::move(newCache);
    LOGI("WorkingSetCache", "Working set synchronized: %u active instances in DRAM cache", (unsigned)_cache.size());
    return true;
}

const EngineInstance* WorkingSetCache::getInstance(const String& instanceId) const {
    for (const auto& inst : _cache) {
        if (inst.instance_id == instanceId) {
            return &inst;
        }
    }
    return nullptr;
}

EngineInstance* WorkingSetCache::getMutableInstance(const String& instanceId) {
    for (auto& inst : _cache) {
        if (inst.instance_id == instanceId) {
            return &inst;
        }
    }
    return nullptr;
}

bool WorkingSetCache::saveAndCacheInstance(const EngineInstance& instance) {
    // 1. Write to storage
    if (!writeInstanceToStorage(instance)) {
        return false;
    }

    // 2. Update or insert in cache
    for (auto& inst : _cache) {
        if (inst.instance_id == instance.instance_id) {
            inst = instance;
            return true;
        }
    }

    if (_cache.size() < _maxActiveInstances) {
        _cache.push_back(instance);
    }
    return true;
}

bool WorkingSetCache::deleteInstance(const String& instanceId) {
    // 1. Remove from cache
    for (auto it = _cache.begin(); it != _cache.end(); ++it) {
        if (it->instance_id == instanceId) {
            _cache.erase(it);
            break;
        }
    }

    // 2. Remove file from storage
    String path = "/config/instances/" + instanceId + ".json";
    return _storage.remove(path.c_str());
}

bool WorkingSetCache::loadTransientInstance(const String& instanceId, EngineInstance& outInstance) {
    const EngineInstance* cached = getInstance(instanceId);
    if (cached) {
        outInstance = *cached;
        return true;
    }
    return loadInstanceFromStorage(instanceId, outInstance);
}

void WorkingSetCache::exportSnapshots(std::vector<EngineInstanceSnapshot>& outSnapshots) const {
    outSnapshots.clear();
    outSnapshots.reserve(_cache.size());
    for (const auto& inst : _cache) {
        EngineInstanceSnapshot s;
        s.instance_id = inst.instance_id;
        s.engine_id = inst.engine_id;
        s.config = inst.config;
        outSnapshots.push_back(s);
    }
}

bool WorkingSetCache::loadInstanceFromStorage(const String& instanceId, EngineInstance& outInstance) {
    String path = "/config/instances/" + instanceId + ".json";
    String content;
    if (!_storage.readString(path.c_str(), content)) {
        return false;
    }

    StaticJsonDocument<1536> doc;
    DeserializationError err = deserializeJson(doc, content);
    if (err) {
        LOGE("WorkingSetCache", "JSON parse error in %s: %s", path.c_str(), err.c_str());
        return false;
    }

    outInstance.instance_id = doc["instance_id"] | instanceId;
    outInstance.engine_id = doc["engine_id"] | "";
    if (doc.containsKey("config") && doc["config"].is<JsonObjectConst>()) {
        JsonObjectConst confObj = doc["config"].as<JsonObjectConst>();
        for (JsonPairConst kv : confObj) {
            if (kv.value().is<int>()) {
                outInstance.config.setInt(kv.key().c_str(), kv.value().as<int>());
            } else if (kv.value().is<float>()) {
                outInstance.config.setString(kv.key().c_str(), String(kv.value().as<float>()));
            } else if (kv.value().is<bool>()) {
                outInstance.config.setBool(kv.key().c_str(), kv.value().as<bool>());
            } else {
                outInstance.config.setString(kv.key().c_str(), kv.value().as<String>());
            }
        }
    }
    return true;
}

bool WorkingSetCache::writeInstanceToStorage(const EngineInstance& instance) {
    _storage.mkdir("/config");
    _storage.mkdir("/config/instances");

    StaticJsonDocument<1536> doc;
    doc["instance_id"] = instance.instance_id;
    doc["engine_id"] = instance.engine_id;
    JsonObject confNode = doc.createNestedObject("config");
    for (const auto& kv : instance.config.getDictionary()) {
        confNode[kv.first] = kv.second;
    }

    String output;
    serializeJsonPretty(doc, output);

    String path = "/config/instances/" + instance.instance_id + ".json";
    return _storage.writeStringAtomic(path.c_str(), output);
}
