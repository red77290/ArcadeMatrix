#include "ConfigSanitizer.h"
#include "Logger.h"
#include "../hal/BoardProfile.h"

SanitizeResult ConfigSanitizer::sanitize(ConfigLoader& config, bool allowRotationBootstrap) {
    SanitizeResult result;
    
    sanitizeMatrix(config.matrix, result);
    sanitizeSystem(config.system, result);
    sanitizeMqtt(config.mqtt, result);

    // Ensure every rotation entry has a corresponding instance in config.instances
    for (const auto& rot : config.rotation) {
        bool found = false;
        for (const auto& inst : config.instances) {
            if (inst.instance_id == rot.instance_id) {
                found = true;
                break;
            }
        }
        if (!found && !rot.instance_id.isEmpty()) {
            EngineInstance newInst;
            newInst.instance_id = rot.instance_id;
            int under = rot.instance_id.indexOf('_');
            newInst.engine_id = (under > 0) ? rot.instance_id.substring(0, under) : rot.instance_id;
            config.instances.push_back(newInst);
            result.defaults_injected++;
            result.modified = true;
            LOGI("ConfigSanitizer", "Recreated missing instance '%s' (engine '%s') referenced by rotation",
                 newInst.instance_id.c_str(), newInst.engine_id.c_str());
        }
    }

    sanitizeInstances(config.instances, result);
    sanitizeRotation(config, allowRotationBootstrap, result);

    if (result.modified) {
        LOGI("ConfigSanitizer", "[CONFIG] In-memory sanitization: %d defaults injected, %d clamped, %d fallbacks, %d invalid instances.",
             result.defaults_injected, result.values_clamped, result.values_fallback, result.invalid_instances);
    }

    return result;
}

void ConfigSanitizer::sanitizeMatrix(MatrixConfig& matrix, SanitizeResult& result) {
    if (matrix.width < 16) {
        matrix.width = 64;
        result.values_clamped++;
        result.modified = true;
    }
    if (matrix.height < 16) {
        matrix.height = 32;
        result.values_clamped++;
        result.modified = true;
    }
    if (matrix.chainLength < 1) {
        matrix.chainLength = 1;
        result.values_clamped++;
        result.modified = true;
    }
    if (matrix.powerLimitPercent < 1 || matrix.powerLimitPercent > 100) {
        matrix.powerLimitPercent = constrain(matrix.powerLimitPercent, 1, 100);
        result.values_clamped++;
        result.modified = true;
    }
    uint8_t maxColorDepth = (BoardProfile::current().memory().tier == MemoryTier::CONSTRAINED) ? 6 : 8;
    if (matrix.colorDepth < 1 || matrix.colorDepth > maxColorDepth) {
        matrix.colorDepth = constrain(matrix.colorDepth, (uint8_t)1, maxColorDepth);
        result.values_clamped++;
        result.modified = true;
    }
    if (matrix.limitRefreshRateHz < 30 || matrix.limitRefreshRateHz > 240) {
        matrix.limitRefreshRateHz = constrain(matrix.limitRefreshRateHz, 30, 240);
        result.values_clamped++;
        result.modified = true;
    }
    if (matrix.rotation_offset < 0 || matrix.rotation_offset > 3) {
        matrix.rotation_offset = (matrix.rotation_offset % 4 + 4) % 4;
        result.values_clamped++;
        result.modified = true;
    }
    if (matrix.latchBlanking < 1 || matrix.latchBlanking > 32) {
        matrix.latchBlanking = 4;
        result.values_clamped++;
        result.modified = true;
    }
    if (matrix.rotation_transition_duration_ms < 50 || matrix.rotation_transition_duration_ms > 3000) {
        matrix.rotation_transition_duration_ms = constrain(matrix.rotation_transition_duration_ms, 100, 2000);
        result.values_clamped++;
        result.modified = true;
    }
}

