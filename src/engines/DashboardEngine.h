#pragma once
#include <Arduino.h>
class MatrixPanel_I2S_DMA;
#include <vector>
#include <map>
#include <mutex>
#include "../../include/core/EngineContract.h"
#include "../api/IWeatherProvider.h"
#include "../hal/HardwareHAL.h"
#include "TimeData.h"

/**
 * @struct IndoorData
 * @brief Environmental indoor data snapshot from on-board SHTC3 sensor.
 */
struct IndoorData {
    float temperatureC = 0.0f;
    float temperatureF = 0.0f;
    float humidityPct = 0.0f;
    bool valid = false;
};

/**
 * @struct SystemData
 * @brief System hardware and network metrics snapshot.
 */
struct SystemData {
    float cpuLoadPct = 0.0f;
    float ramUsagePct = 0.0f;
    int8_t wifiRssi = -100;
    uint32_t uptimeSec = 0;
    bool valid = true;
};

/**
 * @struct WorldTimeItem
 * @brief Secondary world timezone entry.
 */
struct WorldTimeItem {
    String code;           // e.g. "NYC", "TYO", "LON", "PAR", "LAX", "SYD"
    int offsetMinutes = 0; // UTC offset in minutes
    int hours = 0;
    int minutes = 0;

    WorldTimeItem() = default;
    WorldTimeItem(const String& c, int off, int h = 0, int m = 0)
        : code(c), offsetMinutes(off), hours(h), minutes(m) {}
};

/**
 * @struct MarketItem
 * @brief Real-time cryptocurrency or stock quote.
 */
struct MarketItem {
    String symbol;         // "BTC", "ETH", "SOL", "AAPL", "NVDA", "TSLA", "MSFT", "DOGE"
    float price = 0.0f;
    float change24h = 0.0f;
    bool valid = false;

    MarketItem() = default;
    MarketItem(const String& s, float p, float c, bool v = false)
        : symbol(s), price(p), change24h(c), valid(v) {}
};

/**
 * @struct DashboardTimeData
 * @brief Comprehensive date & time structure for DashboardEngine.
 */
struct DashboardTimeData {
    uint8_t hours = 0;
    uint8_t minutes = 0;
    uint8_t seconds = 0;
    uint8_t day = 1;
    uint8_t month = 1;
    uint16_t year = 2026;
    uint8_t dayOfWeek = 0;
};

/**
 * @struct DashboardSnapshot
 * @brief Immutable atomic state snapshot passed to all widget renderers.
 */
struct DashboardSnapshot {
    DashboardTimeData time;
    uint32_t lastSecondMs = 0;
    float subSecondFraction = 0.0f; // 0.0f to 1.0f synchronized with second tick
    WeatherData weather;
    IndoorData indoor;
    SystemData system;
    std::vector<WorldTimeItem> worldTimes;
    std::vector<MarketItem> marketItems;
    bool weatherValid = false;
};

/**
 * @struct DashboardTheme
 * @brief Color palette for dashboard widgets.
 */
struct DashboardTheme {
    uint16_t primary;
    uint16_t secondary;
    uint16_t accent;
    uint16_t panelBg;
    uint16_t text;
    uint16_t textDim;
    uint16_t border;
    uint16_t green;
    uint16_t red;
};

/**
 * @enum ClockMode
 */
enum class ClockMode {
    MODE_DIGITAL = 0,
    MODE_ANALOG = 1,
    MODE_MINIMAL = 2
};

/**
 * @struct DashboardConfigParams
 */
struct DashboardConfigParams {
    ClockMode clockMode = ClockMode::MODE_ANALOG;
    int theme = 0; // 0=Cyberpunk, 1=Amber HUD, 2=Luxury Ice Blue, 3=Matrix
    bool showClock = true;
    bool showWeather = true;
    bool showIndoorTemp = true;
    bool showSysInfo = true;
    bool showDate = true;
    bool showSeconds = true;
    bool smoothSeconds = true;
    bool showWorldClock = true;
    bool showMarkets = true; // Enabled
    int refreshIntervalMin = 10; // Data refresh rate in minutes (Default 10 min)
    String tempUnit = "system";
    String tempOffsetStr = "";
    String worldClocks = "NYC,TYO,LON";
    String trackedMarkets = "BTC,ETH,SOL,NVDA";
    String lang = "system";
    String format24hStr = "system";
    String city = "PARIS";
    int offsetX = 0;
    int offsetY = 0;
};

