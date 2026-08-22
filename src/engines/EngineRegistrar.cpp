#include "EngineRegistrar.h"
#include "core/EngineRegistry.h"
#include "hal/HardwareHAL.h"
#include "core/Logger.h"

#include "ClockEngine.h"
#include "DateEngine.h"
#include "WeatherEngine.h"
#include "FighterEngine.h"
#include "GifEngine.h"
#include "CryptoEngine.h"
#include "StockEngine.h"
#include "TempEngine.h"
#include "VisualizerEngine.h"
#include "../api/CoinGeckoProvider.h"
#include "../api/BinanceProvider.h"
#include "../api/YahooFinanceProvider.h"
#include "DecibelEngine.h"
#include "MessageEngine.h"
#include "MarqueeEngine.h"

bool EngineRegistrar::meetsRequirements(const EngineRequirements& req) {
    const auto& caps = hardwareHAL.capabilities();
    if (req.needsAudio && !caps.hasMicrophone) return false;
    if (req.needsTempSensor && !caps.hasTempSensor) return false;
    if (req.needsGyroscope && !caps.hasGyroscope) return false;
    if (req.needsPsram && !caps.hasPsram) return false;
    return true;
}

static void tryRegister(const EngineDescriptor& desc) {
    const auto& caps = hardwareHAL.capabilities();
    if (desc.requirements.needsPsram && !caps.hasPsram) {
        LOGW("Registrar", "Skipping engine %s: requires PSRAM", desc.metadata.id);
        return;
    }
    if (desc.requirements.needsAudio && !caps.hasMicrophone) {
        LOGW("Registrar", "Skipping engine %s: requires microphone", desc.metadata.id);
        return;
    }
    if (desc.requirements.needsTempSensor && !caps.hasTempSensor) {
        LOGW("Registrar", "Skipping engine %s: requires temperature sensor", desc.metadata.id);
        return;
    }
    if (desc.requirements.needsGyroscope && !caps.hasGyroscope) {
        LOGW("Registrar", "Skipping engine %s: requires gyroscope", desc.metadata.id);
        return;
    }

    EngineRegistry::registerEngine(desc);
}

