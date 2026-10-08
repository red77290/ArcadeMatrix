#pragma once

#include "../../include/core/EngineContract.h"
#include <memory>
#include <map>
#include <Arduino.h>

class DictionaryEngineConfig : public EngineConfig {
public:
    DictionaryEngineConfig() : dict(std::make_shared<std::map<String, String>>()) {}
    virtual ~DictionaryEngineConfig() = default;

    // Shallow copy sharing the map pointer: ZERO dynamic allocation during snapshot publish
    DictionaryEngineConfig(const DictionaryEngineConfig& other) = default;
    DictionaryEngineConfig& operator=(const DictionaryEngineConfig& other) = default;
    DictionaryEngineConfig(DictionaryEngineConfig&& other) noexcept = default;
    DictionaryEngineConfig& operator=(DictionaryEngineConfig&& other) noexcept = default;

    bool hasKey(const char* key) const {
        if (!dict) return false;
        return dict->find(String(key)) != dict->end();
    }

    String getString(const char* key, const char* default_val = "") const override {
        if (!dict) return default_val;
        auto it = dict->find(String(key));
        if (it != dict->end()) {
            return it->second;
        }
        return default_val;
    }

    int getInt(const char* key, int default_val = 0) const override {
        if (!dict) return default_val;
        auto it = dict->find(String(key));
        if (it != dict->end()) {
            return it->second.toInt();
        }
        return default_val;
    }

    float getFloat(const char* key, float default_val = 0.0f) const override {
        if (!dict) return default_val;
        auto it = dict->find(String(key));
        if (it != dict->end()) {
            return it->second.toFloat();
        }
        return default_val;
    }

    bool getBool(const char* key, bool default_val = false) const override {
        if (!dict) return default_val;
        auto it = dict->find(String(key));
        if (it != dict->end()) {
            String v = it->second;
            v.toLowerCase();
            return (v == "true" || v == "1" || v == "yes");
        }
        return default_val;
    }

    void setString(const char* key, const String& value) {
        ensureUnique();
        (*dict)[String(key)] = value;
    }

    void setInt(const char* key, int value) {
        ensureUnique();
        (*dict)[String(key)] = String(value);
    }

    void setBool(const char* key, bool value) {
        ensureUnique();
        (*dict)[String(key)] = value ? "true" : "false";
    }

    void clear() {
        if (dict && dict.use_count() == 1) {
            dict->clear();
        } else {
            dict = std::make_shared<std::map<String, String>>();
        }
    }

    const std::map<String, String>& getDictionary() const {
        static const std::map<String, String> s_empty;
        return dict ? *dict : s_empty;
    }

private:
    void ensureUnique() {
        if (!dict) {
            dict = std::make_shared<std::map<String, String>>();
        } else if (dict.use_count() > 1) {
            dict = std::make_shared<std::map<String, String>>(*dict);
        }
    }

    std::shared_ptr<std::map<String, String>> dict;
};
