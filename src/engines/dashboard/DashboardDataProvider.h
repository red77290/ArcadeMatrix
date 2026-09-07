#pragma once
#include <Arduino.h>
#include <mutex>
#include <map>
#include "DashboardData.h"
#include "../../api/IWeatherProvider.h"

/**
 * @class DashboardDataProvider
 * @brief Non-visual asynchronous data provider, environmental sensor poller, and market/weather fetcher.
 */
class DashboardDataProvider {
public:
    DashboardDataProvider();
    ~DashboardDataProvider();

    void initialize(IWeatherProvider* weatherProvider);
    void start();
    void stop();

    void updateConfig(const DashboardConfigParams& config, const String& weatherApiKey, const String& weatherCity, const String& weatherUnits);
    void update(const DashboardConfigParams& config);
    DashboardSnapshot getSnapshot() const;

    void forceFetchWeather() { m_forceFetchWeather = true; }
    void forceFetchMarkets() { m_forceFetchMarkets = true; }

private:
    static void fetchTaskStatic(void* param);
    void fetchTaskLoop();

    void fetchWeather();
    void fetchMarkets();
    void updateSnapshot(const DashboardConfigParams& config);
    void updateWorldTimes(const String& worldClocks);

    struct CachedIcon {
        bool valid = false;
        uint16_t pixels[64];
    };
    std::map<String, CachedIcon> m_iconCache;

    void preloadIconsFromSd();
    bool loadIconFromSd(const String& path, uint16_t outPixels[64]);
    bool downloadIconViaProxy(const String& targetUrl, const String& destPath);
    bool resolveMarketIcon(const String& symbol, const String& yahooImgUrl, uint16_t outPixels[64]);

    IWeatherProvider* m_weatherProvider;
    DashboardSnapshot m_snapshot;
    mutable std::mutex m_snapshotMutex;

    TaskHandle_t m_fetchTaskHandle;
    volatile bool m_taskRunning;
    volatile bool m_isActive;

    DashboardConfigParams m_config;
    String m_weatherApiKey;
    String m_weatherCity;
    String m_weatherUnits;
    String m_cachedTrackedMarkets;

    volatile bool m_forceFetchWeather;
    volatile bool m_forceFetchMarkets;

    uint32_t m_lastBatchFetch;
    uint32_t m_lastSensorFetch;
    uint32_t m_lastSystemFetch;
    int m_lastSecondSeen;
    uint32_t m_secondStartMillis;
};
