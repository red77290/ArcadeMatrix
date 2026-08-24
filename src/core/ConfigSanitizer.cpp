#include "ConfigSanitizer.h"
#include "Logger.h"

SanitizeResult ConfigSanitizer::sanitizeInstances(ConfigLoader& config) {
    SanitizeResult result;
    
    // ConfigSanitizer validates schemas and fields of existing instances.
    // It NEVER creates or injects instances: instance lifecycle is managed solely by the user via the UI/API.

    for (auto it = config.instances.begin(); it != config.instances.end(); ) {
        // Migrate old visualizer to audiovisualizer
        if (it->engine_id == "visualizer") {
            it->engine_id = "audiovisualizer";
            result.modified = true;
            LOGI("ConfigSanitizer", "Migrated visualizer to audiovisualizer");
        }
        const EngineDescriptor* desc = EngineRegistry::getDescriptor(it->engine_id.c_str());
        if (!desc) {
            LOGW("ConfigSanitizer", "Unknown engine_id '%s' for instance '%s' - removing", it->engine_id.c_str(), it->instance_id.c_str());
            result.invalid_instances++;
            it = config.instances.erase(it);
            result.modified = true;
            continue;
        }

        auto& inst = *it;
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
                long val = val_str.toInt(); // naive parse
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
                if (strlen(field.options) > 0) {
                    String opts = field.options;
                    bool found = false;
                    int start = 0;
                    while (start < opts.length()) {
                        int comma = opts.indexOf(',', start);
                        String opt = (comma == -1) ? opts.substring(start) : opts.substring(start, comma);
                        opt.trim();
                        if (opt.equalsIgnoreCase(val_str)) {
                            found = true;
                            break;
                        }
                        if (comma == -1) break;
                        start = comma + 1;
                    }
                    if (!found) {
                        inst.config.setString(key.c_str(), field.default_value);
                        result.values_fallback++;
                        result.modified = true;
                    }
                }
            }
        }
        ++it;
    }

    // Now remove any references in rotation list to instances that no longer exist or do not allow rotation
    for (auto it = config.rotation.begin(); it != config.rotation.end(); ) {
        bool valid = false;
        for (const auto& inst : config.instances) {
            if (inst.instance_id == it->instance_id) {
                const EngineDescriptor* desc = EngineRegistry::getDescriptor(inst.engine_id.c_str());
                if (desc && desc->capabilities.allowRotation) {
                    valid = true;
                }
                break;
            }
        }
        if (!valid) {
            it = config.rotation.erase(it);
            result.modified = true;
        } else {
            ++it;
        }
    }

    if (result.modified) {
        LOGI("ConfigSanitizer", "[CONFIG] Sanitization completed: %d defaults injected, %d clamped, %d fallbacks, %d invalid instances.",
             result.defaults_injected, result.values_clamped, result.values_fallback, result.invalid_instances);
    }

    return result;
}
