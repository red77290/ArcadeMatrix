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
    static SanitizeResult sanitizeInstances(ConfigLoader& config);
    static SanitizeResult sanitizeInstances(ConfigLoader& config, const EngineRegistry& registry);
};
