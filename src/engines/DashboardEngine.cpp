#include "DashboardEngine.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../core/Logger.h"
#include "../core/ConfigLoader.h"
#include "../api/OpenWeatherMapProvider.h"

DashboardEngine::DashboardEngine()
    : matrix(nullptr), m_layoutDirty(true),
      m_weatherApiKey(""), m_weatherCity("Paris"), m_weatherUnits("metric") {
}

DashboardEngine::~DashboardEngine() {
    m_dataProvider.stop();
}

EngineError DashboardEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    LOGI("Dashboard", "DashboardEngine::initialize called.");
    if (!context || !context->getMatrix()) {
        LOGE("Dashboard", "DashboardEngine::initialize: Invalid context or matrix!");
        return EngineError::InvalidConfig;
    }
    matrix = context->getMatrix();
    m_geometry = context->getGeometry();

    onConfigChanged(engineConfig);

    m_dataProvider.initialize(new OpenWeatherMapProvider());
    LOGI("Dashboard", "DashboardEngine::initialize complete.");
    return EngineError::OK;
}

void DashboardEngine::activate() {
    LOGI("Dashboard", "DashboardEngine::activate called.");
    m_dataProvider.start();
    m_dataProvider.forceFetchWeather();
    m_dataProvider.forceFetchMarkets();
    LOGI("Dashboard", "DashboardEngine::activate complete.");
}

void DashboardEngine::deactivate() {
    LOGI("Dashboard", "DashboardEngine::deactivate called.");
    m_dataProvider.stop();
}

void DashboardEngine::update(EngineContext* context) {
    m_dataProvider.update(m_config);
}

void DashboardEngine::render(EngineContext* context) {
    if (!matrix) return;

    if (m_layoutDirty) {
        m_cachedLayout = DashboardLayoutCalculator::calculate(m_geometry, m_config);
        m_layoutDirty = false;
    }

    DashboardTheme theme = getDashboardTheme(matrix, m_config.theme);
    DashboardSnapshot snap = m_dataProvider.getSnapshot();

    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();

    bool useFahrenheit = m_config.tempUnit.equalsIgnoreCase("F") || m_config.tempUnit.equalsIgnoreCase("imperial") || 
                         ((m_config.tempUnit.isEmpty() || m_config.tempUnit.equalsIgnoreCase("system")) && 
                          (guard->system.unit.equalsIgnoreCase("F") || guard->system.unit.equalsIgnoreCase("imperial")));

    float tempOffset = (m_config.tempOffsetStr.isEmpty() || m_config.tempOffsetStr.equalsIgnoreCase("system")) ? 
                       guard->system.temp_offset : m_config.tempOffsetStr.toFloat();

    String lang = (m_config.lang.isEmpty() || m_config.lang.equalsIgnoreCase("system")) ? 
                  (guard->system.lang.length() > 0 ? guard->system.lang : "en") : m_config.lang;

    bool format24h = (m_config.format24hStr.isEmpty() || m_config.format24hStr.equalsIgnoreCase("system")) ? 
                     guard->system.format24h : 
                     (m_config.format24hStr.equalsIgnoreCase("24h") || m_config.format24hStr.equalsIgnoreCase("true") || m_config.format24hStr == "1");

    // 1. Clock Widget (Analog or Digital)
    if (m_cachedLayout.hasClock) {
        if (m_config.clockMode == ClockMode::MODE_ANALOG) {
            PixelClockWidget::renderAnalog(matrix, m_cachedLayout.clockRect, snap.time, snap.subSecondFraction, theme, m_config.showSeconds, m_config.showDate);
        } else if (m_config.clockMode == ClockMode::MODE_MINIMAL) {
            PixelClockWidget::renderDigital(matrix, m_cachedLayout.clockRect, snap.time, theme, false, false, m_config.city, format24h);
        } else {
            PixelClockWidget::renderDigital(matrix, m_cachedLayout.clockRect, snap.time, theme, m_config.showSeconds, m_config.showDate, m_config.city, format24h);
        }
    }

    // 2. World Clock Widget
    if (m_cachedLayout.hasWorldClock) {
        WorldClockWidget::render(matrix, m_cachedLayout.worldClockRect, snap.worldTimes, theme);
    }

    // 3. Climate Widget (Outdoor + Calibrated Indoor Sensor)
    if (m_cachedLayout.hasClimate) {
        ClimateWidget::render(matrix, m_cachedLayout.climateRect, snap.weather, snap.weatherValid, snap.indoor, tempOffset, theme, useFahrenheit, lang);
    }

    // 4. Market Widget (Crypto + Stock Quotes)
    if (m_cachedLayout.hasMarket) {
        MarketWidget::render(matrix, m_cachedLayout.marketRect, snap.marketItems, theme);
    }

    // 5. SysInfo Widget
    if (m_cachedLayout.hasSysInfo) {
        SysInfoWidget::render(matrix, m_cachedLayout.sysInfoRect, snap.system, theme);
    }
}

void DashboardEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;

    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();

    m_config.clockMode = static_cast<ClockMode>(engineConfig->getInt("clock_mode", 1)); // Default Analog
    m_config.theme = engineConfig->getInt("theme", 0);
    m_config.showClock = engineConfig->getBool("show_clock", true);
    m_config.showWeather = engineConfig->getBool("show_weather", true);
    m_config.showIndoorTemp = engineConfig->getBool("show_indoor_temp", true);
    m_config.showSysInfo = engineConfig->getBool("show_sysinfo", true);
    m_config.showDate = engineConfig->getBool("show_date", true);
    m_config.showSeconds = engineConfig->getBool("show_seconds", true);
    m_config.smoothSeconds = engineConfig->getBool("smooth_seconds", true);
    m_config.showWorldClock = engineConfig->getBool("show_world_clock", true);
    m_config.showMarkets = engineConfig->getBool("show_markets", true);
    m_config.worldClocks = engineConfig->getString("world_clocks", "NYC,TYO,LON");
    m_config.trackedMarkets = engineConfig->getString("tracked_markets", "BTC,ETH,SOL,NVDA");

    // Unit, temp offset, lang and 24h format stored as configured (supporting dynamic "system" inheritance)
    String unit = engineConfig->getString("temp_unit", "");
    if (unit.isEmpty()) unit = engineConfig->getString("units", "system");
    m_config.tempUnit = unit;
    m_config.tempOffsetStr = engineConfig->getString("temp_offset", "");
    m_config.lang = engineConfig->getString("lang", "system");
    m_config.format24hStr = engineConfig->getString("format_24h", "system");

    // Refresh interval: data update frequency in minutes (default 10 min)
    m_config.refreshIntervalMin = engineConfig->getInt("refresh_interval", 10);
    if (m_config.refreshIntervalMin < 1) m_config.refreshIntervalMin = 10;

    m_weatherApiKey = engineConfig->getString("weather_api_key", "");
    m_weatherCity = engineConfig->getString("weather_city", "");
    m_weatherUnits = engineConfig->getString("weather_units", "metric");

    if (m_weatherApiKey.isEmpty() || m_weatherCity.isEmpty()) {
        for (const auto& inst : guard->instances) {
            if (inst.engine_id == "weather") {
                if (m_weatherApiKey.isEmpty()) m_weatherApiKey = inst.config.getString("api_key", "");
                if (m_weatherCity.isEmpty()) m_weatherCity = inst.config.getString("city", "");
                if (m_weatherUnits.isEmpty()) m_weatherUnits = inst.config.getString("units", "metric");
                break;
            }
        }
    }

    m_config.city = m_weatherCity.isEmpty() ? "PARIS" : m_weatherCity;

    m_cachedLayout = DashboardLayoutCalculator::calculate(m_geometry, m_config);
    m_layoutDirty = false;

    m_dataProvider.updateConfig(m_config, m_weatherApiKey, m_weatherCity, m_weatherUnits);
}

void DashboardEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_geometry = geometry;
    m_cachedLayout = DashboardLayoutCalculator::calculate(m_geometry, m_config);
    m_layoutDirty = false;
}

// ============================================================================
// Descriptor Handler Registration
// ============================================================================