void EngineRegistrar::registerAll() {
    LOGI("Registrar", "Registering dynamic engines...");

    EngineDescriptor clockDesc;
    clockDesc.metadata = {"clock", "Clock", "info", "1.0.0"};
    clockDesc.capabilities.realtime = true;
    clockDesc.capabilities.allowsOverlay = true;
    clockDesc.requirements.needsAudio = false;
    clockDesc.factory = []() { return std::unique_ptr<IEngine>(new ClockEngine()); };
    tryRegister(clockDesc);

    EngineDescriptor desc_date;
    desc_date.metadata = {"date", "Date", "info", "1.0.0"};
    desc_date.capabilities.realtime = false;
    desc_date.capabilities.allowsOverlay = true;
    desc_date.requirements.needsAudio = false;
    desc_date.requirements.needsNetwork = false;
    desc_date.factory = []() { return std::unique_ptr<IEngine>(new DateEngine()); };
    tryRegister(desc_date);

    EngineDescriptor desc_weather;
    desc_weather.metadata = {"weather", "Weather", "info", "1.0.0"};
    desc_weather.capabilities.realtime = false;
    desc_weather.capabilities.allowsOverlay = true;
    desc_weather.requirements.needsAudio = false;
    desc_weather.requirements.needsNetwork = true;
    desc_weather.factory = []() { return std::unique_ptr<IEngine>(new WeatherEngine()); };
    tryRegister(desc_weather);

    EngineDescriptor desc_fighter;
    desc_fighter.metadata = {"fighter", "Fighter", "overlay", "1.0.0"};
    desc_fighter.capabilities.realtime = true;
    desc_fighter.capabilities.allowsOverlay = false;
    desc_fighter.requirements.needsAudio = false;
    desc_fighter.requirements.needsNetwork = false;
    desc_fighter.factory = []() { return std::unique_ptr<IEngine>(new FighterEngine()); };
    tryRegister(desc_fighter);

    EngineDescriptor desc_gifs;
    desc_gifs.metadata = {"gifs", "GIF Player", "media", "1.0.0"};
    desc_gifs.capabilities.realtime = true;
    desc_gifs.capabilities.allowsOverlay = false;
    desc_gifs.capabilities.selfPaced = true;
    desc_gifs.requirements.needsAudio = false;
    desc_gifs.requirements.needsNetwork = false;
    desc_gifs.factory = []() { return std::unique_ptr<IEngine>(new GifEngine()); };
    tryRegister(desc_gifs);
    
    EngineDescriptor desc_crypto;
    desc_crypto.metadata = {"crypto", "Crypto Ticker", "finance", "1.0.0"};
    desc_crypto.capabilities.realtime = false;
    desc_crypto.capabilities.allowsOverlay = true;
    desc_crypto.requirements.needsAudio = false;
    desc_crypto.requirements.needsNetwork = true;
    desc_crypto.requirements.needsPsram = true;
    desc_crypto.factory = []() { 
        auto eng = new CryptoEngine();
        eng->addProvider(new CoinGeckoProvider());
        eng->addProvider(new BinanceProvider());
        return std::unique_ptr<IEngine>(eng); 
    };
    tryRegister(desc_crypto);

    EngineDescriptor desc_stock;
    desc_stock.metadata = {"stock", "Stock Ticker", "finance", "1.0.0"};
    desc_stock.capabilities.realtime = false;
    desc_stock.capabilities.allowsOverlay = true;
    desc_stock.requirements.needsAudio = false;
    desc_stock.requirements.needsNetwork = true;
    desc_stock.requirements.needsPsram = true;
    desc_stock.factory = []() {
        auto eng = new StockEngine();
        eng->addProvider(new YahooFinanceProvider());
        return std::unique_ptr<IEngine>(eng);
    };
    tryRegister(desc_stock);

    EngineDescriptor desc_temp;
    desc_temp.metadata = {"temp", "Temperature", "info", "1.0.0"};
    desc_temp.capabilities.realtime = false;
    desc_temp.capabilities.allowsOverlay = true;
    desc_temp.requirements.needsAudio = false;
    desc_temp.requirements.needsNetwork = false;
    desc_temp.requirements.needsTempSensor = true;
    desc_temp.factory = []() { return std::unique_ptr<IEngine>(new TempEngine()); };
    tryRegister(desc_temp);

    EngineDescriptor desc_visualizer;
    desc_visualizer.metadata = {"visualizer", "Audio Visualizer", "visualizer", "1.0.0"};
    desc_visualizer.capabilities.realtime = true;
    desc_visualizer.capabilities.allowsOverlay = false;
    desc_visualizer.requirements.needsAudio = true;
    desc_visualizer.requirements.needsNetwork = false;
    desc_visualizer.factory = []() { return std::unique_ptr<IEngine>(new VisualizerEngine()); };
    tryRegister(desc_visualizer);

    EngineDescriptor desc_decibel;
    desc_decibel.metadata = {"decibel", "Decibel Meter", "info", "1.0.0"};
    desc_decibel.capabilities.realtime = true;
    desc_decibel.capabilities.allowsOverlay = false;
    desc_decibel.requirements.needsAudio = true;
    desc_decibel.requirements.needsNetwork = false;
    desc_decibel.factory = []() { return std::unique_ptr<IEngine>(new DecibelEngine()); };
    tryRegister(desc_decibel);

    EngineDescriptor desc_message;
    desc_message.metadata = {"message", "Custom Message", "info", "1.0.0"};
    desc_message.capabilities.realtime = false;
    desc_message.capabilities.allowsOverlay = false;
    desc_message.requirements.needsAudio = false;
    desc_message.requirements.needsNetwork = false;
    desc_message.factory = []() { return std::unique_ptr<IEngine>(new MessageEngine()); };
    tryRegister(desc_message);

    EngineDescriptor desc_marquee;
    desc_marquee.metadata = {"marquee", "Marquee Images", "info", "1.0.0"};
    desc_marquee.capabilities.realtime = true;
    desc_marquee.capabilities.allowsOverlay = false;
    desc_marquee.requirements.needsAudio = false;
    desc_marquee.requirements.needsNetwork = false;
    desc_marquee.factory = []() { return std::unique_ptr<IEngine>(new MarqueeEngine()); };
    tryRegister(desc_marquee);
}
