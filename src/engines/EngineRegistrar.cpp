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

void EngineRegistrar::registerAll() {
    LOGI("Registrar", "Registering dynamic engines...");

    EngineDescriptor clockDesc;
    clockDesc.metadata = {"clock", "Clock", "info", "1.0.0"};
    clockDesc.capabilities.needs_audio = false;
    clockDesc.factory = []() { return std::unique_ptr<IEngine>(new ClockEngine()); };
    EngineRegistry::registerEngine(clockDesc);

    // 2. Date Engine (Migrated)
    EngineDescriptor desc_date;
    desc_date.metadata = {"date", "Date", "info", "1.0.0"};
    desc_date.capabilities.needs_audio = false;
    desc_date.capabilities.needs_network = false;
    desc_date.factory = []() { return std::unique_ptr<IEngine>(new DateEngine()); };
    EngineRegistry::registerEngine(desc_date);
    EngineDescriptor desc_weather;
    desc_weather.metadata = {"weather", "Weather", "info", "1.0.0"};
    desc_weather.capabilities.needs_audio = false;
    desc_weather.capabilities.needs_network = true;
    desc_weather.factory = []() { return std::unique_ptr<IEngine>(new WeatherEngine()); };
    EngineRegistry::registerEngine(desc_weather);
    EngineDescriptor desc_fighter;
    desc_fighter.metadata = {"fighter", "Fighter", "overlay", "1.0.0"};
    desc_fighter.capabilities.needs_audio = false;
    desc_fighter.capabilities.needs_network = false;
    desc_fighter.factory = []() { return std::unique_ptr<IEngine>(new FighterEngine()); };
    EngineRegistry::registerEngine(desc_fighter);
    EngineDescriptor desc_gifs;
    desc_gifs.metadata = {"gifs", "GIF Player", "media", "1.0.0"};
    desc_gifs.capabilities.needs_audio = false;
    desc_gifs.capabilities.needs_network = false;
    desc_gifs.factory = []() { return std::unique_ptr<IEngine>(new GifEngine()); };
    EngineRegistry::registerEngine(desc_gifs);
    
    EngineDescriptor desc_crypto;
    desc_crypto.metadata = {"crypto", "Crypto Ticker", "finance", "1.0.0"};
    desc_crypto.capabilities.needs_audio = false;
    desc_crypto.capabilities.needs_network = true;
    desc_crypto.factory = []() { 
        auto eng = new CryptoEngine();
        eng->addProvider(new CoinGeckoProvider());
        eng->addProvider(new BinanceProvider());
        return std::unique_ptr<IEngine>(eng); 
    };
    EngineRegistry::registerEngine(desc_crypto);
    EngineDescriptor desc_stock;
    desc_stock.metadata = {"stock", "Stock Ticker", "finance", "1.0.0"};
    desc_stock.capabilities.needs_audio = false;
    desc_stock.capabilities.needs_network = true;
    desc_stock.factory = []() {
        auto eng = new StockEngine();
        eng->addProvider(new YahooFinanceProvider());
        return std::unique_ptr<IEngine>(eng);
    };
    EngineRegistry::registerEngine(desc_stock);
    EngineDescriptor desc_temp;
    desc_temp.metadata = {"temp", "Temperature", "info", "1.0.0"};
    desc_temp.capabilities.needs_audio = false;
    desc_temp.capabilities.needs_network = false;
    desc_temp.factory = []() { return std::unique_ptr<IEngine>(new TempEngine()); };
    EngineRegistry::registerEngine(desc_temp);
    if (hardwareHAL.isAudioAvailable()) {
    EngineDescriptor desc_visualizer;
    desc_visualizer.metadata = {"visualizer", "Audio Visualizer", "visualizer", "1.0.0"};
    desc_visualizer.capabilities.needs_audio = true;
    desc_visualizer.capabilities.needs_network = false;
    desc_visualizer.factory = []() { return std::unique_ptr<IEngine>(new VisualizerEngine()); };
    EngineRegistry::registerEngine(desc_visualizer);
    } else {
        LOGW("Registrar", "Skipping VisualizerEngine: No Audio Hardware");
    }
    if (hardwareHAL.isAudioAvailable()) {
    EngineDescriptor desc_decibel;
    desc_decibel.metadata = {"decibel", "Decibel Meter", "info", "1.0.0"};
    desc_decibel.capabilities.needs_audio = true;
    desc_decibel.capabilities.needs_network = false;
    desc_decibel.factory = []() { return std::unique_ptr<IEngine>(new DecibelEngine()); };
    EngineRegistry::registerEngine(desc_decibel);
    } else {
        LOGW("Registrar", "Skipping DecibelEngine: No Audio Hardware");
    }
    EngineDescriptor desc_message;
    desc_message.metadata = {"message", "Custom Message", "info", "1.0.0"};
    desc_message.capabilities.needs_audio = false;
    desc_message.capabilities.needs_network = false;
    desc_message.factory = []() { return std::unique_ptr<IEngine>(new MessageEngine()); };
    EngineRegistry::registerEngine(desc_message);

    EngineDescriptor desc_marquee;
    desc_marquee.metadata = {"marquee", "Marquee Images", "info", "1.0.0"};
    desc_marquee.capabilities.needs_audio = false;
    desc_marquee.capabilities.needs_network = false;
    desc_marquee.factory = []() { return std::unique_ptr<IEngine>(new MarqueeEngine()); };
    EngineRegistry::registerEngine(desc_marquee);
}
