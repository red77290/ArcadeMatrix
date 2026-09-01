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
    static SanitizeResult sanitize(ConfigLoader& config);
    static SanitizeResult sanitizeInstances(ConfigLoader& config) { return sanitize(config); }

private:
    static void sanitizeSystem(SystemConfig& sys, SanitizeResult& result);
    static void sanitizeMatrix(MatrixConfig& mat, SanitizeResult& result);
    static void sanitizeInstances(std::vector<EngineInstance>& instances, SanitizeResult& result);
    static void sanitizeInstance(EngineInstance& inst, SanitizeResult& result);
    static void sanitizeField(DictionaryEngineConfig& conf, const ConfigField& field, SanitizeResult& result);
    static void sanitizeRotation(ConfigLoader& config, SanitizeResult& result);
};
