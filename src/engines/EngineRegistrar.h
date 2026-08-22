#pragma once
#include "core/EngineContract.h"

// Forward declarations for legacy engines
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

/**
 * @brief Registers all engines (including legacy wrappers) into the global EngineRegistry.
 * Call this exactly once during setup().
 */
class EngineRegistrar {
public:
    static void registerAll();
    static bool meetsRequirements(const EngineRequirements& req);
};