EngineDescriptor DashboardEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"dashboard", "Dashboard Engine", "info", FIRMWARE_VERSION};
    desc.requirements.needsPsram = false;
    desc.requirements.needsAudio = false;
    desc.requirements.needsTempSensor = false;
    desc.requirements.needsGyroscope = false;

    desc.schema.fields = {
        ConfigField("clock_mode", ConfigType::ENUM, "Clock Style", "Display as Digital or Analog Hands", "1", false, "", "", "", "0:Digital Modern,1:Pixel-Art Watch Dial,2:Minimal", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("theme", ConfigType::ENUM, "Color Theme", "Color palette for dashboard widgets", "0", false, "", "", "", "0:Cyberpunk Neon,1:Arcade Amber HUD,2:Minimalist Luxury,3:Matrix Phosphor", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_clock", ConfigType::BOOLEAN, "Show Clock", "Display main time widget", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_world_clock", ConfigType::BOOLEAN, "Show World Clocks", "Display secondary timezones (NYC, TYO, LON...)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("world_clocks", ConfigType::STRING, "World Timezones", "Select timezones from the list or enter custom city/airport codes and offsets (e.g. NYC,TYO,LON,PAR,DXB,SIN,LAX,MIA,HKG,SYD,BER,ROM,MAD,AMS,YUL,UTC or REU:+4)", "NYC,TYO,LON", false, "", "", "", "NYC:New York (NYC),TYO:Tokyo (TYO),LON:London (LON),PAR:Paris (PAR),LAX:Los Angeles (LAX),SFO:San Francisco (SFO),CHI:Chicago (CHI),MIA:Miami (MIA),DXB:Dubai (DXB),SIN:Singapore (SIN),HKG:Hong Kong (HKG),SYD:Sydney (SYD),BER:Berlin (BER),ROM:Rome (ROM),MAD:Madrid (MAD),AMS:Amsterdam (AMS),YUL:Montreal (YUL),UTC:UTC (GMT)", "", true, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_weather", ConfigType::BOOLEAN, "Show Weather", "Display outdoor weather & temp", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("weather_city", ConfigType::STRING, "Weather City", "City name for weather forecasts (e.g. Paris, London, Tokyo, New York)", "Paris", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("weather_api_key", ConfigType::STRING, "OpenWeatherMap Key (Optional)", "OpenWeatherMap API Key (leave empty to use free Open-Meteo service without key)", "", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_indoor_temp", ConfigType::BOOLEAN, "Show Indoor Climate (SHTC3)", "Display room temperature & humidity", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("temp_unit", ConfigType::ENUM, "Temperature Unit", "Celsius (°C) or Fahrenheit (°F)", "system", false, "", "", "", "system:System (General),C:Celsius (°C),F:Fahrenheit (°F)", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("temp_offset", ConfigType::FLOAT, "Indoor Temp Offset", "Offset to compensate for CPU heat dissipation in the chosen temperature unit (leave empty to use General System setting)", "", false, "-30.0", "30.0", "0.5", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("refresh_interval", ConfigType::ENUM, "Refresh Interval", "Data refresh frequency for weather and markets", "10", false, "", "", "", "1:1 Minute,5:5 Minutes,10:10 Minutes (Recommended),15:15 Minutes,30:30 Minutes,60:60 Minutes", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("format_24h", ConfigType::ENUM, "Time Format", "24H or 12H time format", "system", false, "", "", "", "system:System (General),24h:24 Hours (23:59),12h:12 Hours (11:59 PM)", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Language", "Language for labels and dates", "system", false, "", "", "", "system:System (General),fr:Français,en:English,es:Español", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_markets", ConfigType::BOOLEAN, "Show Markets / Stocks", "Display live crypto and stock ticker badges", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("tracked_markets", ConfigType::STRING, "Tracked Markets (Crypto & Stocks)", "Select symbols from list or enter custom ticker symbols (e.g. PEPE, KAS, TAO, NVDA, AAPL, MSFT, BTC, ETH, SOL...)", "BTC,ETH,SOL,NVDA", false, "", "", "", "BTC:Bitcoin (BTC),ETH:Ethereum (ETH),SOL:Solana (SOL),BNB:Binance (BNB),XRP:Ripple (XRP),DOGE:Dogecoin (DOGE),ADA:Cardano (ADA),AVAX:Avalanche (AVAX),LINK:Chainlink (LINK),SUI:Sui (SUI),NEAR:Near (NEAR),PEPE:Pepe (PEPE),SHIB:Shiba (SHIB),TAO:Bittensor (TAO),APT:Aptos (APT),KAS:Kaspa (KAS),RENDER:Render (RENDER),FET:Fetch.ai (FET),INJ:Injective (INJ),BONK:Bonk (BONK),WIF:Dogwifhat (WIF),NVDA:Nvidia (NVDA),AAPL:Apple (AAPL),TSLA:Tesla (TSLA),MSFT:Microsoft (MSFT),GOOG:Alphabet (GOOG),AMZN:Amazon (AMZN),META:Meta (META),AMD:AMD (AMD),PLTR:Palantir (PLTR),MSTR:MicroStrategy (MSTR),COIN:Coinbase (COIN),SPY:S&P 500 (SPY),QQQ:Nasdaq (QQQ)", "", true, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_sysinfo", ConfigType::BOOLEAN, "Show System Vitals", "Display RAM, CPU & WiFi gauges", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_date", ConfigType::BOOLEAN, "Show Date", "Display day and date badge", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_seconds", ConfigType::BOOLEAN, "Show Seconds", "Display sweeping second hand or seconds digits", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("smooth_seconds", ConfigType::BOOLEAN, "Smooth Sweeping Seconds", "Continuous sweeping second hand vs crisp 1s ticks", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };

    desc.factory = []() { return std::unique_ptr<IEngine>(new DashboardEngine()); };
    return desc;
}
