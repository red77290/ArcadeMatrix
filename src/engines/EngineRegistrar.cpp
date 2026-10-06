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
#include "DashboardEngine.h"
#include "GNewsEngine.h"
#include "MarqueeEngine.h"
#include "MqttDataEngine.h"

CompatibilityVerdict EngineRegistrar::evaluateCompatibility(const EngineDescriptor& desc, const char* targetPipeline, EvaluationMode mode) {
    auto ctx = CompatibilityEvaluator::buildCurrentContext(mode);
    if (targetPipeline && strlen(targetPipeline) > 0) {
        ctx.requestedPipeline = targetPipeline;
    }
    return CompatibilityEvaluator::evaluate(desc, ctx);
}

RequirementCheckResult EngineRegistrar::checkRequirements(const EngineRequirements& req, const char* targetPipeline, EvaluationMode mode) {
    EngineDescriptor dummyDesc;
    dummyDesc.requirements = req;
    auto verdict = evaluateCompatibility(dummyDesc, targetPipeline, mode);
    return { verdict.compatible(), String(verdict.reasonText) };
}

bool EngineRegistrar::meetsRequirements(const EngineRequirements& req, const char* targetPipeline, EvaluationMode mode) {
    return checkRequirements(req, targetPipeline, mode).satisfied;
}

bool EngineRegistrar::registerHandler(const IEngineDescriptorHandler& handler) {
    EngineDescriptor desc = handler.getDescriptor();
    auto verdict = evaluateCompatibility(desc);
    desc.available = verdict.compatible();
    desc.unavailableReason = verdict.reasonText;
    if (!verdict.compatible()) {
        LOGW("Registrar", "Engine %s registered as unavailable: %s", desc.metadata.id ? desc.metadata.id : "", desc.unavailableReason);
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
    static const DashboardEngineDescriptorHandler dashboardHandler;
    static const GNewsEngineDescriptorHandler gnewsHandler;
    static const MarqueeEngineDescriptorHandler marqueeHandler;
    static const MqttDataEngineDescriptorHandler mqttDataHandler;

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
        &musicHandler,
        &dashboardHandler,
        &gnewsHandler,
        &marqueeHandler,
        &mqttDataHandler
    };

    for (const auto* handler : handlers) {
        if (handler) {
            registerHandler(*handler);
        }
    }
    
    LOGI("Registrar", "All engines successfully registered (%u available).", EngineRegistry::count());
}
