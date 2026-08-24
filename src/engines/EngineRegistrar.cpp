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

static void tryRegister(const EngineDescriptor& desc) {
    auto res = EngineRegistrar::checkRequirements(desc.requirements);
    if (!res.satisfied) {
        LOGW("Registrar", "Skipping engine %s: %s", desc.metadata.id ? desc.metadata.id : "", res.reason.c_str());
        return;
    }
    EngineRegistry::registerEngine(desc);
}

void EngineRegistrar::registerAll() {
    LOGI("Registrar", "Registering dynamic engines with complete schemas...");

    // 1. Clock Engine
    EngineDescriptor clockDesc;
    clockDesc.metadata = {"clock", "Clock", "info", "3.0.0"};
    clockDesc.capabilities.realtime = true;
    clockDesc.capabilities.allowsOverlay = true;
    clockDesc.requirements.needsAudio = false;
    clockDesc.schema.fields = {
        ConfigField("clock_theme", ConfigType::ENUM, "Clock Theme", "Visual theme / clockface", "0", false, "", "", "", "", "/api/themes", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("clock_format", ConfigType::STRING, "Time Format", "POSIX strftime format", "%H:%M:%S", false, "", "", "", "%H:%M:%S,%H:%M,%I:%M:%S %p,%I:%M %p", "", false, "", ValidationPolicy::Ignore),
        ConfigField("clock_font", ConfigType::ENUM, "Font", "Display typeface", "PressStart2P.ttf", false, "", "", "", "", "/api/fonts", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("timezone", ConfigType::ENUM, "Timezone", "Select timezone or region", "Europe/Paris", false, "", "", "", "", "/api/timezones", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("clock_size", ConfigType::INTEGER, "Font Size", "Text scaling multiplier", "2", false, "1", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("clock_color_1", ConfigType::COLOR, "Primary Color", "Custom gradient top color", "#ffffff", false, "", "", "", "", "", false, "clock_theme=20", ValidationPolicy::Ignore),
        ConfigField("clock_color_2", ConfigType::COLOR, "Secondary Color", "Custom gradient bottom color", "#ff00ff", false, "", "", "", "", "", false, "clock_theme=20", ValidationPolicy::Ignore),
        ConfigField("clock_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("clock_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    clockDesc.factory = []() { return std::unique_ptr<IEngine>(new ClockEngine()); };
    tryRegister(clockDesc);

    // 2. Date Engine
    EngineDescriptor desc_date;
    desc_date.metadata = {"date", "Date", "info", "3.0.0"};
    desc_date.capabilities.realtime = false;
    desc_date.capabilities.allowsOverlay = true;
    desc_date.requirements.needsAudio = false;
    desc_date.requirements.needsNetwork = false;
    desc_date.schema.fields = {
        ConfigField("date_theme", ConfigType::ENUM, "Date Theme", "Visual theme for date", "0", false, "", "", "", "", "/api/themes", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("date_format", ConfigType::STRING, "Date Format", "Format for date display", "%d/%m/%Y", false, "", "", "", "%d/%m/%Y,%Y-%m-%d,%d %b %Y,%A %d %B", "", false, "", ValidationPolicy::Ignore),
        ConfigField("date_font", ConfigType::ENUM, "Font", "Display typeface", "PressStart2P.ttf", false, "", "", "", "", "/api/fonts", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("timezone", ConfigType::ENUM, "Timezone", "Select timezone or region", "Europe/Paris", false, "", "", "", "", "/api/timezones", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("date_size", ConfigType::INTEGER, "Font Size", "Text scaling multiplier", "1", false, "1", "3", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("date_color_1", ConfigType::COLOR, "Primary Color", "Custom gradient top color", "#ffffff", false, "", "", "", "", "", false, "date_theme=20", ValidationPolicy::Ignore),
        ConfigField("date_color_2", ConfigType::COLOR, "Secondary Color", "Custom gradient bottom color", "#00ffff", false, "", "", "", "", "", false, "date_theme=20", ValidationPolicy::Ignore),
        ConfigField("date_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("date_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_date.factory = []() { return std::unique_ptr<IEngine>(new DateEngine()); };
    tryRegister(desc_date);

    // 3. Weather Engine
    EngineDescriptor desc_weather;
    desc_weather.metadata = {"weather", "Weather", "info", "3.0.0"};
    desc_weather.capabilities.realtime = false;
    desc_weather.capabilities.allowsOverlay = true;
    desc_weather.requirements.needsAudio = false;
    desc_weather.requirements.needsNetwork = true;
    desc_weather.schema.fields = {
        ConfigField("api_key", ConfigType::STRING, "API Key", "OpenWeatherMap API Key", "", false, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("city", ConfigType::STRING, "City", "City (e.g. Paris,FR or for US: Tucson,AZ,US)", "Paris,FR", true, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("units", ConfigType::ENUM, "Units", "Temperature unit (°C or °F)", "metric", false, "", "", "", "metric,imperial", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Language", "Language code for day labels", "fr", false, "", "", "", "fr,en,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("weather_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("weather_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_weather.factory = []() { return std::unique_ptr<IEngine>(new WeatherEngine()); };
    tryRegister(desc_weather);

    // 4. Fighter Engine (Overlay)
    EngineDescriptor desc_fighter;
    desc_fighter.metadata = {"fighter", "Fighter", "overlay", "3.0.0"};
    desc_fighter.capabilities.realtime = true;
    desc_fighter.capabilities.isOverlay = true;
    desc_fighter.capabilities.allowsOverlay = false;
    desc_fighter.requirements.needsAudio = false;
    desc_fighter.requirements.needsNetwork = false;
    desc_fighter.schema.fields = {
        ConfigField("enabled", ConfigType::BOOLEAN, "Enabled", "Enable M.U.G.E.N fighter overlay pass", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("fighter_interval_sec", ConfigType::INTEGER, "Switch Interval", "Seconds between fighter changes", "15", false, "5", "120", "5", "", "", false, "", ValidationPolicy::Clamp)
    };
    // desc_fighter.factory = []() { return std::unique_ptr<IEngine>(new FighterEngine()); };
    // tryRegister(desc_fighter); // Removed, Fighter is an overlay, not a standalone engine

    // 5. GIF Player Engine
    EngineDescriptor desc_gifs;
    desc_gifs.metadata = {"gifs", "GIF Player", "media", "3.0.0"};
    desc_gifs.capabilities.realtime = true;
    desc_gifs.capabilities.allowsOverlay = false;
    desc_gifs.capabilities.selfPaced = true;
    desc_gifs.requirements.needsAudio = false;
    desc_gifs.requirements.needsNetwork = false;
    desc_gifs.schema.fields = {
        ConfigField("folder", ConfigType::LIST, "Playlists", "Active GIF playlists", "all", false, "", "", "", "", "/api/playlists", true, "", ValidationPolicy::Ignore),
        ConfigField("speed_multiplier", ConfigType::FLOAT, "Speed Multiplier", "Playback speed factor", "1.0", false, "0.25", "3.0", "0.25", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("shuffle", ConfigType::BOOLEAN, "Shuffle", "Randomize animation order", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("duration_sec", ConfigType::INTEGER, "Duration per GIF", "Seconds per animation", "10", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_gifs.factory = []() { return std::unique_ptr<IEngine>(new GifEngine()); };
    tryRegister(desc_gifs);

    // 6. Crypto Engine (Requires PSRAM)
    EngineDescriptor desc_crypto;
    desc_crypto.metadata = {"crypto", "Crypto Tracker", "finance", "3.0.0"};
    desc_crypto.capabilities.realtime = false;
    desc_crypto.capabilities.allowsOverlay = true;
    desc_crypto.requirements.needsPsram = true;
    desc_crypto.requirements.needsNetwork = true;
    desc_crypto.schema.fields = {
        ConfigField("symbols", ConfigType::STRING, "Symbols", "Comma-separated crypto symbols", "BTC,ETH,SOL", true, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("show_chart", ConfigType::BOOLEAN, "Show Chart", "Display historical price sparkline chart", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("chart_timeframe", ConfigType::ENUM, "Chart Timeframe", "Historical chart timeframe", "daily", false, "", "", "", "hourly,daily,weekly,monthly", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("duration_sec", ConfigType::INTEGER, "Page Duration (s)", "Seconds to dwell on each view", "5", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("currency", ConfigType::ENUM, "Fiat Currency", "Target currency for quotes", "USD", false, "", "", "", "USD,EUR,GBP,JPY", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("provider", ConfigType::ENUM, "Provider", "Market data provider", "coingecko", false, "", "", "", "coingecko,binance", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("cache_ttl_min", ConfigType::INTEGER, "Cache TTL (min)", "Minutes between fresh API requests", "5", false, "1", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("crypto_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("crypto_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_crypto.factory = []() { return std::unique_ptr<IEngine>(new CryptoEngine()); };
    tryRegister(desc_crypto);

    // 7. Stock Engine (Requires PSRAM)
    EngineDescriptor desc_stock;
    desc_stock.metadata = {"stock", "Stock Ticker", "finance", "3.0.0"};
    desc_stock.capabilities.realtime = false;
    desc_stock.capabilities.allowsOverlay = true;
    desc_stock.requirements.needsPsram = true;
    desc_stock.requirements.needsNetwork = true;
    desc_stock.schema.fields = {
        ConfigField("symbols", ConfigType::STRING, "Symbols", "Comma-separated stock symbols", "AAPL,TSLA,NVDA", true, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("show_chart", ConfigType::BOOLEAN, "Show Chart", "Display historical price sparkline chart", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("chart_timeframe", ConfigType::ENUM, "Chart Timeframe", "Historical chart timeframe", "daily", false, "", "", "", "hourly,daily,weekly,monthly", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("duration_sec", ConfigType::INTEGER, "Page Duration (s)", "Seconds to dwell on each view", "5", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("provider", ConfigType::ENUM, "Provider", "Market data provider", "yahoo", false, "", "", "", "yahoo", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("cache_ttl_min", ConfigType::INTEGER, "Cache TTL (min)", "Minutes between fresh API requests", "5", false, "1", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("stock_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("stock_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_stock.factory = []() { return std::unique_ptr<IEngine>(new StockEngine()); };
    tryRegister(desc_stock);

    // 8. Visualizer Engine (Requires Microphone)
    EngineDescriptor desc_visualizer;
    desc_visualizer.metadata = {"audiovisualizer", "Audio Visualizer", "audio", "3.0.0"};
    desc_visualizer.capabilities.realtime = true;
    desc_visualizer.capabilities.allowsOverlay = false;
    desc_visualizer.requirements.needsAudio = true;
    desc_visualizer.schema.fields = {
        ConfigField("enabled", ConfigType::BOOLEAN, "Enabled", "Enable real-time spectrum display", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("style", ConfigType::ENUM, "Style", "FFT visualization style", "spectrum", false, "", "", "", "spectrum,waveform,radial,neon_fire", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("sensitivity", ConfigType::INTEGER, "Sensitivity", "Microphone sensitivity", "5", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("gain", ConfigType::FLOAT, "Audio Gain", "Gain scaling factor", "1.0", false, "0.5", "5.0", "0.5", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_visualizer.factory = []() { return std::unique_ptr<IEngine>(new VisualizerEngine()); };
    tryRegister(desc_visualizer);

    // 9. Decibel Engine (Requires Microphone)
    EngineDescriptor desc_decibel;
    desc_decibel.metadata = {"decibelMeter", "Noise Level", "audio", "3.0.0"};
    desc_decibel.capabilities.realtime = true;
    desc_decibel.capabilities.allowsOverlay = false;
    desc_decibel.requirements.needsAudio = true;
    desc_decibel.schema.fields = {
        ConfigField("threshold", ConfigType::INTEGER, "Alert Threshold (dB)", "Warning threshold level", "80", false, "40", "120", "5", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_decibel.factory = []() { return std::unique_ptr<IEngine>(new DecibelEngine()); };
    tryRegister(desc_decibel);

    // 10. Temperature Engine (Requires Temp Sensor)
    EngineDescriptor desc_temp;
    desc_temp.metadata = {"temp", "Environment Sensor", "sensor", "3.0.0"};
    desc_temp.capabilities.realtime = false;
    desc_temp.capabilities.allowsOverlay = true;
    desc_temp.requirements.needsTempSensor = true;
    desc_temp.schema.fields = {
        ConfigField("units", ConfigType::ENUM, "Units", "Temperature measurement units", "C", false, "", "", "", "C,F", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("temp_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("temp_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_temp.factory = []() { return std::unique_ptr<IEngine>(new TempEngine()); };
    tryRegister(desc_temp);

    // 11. Message Engine (Unified text scrolling & banner engine)
    EngineDescriptor desc_msg;
    desc_msg.metadata = {"message", "Message", "display", "3.0.0"};
    desc_msg.capabilities.realtime = true;
    desc_msg.capabilities.allowsOverlay = false;
    desc_msg.schema.fields = {
        ConfigField("text", ConfigType::STRING, "Message Text", "Text banner or message to display", "ArcadeMatrix", true, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("color", ConfigType::COLOR, "Text Color", "Hex color code (#RRGGBB)", "#ffffff", false, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("size", ConfigType::INTEGER, "Font Size", "Text scale multiplier", "1", false, "1", "4", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("direction", ConfigType::ENUM, "Direction", "Scroll direction or static", "rtl", false, "", "", "", "rtl,ltr,ttb,btt,static", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("speed", ConfigType::INTEGER, "Speed (ms)", "Scroll delay per step (lower is faster)", "50", false, "10", "200", "5", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("font", ConfigType::ENUM, "Font", "Display typeface", "Default", false, "", "", "", "", "/api/fonts", false, "", ValidationPolicy::FallbackDefault)
    };
    desc_msg.factory = []() { return std::unique_ptr<IEngine>(new MessageEngine()); };
    tryRegister(desc_msg);
    
    LOGI("Registrar", "All engines successfully registered (%u available).", EngineRegistry::count());
}
