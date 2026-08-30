#include "ConfigSanitizer.h"
#include "Logger.h"

SanitizeResult ConfigSanitizer::sanitize(ConfigLoader& config) {
    SanitizeResult result;
    
    // 1. Sanitize Matrix / Display Config (Keep user values if set, provide sensible lower bounds)
    if (config.matrix.width < 16) {
        config.matrix.width = 64;
        result.values_clamped++;
        result.modified = true;
    }
    if (config.matrix.height < 16) {
        config.matrix.height = 32;
        result.values_clamped++;
        result.modified = true;
    }
    if (config.matrix.chainLength < 1) {
        config.matrix.chainLength = 1;
        result.values_clamped++;
        result.modified = true;
    }
    if (config.matrix.powerLimitPercent < 1 || config.matrix.powerLimitPercent > 100) {
        config.matrix.powerLimitPercent = constrain(config.matrix.powerLimitPercent, 1, 100);
        result.values_clamped++;
        result.modified = true;
    }
    if (config.matrix.colorDepth < 1 || config.matrix.colorDepth > 11) {
        config.matrix.colorDepth = constrain(config.matrix.colorDepth, 1, 11);
        result.values_clamped++;
        result.modified = true;
    }
    if (config.matrix.limitRefreshRateHz < 30 || config.matrix.limitRefreshRateHz > 240) {
        config.matrix.limitRefreshRateHz = constrain(config.matrix.limitRefreshRateHz, 30, 240);
        result.values_clamped++;
        result.modified = true;
    }
    if (config.matrix.rotation_offset < 0 || config.matrix.rotation_offset > 3) {
        config.matrix.rotation_offset = (config.matrix.rotation_offset % 4 + 4) % 4;
        result.values_clamped++;
        result.modified = true;
    }
    if (config.matrix.latchBlanking < 1 || config.matrix.latchBlanking > 32) {
        config.matrix.latchBlanking = 4;
        result.values_clamped++;
        result.modified = true;
    }
    if (config.matrix.rotation_transition_duration_ms < 50 || config.matrix.rotation_transition_duration_ms > 3000) {
        config.matrix.rotation_transition_duration_ms = constrain(config.matrix.rotation_transition_duration_ms, 100, 2000);
        result.values_clamped++;
        result.modified = true;
    }

    // 2. Sanitize System Config
    if (config.system.timezone.length() == 0) {
        config.system.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
        result.defaults_injected++;
        result.modified = true;
    }
    if (config.system.lang.length() == 0) {
        config.system.lang = "en";
        result.defaults_injected++;
        result.modified = true;
    }
    if (config.system.unit != "C" && config.system.unit != "F") {
        config.system.unit = "C";
        result.values_fallback++;
        result.modified = true;
    }
    if (config.system.temp_offset < -20.0f || config.system.temp_offset > 20.0f) {
        config.system.temp_offset = constrain(config.system.temp_offset, -20.0f, 20.0f);
        result.values_clamped++;
        result.modified = true;
    }
    if (config.system.night_brightness < 1 || config.system.night_brightness > 100) {
        config.system.night_brightness = constrain(config.system.night_brightness, 1, 100);
        result.values_clamped++;
        result.modified = true;
    }
    if (config.system.idle_fighter_interval < 5 || config.system.idle_fighter_interval > 3600) {
        config.system.idle_fighter_interval = constrain(config.system.idle_fighter_interval, 10, 600);
        result.values_clamped++;
        result.modified = true;
    }

    // 3. Sanitize Engine Instances & Schemas
    for (auto& inst : config.instances) {
        // Migrate old visualizer to audiovisualizer
        if (inst.engine_id == "visualizer") {
            inst.engine_id = "audiovisualizer";
            result.modified = true;
            LOGI("ConfigSanitizer", "Migrated visualizer to audiovisualizer");
        }
        if (inst.engine_id == "audiovisualizer") {
            if (inst.config.hasKey("enabled") && !inst.config.hasKey("priority_mode")) {
                inst.config.setString("priority_mode", inst.config.getString("enabled"));
                result.modified = true;
            }
        }
        const EngineDescriptor* desc = EngineRegistry::getDescriptor(inst.engine_id.c_str());
        if (!desc) {
            continue; // Keep custom or dynamically registered engines intact
        }

        const ConfigSchema& schema = desc->schema;

        for (const auto& field : schema.fields) {
            String key = field.id;
            
            if (!inst.config.hasKey(key.c_str())) {
                // Missing: inject default
                inst.config.setString(key.c_str(), field.default_value);
                result.defaults_injected++;
                result.modified = true;
                continue;
            }

            String val_str = inst.config.getString(key.c_str());
            bool invalid = false;
            
            if (field.type == ConfigType::INTEGER) {
                long val = val_str.toInt();
                long new_val = val;

                if (strlen(field.min_val) > 0) {
                    long min_val = String(field.min_val).toInt();
                    if (new_val < min_val) { new_val = min_val; invalid = true; }
                }
                if (strlen(field.max_val) > 0) {
                    long max_val = String(field.max_val).toInt();
                    if (new_val > max_val) { new_val = max_val; invalid = true; }
                }

                if (invalid) {
                    if (field.validation_policy == ValidationPolicy::Clamp) {
                        inst.config.setInt(key.c_str(), new_val);
                        result.values_clamped++;
                        result.modified = true;
                    } else if (field.validation_policy == ValidationPolicy::FallbackDefault) {
                        inst.config.setString(key.c_str(), field.default_value);
                        result.values_fallback++;
                        result.modified = true;
                    }
                }
            } else if (field.type == ConfigType::FLOAT) {
                float val = val_str.toFloat();
                float new_val = val;

                if (strlen(field.min_val) > 0) {
                    float min_val = String(field.min_val).toFloat();
                    if (new_val < min_val) { new_val = min_val; invalid = true; }
                }
                if (strlen(field.max_val) > 0) {
                    float max_val = String(field.max_val).toFloat();
                    if (new_val > max_val) { new_val = max_val; invalid = true; }
                }

                if (invalid) {
                    if (field.validation_policy == ValidationPolicy::Clamp) {
                        inst.config.setString(key.c_str(), String(new_val));
                        result.values_clamped++;
                        result.modified = true;
                    } else if (field.validation_policy == ValidationPolicy::FallbackDefault) {
                        inst.config.setString(key.c_str(), field.default_value);
                        result.values_fallback++;
                        result.modified = true;
                    }
                }
            } else if (field.type == ConfigType::BOOLEAN) {
                String lower = val_str;
                lower.toLowerCase();
                if (lower != "true" && lower != "false" && lower != "1" && lower != "0") {
                    inst.config.setString(key.c_str(), field.default_value);
                    result.values_fallback++;
                    result.modified = true;
                }
            } else if (field.type == ConfigType::ENUM) {
                if (val_str.isEmpty() && strlen(field.default_value) > 0) {
                    inst.config.setString(key.c_str(), field.default_value);
                    result.values_fallback++;
                    result.modified = true;
                }
            }
        }
    }

    if (result.modified) {
        LOGI("ConfigSanitizer", "[CONFIG] In-memory sanitization: %d defaults injected, %d clamped, %d fallbacks.",
             result.defaults_injected, result.values_clamped, result.values_fallback);
    }

    return result;
}