void ConfigSanitizer::sanitizeSystem(SystemConfig& system, SanitizeResult& result) {
    if (system.timezone.length() == 0) {
        system.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
        result.defaults_injected++;
        result.modified = true;
    }
    if (system.lang.length() == 0) {
        system.lang = "en";
        result.defaults_injected++;
        result.modified = true;
    }
    if (system.unit != "C" && system.unit != "F") {
        system.unit = "C";
        result.values_fallback++;
        result.modified = true;
    }
    if (system.temp_offset < -30.0f || system.temp_offset > 30.0f) {
        system.temp_offset = constrain(system.temp_offset, -30.0f, 30.0f);
        result.values_clamped++;
        result.modified = true;
    }
    if (system.night_brightness < 0 || system.night_brightness > 100) {
        system.night_brightness = constrain(system.night_brightness, 0, 100);
        result.values_clamped++;
        result.modified = true;
    }
    if (system.idle_fighter_interval < 5 || system.idle_fighter_interval > 3600) {
        system.idle_fighter_interval = constrain(system.idle_fighter_interval, 10, 600);
        result.values_clamped++;
        result.modified = true;
    }
    if (system.idle_fighter_speed < 25 || system.idle_fighter_speed > 200) {
        system.idle_fighter_speed = constrain(system.idle_fighter_speed, 25, 200);
        result.values_clamped++;
        result.modified = true;
    }
}

void ConfigSanitizer::sanitizeInstances(std::vector<EngineInstance>& instances, SanitizeResult& result) {
    for (auto it = instances.begin(); it != instances.end(); ) {
        // Reject and prune non-existent or internal-only engines (e.g. fighter)
        if (it->engine_id == "fighter") {
            it = instances.erase(it);
            result.invalid_instances++;
            result.modified = true;
            LOGW("ConfigSanitizer", "Pruned invalid internal instance pointing to 'fighter'");
            continue;
        }

        // Migrate old visualizer to audiovisualizer
        if (it->engine_id == "visualizer") {
            it->engine_id = "audiovisualizer";
            result.modified = true;
            LOGI("ConfigSanitizer", "Migrated visualizer to audiovisualizer");
        }
        if (it->engine_id == "audiovisualizer") {
            if (it->config.hasKey("enabled") && !it->config.hasKey("priority_mode")) {
                it->config.setString("priority_mode", it->config.getString("enabled"));
                result.modified = true;
            }
        }

        sanitizeInstance(*it, result);
        ++it;
    }
}

void ConfigSanitizer::sanitizeInstance(EngineInstance& inst, SanitizeResult& result) {
    const EngineDescriptor* desc = EngineRegistry::getDescriptor(inst.engine_id.c_str());
    if (!desc) {
        return; // Keep custom or dynamically registered engines intact
    }

    for (const auto& field : desc->schema.fields) {
        sanitizeField(inst.config, field, result);
    }
}