/**
 * @struct DashboardLayout
 * @brief Declarative bounded Rect slots calculated once per geometry/config version.
 */
struct DashboardLayout {
    Rect clockRect;
    Rect worldClockRect;
    Rect climateRect;      // Combined Outdoor Weather + Calibrated Indoor Sensor
    Rect marketRect;       // Crypto + Stock Tickers
    Rect sysInfoRect;
    Rect dateRect;

    bool hasClock = false;
    bool hasWorldClock = false;
    bool hasClimate = false;
    bool hasMarket = false;
    bool hasSysInfo = false;
    bool hasDate = false;

    bool isHorizontalDeck = false; // 256x64 Horizontal Desk Clock layout
    bool isVerticalTower = false;  // 64x256 Tall Portrait Tower layout
};

/**
 * @class DashboardLayoutCalculator
 * @brief Pure geometry calculator implementing dual-orientation responsive layouts.
 */
class DashboardLayoutCalculator {
public:
    static DashboardLayout calculate(const DisplayGeometry& geometry, const DashboardConfigParams& config);
};

// ============================================================================
// Stateless Presentation Widgets (Pure Rendering, Zero Lifecycle)
// ============================================================================

class PixelClockWidget {
public:
    static void renderAnalog(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const DashboardTimeData& time, float subSecond, const DashboardTheme& theme, bool showSeconds, bool showDate);
    static void renderDigital(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const DashboardTimeData& time, const DashboardTheme& theme, bool showSeconds, bool showDate, const String& city = "PARIS", bool format24h = true);
};

class WorldClockWidget {
public:
    static void render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const std::vector<WorldTimeItem>& worldTimes, const DashboardTheme& theme);
};

class ClimateWidget {
public:
    static void render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const WeatherData& weather, bool weatherValid, const IndoorData& indoor, float tempOffset, const DashboardTheme& theme, bool useFahrenheit, const String& lang = "en");
};

class MarketWidget {
public:
    static void render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const std::vector<MarketItem>& items, const DashboardTheme& theme);
};

class SysInfoWidget {
public:
    static void render(MatrixPanel_I2S_DMA* matrix, const Rect& rect, const SystemData& sys, const DashboardTheme& theme);
};

// ============================================================================
// Engine Orchestrator
// ============================================================================

class DashboardEngine : public IEngine {
public:
    DashboardEngine();
    ~DashboardEngine() override;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

    bool isRealtime() const override { return true; }

private:
    MatrixPanel_I2S_DMA* matrix;
    DashboardConfigParams m_config;
    DisplayGeometry m_geometry;
    DashboardLayout m_cachedLayout;
    bool m_layoutDirty;

    DashboardSnapshot m_snapshot;
    std::mutex m_snapshotMutex;

    IWeatherProvider* m_weatherProvider;
    uint32_t m_lastBatchFetch;
    uint32_t m_lastWeatherFetch;
    uint32_t m_lastSensorFetch;
    uint32_t m_lastSystemFetch;
    uint32_t m_lastMarketFetch;

    int m_lastSecondSeen;
    uint32_t m_secondStartMillis;

    String m_weatherApiKey;
    String m_weatherCity;
    String m_weatherUnits;
    String m_cachedTrackedMarkets;

    TaskHandle_t m_fetchTaskHandle;
    volatile bool m_taskRunning;
    volatile bool m_isActive;
    volatile bool m_forceFetchMarkets;
    volatile bool m_forceFetchWeather;
    static void fetchTaskStatic(void* param);
    void fetchTaskLoop();

    void updateSnapshot();
    void fetchWeather();
    void fetchMarkets();
    void updateWorldTimes();
    DashboardTheme getTheme(int themeId) const;
};

class DashboardEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
