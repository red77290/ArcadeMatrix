#pragma once
#include "core/EngineContract.h"
#include "core/ConfigLoader.h"
#include "Logger.h"
#include <map>
#include <string>

/**
 * @brief Temporary adapter to convert legacy ConfigLoader structs into the generic EngineConfig interface.
 * This ensures engines don't directly depend on ConfigLoader during the migration phase.
 */
class LegacyConfigAdapter : public EngineConfig {
public:
    LegacyConfigAdapter(const ConfigLoader& config, const String& engineType) 
        : m_config(config), m_engineType(engineType) {}

    String getString(const char* key, const char* default_val = "") const override {
        String k = String(key);
        if (m_engineType == "date") {
            if (k == "format") return m_config.dateSettings.format;
            if (k == "date_font_path") return m_config.dateSettings.date_font_path;
            if (k == "date_color_1") return m_config.dateSettings.date_color_1;
            if (k == "date_color_2") return m_config.dateSettings.date_color_2;
        } else if (m_engineType == "clock") {
            if (k == "timezone") return m_config.time.timezone;
            if (k == "clock_font_path") return m_config.time.clock_font_path;
            if (k == "clock_color_1") return m_config.time.clock_color_1;
            if (k == "clock_color_2") return m_config.time.clock_color_2;
        }
        return default_val;
    }

    int getInt(const char* key, int default_val = 0) const override {
        String k = String(key);
        if (m_engineType == "date") {
            if (k == "theme") return m_config.dateSettings.theme;
            if (k == "date_font") return m_config.dateSettings.date_font;
            if (k == "date_size") return m_config.dateSettings.date_size;
            if (k == "date_offset_x") return m_config.dateSettings.date_offset_x;
            if (k == "date_offset_y") return m_config.dateSettings.date_offset_y;
        } else if (m_engineType == "clock") {
            if (k == "clock_theme") return m_config.time.clock_theme;
            if (k == "clock_font") return m_config.time.clock_font;
            if (k == "clock_size") return m_config.time.clock_size;
            if (k == "clock_offset_x") return m_config.time.clock_offset_x;
            if (k == "clock_offset_y") return m_config.time.clock_offset_y;
        }
        return default_val;
    }

    float getFloat(const char* key, float default_val = 0.0f) const override {
        return default_val; // For now legacy adapter doesn't need to serve float
    }

    bool getBool(const char* key, bool default_val = false) const override {
        String k = String(key);
        if (m_engineType == "clock") {
            if (k == "format24h") return m_config.time.format24h;
        }
        return default_val;
    }

private:
    const ConfigLoader& m_config;
    String m_engineType;
};
