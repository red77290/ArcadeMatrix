#pragma once
#include "core/EngineContract.h"

// Forward declarations for engine classes
class ClockEngine;
class DateEngine;
class WeatherEngine;
class FighterEngine;
class GifEngine;
class CryptoEngine;
class StockEngine;
class TempEngine;
class VisualizerEngine;
class DecibelEngine;
class MessageEngine;
class MarqueeEngine;

struct RequirementCheckResult {
    bool satisfied;
    String reason;
};

/**
 * @brief Registers all engines into the global EngineRegistry with requirement gating and handler lookup.
 */
class EngineRegistrar {
public:
    static void registerAll();
    static bool registerHandler(const IEngineDescriptorHandler& handler);
    static RequirementCheckResult checkRequirements(const EngineRequirements& req);
    static bool meetsRequirements(const EngineRequirements& req);
};

