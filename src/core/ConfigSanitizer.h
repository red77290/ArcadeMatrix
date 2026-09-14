#pragma once

#include "ConfigLoader.h"
#include "core/EngineRegistry.h"
#include <vector>
#include <stdint.h>

struct SanitizeResult {
    bool modified = false;
    uint16_t defaults_injected = 0;
    uint16_t values_clamped = 0;
    uint16_t values_fallback = 0;
    uint16_t invalid_instances = 0;
};

class ConfigSanitizer {
public:
    /**
     * @brief Normalises an in-memory configuration.
     *
     * @param allowRotationBootstrap When true, an empty rotation is seeded with every
     *        rotation-capable instance. This is a first-boot convenience ONLY and must
     *        never be enabled on a user-initiated mutation: an empty rotation is a
     *        legitimate, explicit user state, and silently repopulating it turned a
     *        single "create screen" action into a dozen unrequested rotation entries.
     */
    static SanitizeResult sanitize(ConfigLoader& config, bool allowRotationBootstrap = false);

    /**
     * @brief Normalisation entry point for runtime mutations (REST API, MQTT, presets).
     *        Never bootstraps the rotation.
     */
    static SanitizeResult sanitizeInstances(ConfigLoader& config) { return sanitize(config, false); }

private:
    static void sanitizeSystem(SystemConfig& sys, SanitizeResult& result);
    static void sanitizeMatrix(MatrixConfig& mat, SanitizeResult& result);
    static void sanitizeMqtt(MqttConfig& mqtt, SanitizeResult& result);
    static void sanitizeInstances(std::vector<EngineInstance>& instances, SanitizeResult& result);
    static void sanitizeInstance(EngineInstance& inst, SanitizeResult& result);
    static void sanitizeField(DictionaryEngineConfig& conf, const ConfigField& field, SanitizeResult& result);
    static void sanitizeRotation(ConfigLoader& config, bool allowBootstrap, SanitizeResult& result);
};