void ConfigSanitizer::sanitizeField(DictionaryEngineConfig& conf, const ConfigField& field, SanitizeResult& result) {
    const char* key = field.id;
    if (!key || key[0] == '\0') return;

    // 1. Missing field check
    if (!conf.hasKey(key)) {
        if (field.required || strlen(field.default_value) > 0) {
            conf.setString(key, field.default_value);
            result.defaults_injected++;
            result.modified = true;
        }
        return;
    }

    String val_str = conf.getString(key);
    bool invalid = false;

    // 2. Type & Bounds check (ordered priority: type -> min/max -> enum -> validation_policy)
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
                conf.setInt(key, new_val);
                result.values_clamped++;
                result.modified = true;
            } else if (field.validation_policy == ValidationPolicy::FallbackDefault) {
                conf.setString(key, field.default_value);
                result.values_fallback++;
                result.modified = true;
            } else if (field.validation_policy == ValidationPolicy::Reject) {
                conf.setString(key, field.default_value);
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
                conf.setString(key, String(new_val));
                result.values_clamped++;
                result.modified = true;
            } else if (field.validation_policy == ValidationPolicy::FallbackDefault ||
                       field.validation_policy == ValidationPolicy::Reject) {
                conf.setString(key, field.default_value);
                result.values_fallback++;
                result.modified = true;
            }
        }
    } else if (field.type == ConfigType::BOOLEAN) {
        String lower = val_str;
        lower.toLowerCase();
        if (lower != "true" && lower != "false" && lower != "1" && lower != "0") {
            conf.setString(key, field.default_value);
            result.values_fallback++;
            result.modified = true;
        }
    } else if (field.type == ConfigType::ENUM) {
        if (val_str.isEmpty() && strlen(field.default_value) > 0) {
            conf.setString(key, field.default_value);
            result.values_fallback++;
            result.modified = true;
        } else {
            // 1. Auto-heal dirty values that retained the dropdown label (e.g. "F:Fahrenheit (°F)" -> "F")
            if (val_str.indexOf(':') != -1 && !val_str.startsWith("%")) {
                int colonIdx = val_str.indexOf(':');
                String cleanVal = val_str.substring(0, colonIdx);
                cleanVal.trim();
                if (!cleanVal.isEmpty()) {
                    val_str = cleanVal;
                    conf.setString(key, val_str);
                    result.modified = true;
                }
            }

            // 2. Validate against field.options if provided
            if (strlen(field.options) > 0) {
                bool found = false;
                String optsStr(field.options);
                int start = 0;
                while (start < optsStr.length()) {
                    int comma = optsStr.indexOf(',', start);
                    String item = (comma == -1) ? optsStr.substring(start) : optsStr.substring(start, comma);
                    item.trim();
                    if (!item.isEmpty()) {
                        String optVal = item;
                        if (!item.startsWith("%") && item.indexOf(':') != -1) {
                            optVal = item.substring(0, item.indexOf(':'));
                            optVal.trim();
                        }
                        if (val_str.equalsIgnoreCase(optVal)) {
                            found = true;
                            break;
                        }
                    }
                    if (comma == -1) break;
                    start = comma + 1;
                }
                if (!found && (field.validation_policy == ValidationPolicy::FallbackDefault ||
                               field.validation_policy == ValidationPolicy::Reject)) {
                    if (strlen(field.default_value) > 0) {
                        conf.setString(key, field.default_value);
                        result.values_fallback++;
                        result.modified = true;
                    }
                }
            }
        }
    } else if (field.type == ConfigType::LIST) {
        if (val_str.isEmpty() && strlen(field.default_value) > 0) {
            conf.setString(key, field.default_value);
            result.defaults_injected++;
            result.modified = true;
        }
    }
}

void ConfigSanitizer::sanitizeMqtt(MqttConfig& mqtt, SanitizeResult& result) {
    if (mqtt.port == 0) {
        mqtt.port = 1883;
        result.defaults_injected++;
        result.modified = true;
    }
}

void ConfigSanitizer::sanitizeRotation(ConfigLoader& config, bool allowBootstrap, SanitizeResult& result) {
    // 1. Prune rotation entries pointing to engines unavailable on this hardware profile
    for (auto it = config.rotation.begin(); it != config.rotation.end(); ) {
        const EngineInstance* inst = nullptr;
        for (const auto& i : config.instances) {
            if (i.instance_id == it->instance_id) {
                inst = &i;
                break;
            }
        }
        if (inst) {
            const auto* desc = EngineRegistry::getDescriptor(inst->engine_id.c_str());
            if (desc && !desc->available) {
                LOGW("ConfigSanitizer", "Removing rotation entry '%s' (engine '%s' unavailable on this hardware)",
                     it->instance_id.c_str(), inst->engine_id.c_str());
                it = config.rotation.erase(it);
                result.modified = true;
                continue;
            }
        }
        ++it;
    }

    // Seeding the rotation from the instance list is a first-boot convenience, never a
    // repair. Running it on every mutation meant that creating a single screen while the
    // rotation happened to be empty silently enrolled every other configured screen too.
    if (!allowBootstrap) return;

    if (config.rotation.empty() && !config.instances.empty()) {
        for (const auto& inst : config.instances) {
            const auto* desc = EngineRegistry::getDescriptor(inst.engine_id.c_str());
            if (desc && desc->available && desc->capabilities.allowRotation) {
                config.rotation.emplace_back(inst.instance_id, 15, OverlayConfig{true});
                result.defaults_injected++;
                result.modified = true;
            }
        }
    }
}
