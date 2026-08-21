#pragma once

#include "../../include/core/EngineContract.h"
#include <map>
#include <Arduino.h>

class DictionaryEngineConfig : public EngineConfig {
public:
    virtual ~DictionaryEngineConfig() = default;

    String getString(const char* key, const char* default_val = "") const override {
        auto it = dict.find(String(key));
        if (it != dict.end()) {
            return it->second;
        }
        return default_val;
    }

    int getInt(const char* key, int default_val = 0) const override {
        auto it = dict.find(String(key));
        if (it != dict.end()) {
            return it->second.toInt();
        }
        return default_val;
    }

    float getFloat(const char* key, float default_val = 0.0f) const override {
        auto it = dict.find(String(key));
        if (it != dict.end()) {
            return it->second.toFloat();
        }
        return default_val;
    }

    bool getBool(const char* key, bool default_val = false) const override {
        auto it = dict.find(String(key));
        if (it != dict.end()) {
            String v = it->second;
            v.toLowerCase();
            return (v == "true" || v == "1" || v == "yes");
        }
        return default_val;
    }

    void setString(const char* key, const String& value) {
        dict[String(key)] = value;
    }

    void setInt(const char* key, int value) {
        dict[String(key)] = String(value);
    }

    void setBool(const char* key, bool value) {
        dict[String(key)] = value ? "true" : "false";
    }
    
    const std::map<String, String>& getDictionary() const {
        return dict;
    }

private:
    std::map<String, String> dict;
};
