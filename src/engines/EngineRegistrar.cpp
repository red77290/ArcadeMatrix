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

RequirementCheckResult EngineRegistrar::checkRequirements(const EngineRequirements& req, const char* activePipeline) {
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
    if (req.needsNetwork && !caps.hasNetwork) {
        return {false, "Requires network/WiFi connection"};
    }
    if (req.needsSd && !caps.hasSd) {
        return {false, "Requires SD card"};
    }

    if (req.minFreeDmaBytes > 0) {
        uint32_t freeDma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        if (freeDma < req.minFreeDmaBytes) {
            return {false, "Insufficient DMA memory available"};
        }
    }

    if (req.needsTls && !caps.hasPsram) {
        uint32_t freeDram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        uint32_t requiredHeap = (req.minFreeInternalHeapBytes > 0) ? req.minFreeInternalHeapBytes : 50000;
        uint32_t requiredBlock = (req.minLargestInternalBlockBytes > 0) ? req.minLargestInternalBlockBytes : 28000;

        if (freeDram < requiredHeap || largestBlock < requiredBlock) {
            if (activePipeline && strcmp(activePipeline, "canvas_single") == 0) {
                return {false, "Insufficient contiguous internal DRAM block (≥ 28KB) for TLS handshake"};
            }
            return {false, "Requires additional internal heap headroom (≥ 50KB total, ≥ 28KB block). Switch to Canvas Single pipeline to unlock TLS."};
        }
    } else {
        if (req.minFreeInternalHeapBytes > 0) {
            uint32_t freeDram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (freeDram < req.minFreeInternalHeapBytes) {
                return {false, "Insufficient internal DRAM"};
            }
        }
        if (req.minLargestInternalBlockBytes > 0) {
            uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (largestBlock < req.minLargestInternalBlockBytes) {
                return {false, "Insufficient contiguous internal DRAM block"};
            }
        }
    }

    return {true, ""};
}

bool EngineRegistrar::meetsRequirements(const EngineRequirements& req, const char* activePipeline) {
    return checkRequirements(req, activePipeline).satisfied;
}

bool EngineRegistrar::registerHandler(const IEngineDescriptorHandler& handler) {
    EngineDescriptor desc = handler.getDescriptor();
    auto res = checkRequirements(desc.requirements);
    desc.available = res.satisfied;
    desc.unavailableReason = res.reason.c_str();
    if (!res.satisfied) {
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
        &marqueeHandler
    };

    for (const auto* handler : handlers) {
        if (handler) {
            registerHandler(*handler);
        }
    }
    
    LOGI("Registrar", "All engines successfully registered (%u available).", EngineRegistry::count());
}
