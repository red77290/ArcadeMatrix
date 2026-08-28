#include "EngineRegistrar.h"
#include "core/EngineRegistry.h"
#include "hal/HardwareHAL.h"
#include "core/Logger.h"

#include "ClockEngine.h"
#include "DateEngine.h"
#include "WeatherEngine.h"
#include "GifEngine.h"
#include "CryptoEngine.h"
#include "StockEngine.h"
#include "VisualizerEngine.h"
#include "DecibelEngine.h"
#include "TempEngine.h"
#include "MessageEngine.h"
#include "GoogleCastEngine.h"
#include "SpotifyEngine.h"
#include "SysInfoEngine.h"
#include "MusicEngine.h"

RequirementCheckResult EngineRegistrar::checkRequirements(const EngineRequirements& req) {
    const auto& caps = hardwareHAL.capabilities();
    if (req.needsPsram && !caps.hasPsram) {
        return {false, "Requires PSRAM"};
    }
    if (req.needsAudio && !caps.hasMicrophone) {
        return {false, "Requires microphone"};
    }
    if (req.needsTempSensor && !caps.hasTempSensor) {
        return {false, "Requires temperature sensor"};
    }
    if (req.needsGyroscope && !caps.hasGyroscope) {
        return {false, "Requires gyroscope"};
    }
    return {true, ""};
}

bool EngineRegistrar::meetsRequirements(const EngineRequirements& req) {
    return checkRequirements(req).satisfied;
}

bool EngineRegistrar::registerHandler(const IEngineDescriptorHandler& handler) {
    EngineDescriptor desc = handler.getDescriptor();
    auto res = checkRequirements(desc.requirements);
    if (!res.satisfied) {
        LOGW("Registrar", "Skipping engine %s: %s", desc.metadata.id ? desc.metadata.id : "", res.reason.c_str());
        return false;
    }
    return EngineRegistry::registerEngine(desc);
}

void EngineRegistrar::registerAll() {
    LOGI("Registrar", "Registering dynamic engines from descriptor handlers...");

    // Static instances of all descriptor handlers
    static const ClockEngineDescriptorHandler clockHandler;
    static const DateEngineDescriptorHandler dateHandler;
    static const WeatherEngineDescriptorHandler weatherHandler;
    static const GifEngineDescriptorHandler gifHandler;
    static const CryptoEngineDescriptorHandler cryptoHandler;
    static const StockEngineDescriptorHandler stockHandler;
    static const VisualizerEngineDescriptorHandler visualizerHandler;
    static const DecibelEngineDescriptorHandler decibelHandler;
    static const TempEngineDescriptorHandler tempHandler;
    static const MessageEngineDescriptorHandler messageHandler;
    static const GoogleCastDescriptorHandler googleCastHandler;
    static const SpotifyDescriptorHandler spotifyHandler;
    static const SysInfoEngineDescriptorHandler sysInfoHandler;
    static const MusicEngineDescriptorHandler musicHandler;

    const IEngineDescriptorHandler* handlers[] = {
        &clockHandler,
        &dateHandler,
        &weatherHandler,
        &gifHandler,
        &cryptoHandler,
        &stockHandler,
        &visualizerHandler,
        &decibelHandler,
        &tempHandler,
        &messageHandler,
        &googleCastHandler,
        &spotifyHandler,
        &sysInfoHandler,
        &musicHandler
    };

    for (const auto* handler : handlers) {
        if (handler) {
            registerHandler(*handler);
        }
    }
    
    LOGI("Registrar", "All engines successfully registered (%u available).", EngineRegistry::count());
}
