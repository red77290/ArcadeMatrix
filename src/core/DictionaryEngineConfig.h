#pragma once

#include "../../include/core/EngineContract.h"
#include <memory>
#include <map>
#include <cstring>
#include <strings.h>
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
        return findValue(key) != nullptr;
    }

    String getString(const char* key, const char* default_val = "") const override {
        const String* val = findValue(key);
        if (val) {
            return *val;
        }
        return default_val ? default_val : "";
    }

    int getInt(const char* key, int default_val = 0) const override {
        const String* val = findValue(key);
        if (val) {
            return val->toInt();
        }
        return default_val;
    }

    float getFloat(const char* key, float default_val = 0.0f) const override {
        const String* val = findValue(key);
        if (val) {
            return val->toFloat();
        }
        return default_val;
    }

    bool getBool(const char* key, bool default_val = false) const override {
        const String* val = findValue(key);
        if (val) {
            const char* s = val->c_str();
            return (strcasecmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcasecmp(s, "yes") == 0);
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
    const String* findValue(const char* key) const {
        if (!dict || !key) return nullptr;
        for (const auto& kv : *dict) {
            if (strcmp(kv.first.c_str(), key) == 0) {
                return &kv.second;
            }
        }
        return nullptr;
    }

    void ensureUnique() {
        if (!dict) {
            dict = std::make_shared<std::map<String, String>>();
        } else if (dict.use_count() > 1) {
            dict = std::make_shared<std::map<String, String>>(*dict);
        }
    }

    std::shared_ptr<std::map<String, String>> dict;
};
