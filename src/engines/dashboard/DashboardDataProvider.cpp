#include "DashboardDataProvider.h"
#include "../../core/Logger.h"
#include "../../core/CpuLoad.h"
#include "../../core/I18n.h"
#include "../../hal/HardwareHAL.h"
#include "../../api/YahooFinanceProvider.h"
#include "../../api/CoinGeckoProvider.h"
#include "../../services/IconService.h"
#include "../../core/net/SecureHttpClient.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "../../core/NetworkBudget.h"
#include <ArduinoJson.h>
#include "../../core/Globals.h"
#include "../../core/SDUtils.h"
#include "../../core/SdLockGuard.h"

DashboardDataProvider::DashboardDataProvider()
    : m_weatherProvider(nullptr),
      m_fetchTaskHandle(nullptr),
      m_taskRunning(false),
      m_taskExited(true),
      m_isActive(false),
      m_forceFetchWeather(false),
      m_forceFetchMarkets(false),
      m_lastBatchFetch(0),
      m_lastSensorFetch(0),
      m_lastSystemFetch(0),
      m_lastSecondSeen(-1),
      m_secondStartMillis(0) {
    // Default valid weather so Outdoor widget displays immediately
    m_snapshot.weather.temp = 21.0f;
    m_snapshot.weather.label = "PARIS";
    m_snapshot.weather.iconCode = "01d";
    m_snapshot.weather.description = "Sunny";
    m_snapshot.weatherValid = true;

    m_snapshot.marketItems.reserve(8);
    m_snapshot.marketItems.push_back(MarketItem("BTC", 90000.0f, 2.5f, true));
    m_snapshot.marketItems.push_back(MarketItem("ETH", 3300.0f, -1.2f, true));
    m_snapshot.marketItems.push_back(MarketItem("SOL", 190.0f, 5.8f, true));
    m_snapshot.marketItems.push_back(MarketItem("NVDA", 135.0f, 3.4f, true));

    m_snapshot.worldTimes.reserve(8);

    for (int b = 0; b < 2; ++b) {
        m_netBuffers[b].weather = m_snapshot.weather;
        m_netBuffers[b].weatherValid = true;
        m_netBuffers[b].marketCount = 4;
        m_netBuffers[b].marketItems[0] = MarketItem("BTC", 90000.0f, 2.5f, true);
        m_netBuffers[b].marketItems[1] = MarketItem("ETH", 3300.0f, -1.2f, true);
        m_netBuffers[b].marketItems[2] = MarketItem("SOL", 190.0f, 5.8f, true);
        m_netBuffers[b].marketItems[3] = MarketItem("NVDA", 135.0f, 3.4f, true);
    }
}

DashboardDataProvider::~DashboardDataProvider() {
    // Destructor safety barrier (Anti-UAF):
    // In normal operation, shutdown() has already completed on Core 0
    // and m_taskExited is true (0 ms wait). If called directly or quarantined destruction
    // was bypassed, we must guarantee the worker is dead before deleting m_weatherProvider.
    if (m_fetchTaskHandle && !m_taskExited.load(std::memory_order_acquire)) {
        m_isActive.store(false, std::memory_order_release);
        m_taskRunning.store(false, std::memory_order_release);
        net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_DASHBOARD);
        while (!m_taskExited.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        m_fetchTaskHandle = nullptr;
    }
    if (m_weatherProvider) {
        delete m_weatherProvider;
        m_weatherProvider = nullptr;
    }
}

void DashboardDataProvider::initialize(IWeatherProvider* weatherProvider) {
    if (m_weatherProvider && m_weatherProvider != weatherProvider) {
        delete m_weatherProvider;
    }
    m_weatherProvider = weatherProvider;
    start();
}

void DashboardDataProvider::start() {
    if (m_taskRunning.load(std::memory_order_acquire)) return;
    m_taskRunning.store(true, std::memory_order_release);
    m_taskExited.store(false, std::memory_order_release);
    m_isActive.store(true, std::memory_order_release);

    BaseType_t res = xTaskCreatePinnedToCore(
        fetchTaskStatic,
        "DashFetch",
        8192,
        this,
        1,
        &m_fetchTaskHandle,
        0 // Core 0 background worker
    );

    if (res != pdPASS) {
        LOGE("Dashboard", "Failed to create DashFetch background task!");
        m_taskRunning.store(false, std::memory_order_release);
        m_taskExited.store(true, std::memory_order_release);
        m_fetchTaskHandle = nullptr;
    } else {
        LOGI("Dashboard", "DashFetch task spawned successfully on Core 0.");
    }
}

void DashboardDataProvider::deactivate() {
    // Non-blocking state transition on Core 1: signal abort and cooperative cancellation
    m_isActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_DASHBOARD);
}

bool DashboardDataProvider::shutdown() {
    m_isActive.store(false, std::memory_order_release);
    m_taskRunning.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_DASHBOARD);

    if (m_fetchTaskHandle) {
        for (int i = 0; i < 30 && !m_taskExited.load(std::memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (m_taskExited.load(std::memory_order_acquire)) {
            m_fetchTaskHandle = nullptr;
            return true;
        } else {
            LOGW("Dashboard", "DashFetch task did not exit within 300ms cooperative window");
            return false;
        }
    }
    return true;
}

void DashboardDataProvider::stop() {
    shutdown();
}

void DashboardDataProvider::updateConfig(const DashboardConfigParams& config, const String& weatherApiKey, const String& weatherCity, const String& weatherUnits) {
    String oldMarkets = m_cachedTrackedMarkets;
    String oldCity = m_weatherCity;
    String oldApiKey = m_weatherApiKey;

    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_config = config;
        m_weatherApiKey = weatherApiKey;
        m_weatherCity = weatherCity;
        m_weatherUnits = weatherUnits;
        m_cachedTrackedMarkets = config.trackedMarkets;
    }

    std::vector<String> symbols;
    String raw = config.trackedMarkets;
    int start = 0;
    while (start < (int)raw.length()) {
        int comma = raw.indexOf(',', start);
        String token = (comma == -1) ? raw.substring(start) : raw.substring(start, comma);
        token.trim();
        token.toUpperCase();
        if (token.length() > 0) symbols.push_back(token);
        if (comma == -1) break;
        start = comma + 1;
    }
    if (!symbols.empty()) {
        std::vector<MarketItem> placeholders;
        placeholders.reserve(symbols.size());
        for (const auto& sym : symbols) {
            bool found = false;
            for (const auto& cur : m_snapshot.marketItems) {
                if (cur.symbol == sym) {
                    placeholders.push_back(cur);
                    found = true;
                    break;
                }
            }
            if (!found) {
                MarketItem placeholder(sym, 0.0f, 0.0f, false);
                placeholders.push_back(placeholder);
            }
        }
        m_snapshot.marketItems = placeholders;

        // Also align m_netBuffers so background network fetch never scrambles the order
        for (int b = 0; b < 2; ++b) {
            uint8_t count = (uint8_t)min((size_t)8, symbols.size());
            MarketItem newItems[8];
            for (uint8_t i = 0; i < count; ++i) {
                bool found = false;
                for (uint8_t j = 0; j < m_netBuffers[b].marketCount; ++j) {
                    if (m_netBuffers[b].marketItems[j].symbol == symbols[i]) {
                        newItems[i] = m_netBuffers[b].marketItems[j];
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    newItems[i] = MarketItem(symbols[i], 0.0f, 0.0f, false);
                }
            }
            m_netBuffers[b].marketCount = count;
            for (uint8_t i = 0; i < count; ++i) {
                m_netBuffers[b].marketItems[i] = newItems[i];
            }
        }
    }

    updateWorldTimes(config.worldClocks);
    if (oldCity != weatherCity || oldApiKey != weatherApiKey) {
        m_forceFetchWeather = true;
    }
    if (oldMarkets != config.trackedMarkets) {
        m_forceFetchMarkets = true;
    }
}

const DashboardSnapshot& DashboardDataProvider::getSnapshot() const {
    return m_snapshot;
}

void DashboardDataProvider::fetchTaskStatic(void* param) {
    DashboardDataProvider* self = static_cast<DashboardDataProvider*>(param);
    if (self) {
        self->fetchTaskLoop();
    }
    vTaskDelete(NULL);
}

void DashboardDataProvider::fetchSynchronousBurst() {
    if (WiFi.status() != WL_CONNECTED) return;
    uint32_t now = millis();
    uint32_t intervalMs = (uint32_t)max(1, m_config.refreshIntervalMin) * 60000UL;
    bool needsFetch = (m_lastBatchFetch == 0) || (now - m_lastBatchFetch >= intervalMs) || m_forceFetchWeather || m_forceFetchMarkets;
    if (!needsFetch) {
        LOGD("Dashboard", "Synchronous pre-fetch skipped: data is fresh (age=%u s, interval=%u s).",
             (unsigned)((now - m_lastBatchFetch) / 1000), (unsigned)(intervalMs / 1000));
        return;
    }

    LOGI("Dashboard", "Executing synchronous transition pre-fetch in DMA-released memory window...");
    m_lastBatchFetch = now;
    m_forceFetchWeather = false;
    m_forceFetchMarkets = false;

    // 1. Preload icons from SD if not already loaded
    preloadIconsFromSd();

    // 2. Fetch Weather
    if (m_config.showWeather) {
        fetchWeather();
    }

    // 3. Fetch Market items
    if (m_config.showMarkets) {
        fetchMarkets();
    }

    LOGI("Dashboard", "Synchronous transition pre-fetch completed.");
}

void DashboardDataProvider::fetchTaskLoop() {
    LOGI("Dashboard", "DashFetch background task started on Core 0.");

    // Core 0 background preload of any existing icons on SD card (zero impact on Core 1)
    preloadIconsFromSd();

#if defined(ESP32)
    // If data was already synchronously pre-fetched during DMA release window, do not wait 4 seconds
    if (!psramFound() && m_lastBatchFetch > 0) {
        // Skip boot delay: data was freshly acquired in DMA-released window
    } else
#endif
    {
        for (int i = 0; i < 40 && m_taskRunning.load(std::memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Delay initial network queries so boot settles
        }
    }

    while (m_taskRunning.load(std::memory_order_acquire)) {
        if (m_isActive.load(std::memory_order_acquire) && WiFi.status() == WL_CONNECTED) {
            uint32_t now = millis();
#if defined(ESP32)
            // On classic ESP32 without PSRAM, once initial data is fetched (m_lastBatchFetch > 0),
            // defer mid-rotation timer refreshes to next rotation/activation (when panel is released & 72 KB is free)
            // unless explicitly forced via m_forceFetchWeather or m_forceFetchMarkets.
            if (!psramFound() && m_lastBatchFetch > 0 && !m_forceFetchWeather && !m_forceFetchMarkets) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
#endif

            uint32_t intervalMs = (uint32_t)max(1, m_config.refreshIntervalMin) * 60000UL;
            bool shouldFetch = m_forceFetchWeather || m_forceFetchMarkets || (m_lastBatchFetch == 0) || (now - m_lastBatchFetch >= intervalMs);

            if (shouldFetch) {
                m_forceFetchWeather = false;
                m_forceFetchMarkets = false;
                m_lastBatchFetch = now;
                LOGI("Dashboard", "Executing sequential synchronized data fetch (interval=%d min)...", m_config.refreshIntervalMin);

                // 1. Fetch Weather first
                if (m_config.showWeather && m_isActive.load(std::memory_order_acquire) && m_taskRunning.load(std::memory_order_acquire)) {
                    fetchWeather();
                    for (int i = 0; i < 5 && m_isActive.load(std::memory_order_acquire) && m_taskRunning.load(std::memory_order_acquire); i++) {
                        vTaskDelay(pdMS_TO_TICKS(100)); // Yield to let Core 0 network memory settle
                    }
                }

                // 2. Fetch Market items strictly one by one
                if (m_config.showMarkets && m_isActive.load(std::memory_order_acquire) && m_taskRunning.load(std::memory_order_acquire)) {
                    fetchMarkets();
                    for (int i = 0; i < 3 && m_isActive.load(std::memory_order_acquire) && m_taskRunning.load(std::memory_order_acquire); i++) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                }

                LOGI("Dashboard", "Sequential data fetch completed. Next refresh in %d min.", m_config.refreshIntervalMin);

                static uint32_t lastHwmLog = 0;
                if (now - lastHwmLog > 30000) {
                    lastHwmLog = now;
                    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
                    LOGD("Dashboard", "DashFetch stack HWM: %u words (%u bytes free)",
                         (unsigned)hwm, (unsigned)(hwm * sizeof(StackType_t)));
                }
            }
        }

        // Responsive sleep in slices to acknowledge cooperative shutdown promptly
        for (int i = 0; i < 10 && m_taskRunning.load(std::memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    m_taskRunning.store(false, std::memory_order_release);
    m_taskExited.store(true, std::memory_order_release);
}

void DashboardDataProvider::updateWorldTimes(const String& clocks) {
    m_snapshot.worldTimes.clear();

    struct TzDef { const char* code; int offsetMin; };
    static const TzDef DEFS[] = {
        {"NYC", -240}, {"TYO", 540},  {"LON", 60},   {"PAR", 120},
        {"BER", 120},  {"ROM", 120},  {"MAD", 120},  {"AMS", 120},
        {"BRU", 120},  {"GVA", 120},  {"ZRH", 120},  {"VIE", 120},
        {"PRG", 120},  {"WAW", 120},  {"ATH", 180},  {"IST", 180},
        {"MOW", 180},  {"DXB", 240},  {"REU", 240},  {"MRU", 240},
        {"DOH", 180},  {"RUH", 180},  {"DEL", 330},  {"BOM", 330},
        {"BKK", 420},  {"JKT", 420},  {"SIN", 480},  {"HKG", 480},
        {"PEK", 480},  {"SHA", 480},  {"TPE", 480},  {"SEL", 540},
        {"SYD", 600},  {"MEL", 600},  {"BNE", 600},  {"AKL", 720},
        {"HNL", -600}, {"ANC", -480}, {"LAX", -420}, {"SFO", -420},
        {"SEA", -420}, {"DEN", -360}, {"CHI", -300}, {"DFW", -300},
        {"MIA", -240}, {"BOS", -240}, {"YUL", -240}, {"YYZ", -240},
        {"YVR", -420}, {"MEX", -360}, {"BOG", -300}, {"LIM", -300},
        {"SCL", -240}, {"EZE", -180}, {"RIO", -180}, {"SAO", -180},
        {"UTC", 0},    {"GMT", 0}
    };

    int start = 0;
    while (start < (int)clocks.length()) {
        int comma = clocks.indexOf(',', start);
        String token = (comma == -1) ? clocks.substring(start) : clocks.substring(start, comma);
        token.trim();
        token.toUpperCase();

        if (token.length() > 0) {
            bool matched = false;

            int colonIdx = token.indexOf(':');
            int plusIdx = token.indexOf('+');
            int minusIdx = token.indexOf('-');

            if (colonIdx != -1) {
                String code = token.substring(0, colonIdx);
                code.trim();
                float offH = token.substring(colonIdx + 1).toFloat();
                WorldTimeItem item;
                item.code = code.substring(0, 4);
                item.offsetMinutes = (int)(offH * 60.0f);
                m_snapshot.worldTimes.push_back(item);
                matched = true;
            } else if ((plusIdx > 0 && plusIdx < 5) || (minusIdx > 0 && minusIdx < 5)) {
                int signIdx = (plusIdx != -1) ? plusIdx : minusIdx;
                String code = token.substring(0, signIdx);
                code.trim();
                float offH = token.substring(signIdx).toFloat();
                WorldTimeItem item;
                item.code = code.substring(0, 4);
                item.offsetMinutes = (int)(offH * 60.0f);
                m_snapshot.worldTimes.push_back(item);
                matched = true;
            } else {
                for (const auto& def : DEFS) {
                    if (token == def.code) {
                        WorldTimeItem item;
                        item.code = def.code;
                        item.offsetMinutes = def.offsetMin;
                        m_snapshot.worldTimes.push_back(item);
                        matched = true;
                        break;
                    }
                }
            }

            if (!matched && token.length() <= 4) {
                WorldTimeItem item;
                item.code = token;
                item.offsetMinutes = 120;
                m_snapshot.worldTimes.push_back(item);
            }
        }

        if (comma == -1) break;
        start = comma + 1;
    }

    if (m_snapshot.worldTimes.empty()) {
        m_snapshot.worldTimes.push_back({"NYC", -240, 0, 0});
        m_snapshot.worldTimes.push_back({"TYO", 540, 0, 0});
        m_snapshot.worldTimes.push_back({"LON", 60, 0, 0});
    }
}

void DashboardDataProvider::fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;

    String apiKey;
    String city;
    String lang;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        apiKey = m_weatherApiKey;
        city = m_weatherCity.isEmpty() ? "Paris" : m_weatherCity;
        lang = m_config.lang;
    }
    lang.toLowerCase();
    if (lang.isEmpty() || lang == "system" || lang == "auto") {
        lang = String(I18n::getLangCode(I18n::getLang()));
    }

    // 1. Try OpenWeatherMap if key is provided
    if (m_weatherProvider && apiKey.length() > 5) {
        WeatherData fc[1];
        int numFc = 0;
        if (m_weatherProvider->fetchForecast(apiKey, city, lang, "metric", fc, 1, numFc)) {
            if (numFc > 0) {
                uint8_t pubIdx = m_netPublishedIdx.load(std::memory_order_relaxed);
                uint8_t writeIdx = 1 - pubIdx;
                m_netBuffers[writeIdx] = m_netBuffers[pubIdx];
                m_netBuffers[writeIdx].weather = fc[0];
                m_netBuffers[writeIdx].weatherValid = true;
                m_netPublishedIdx.store(writeIdx, std::memory_order_release);
                m_netHasNewData.store(true, std::memory_order_release);
                LOGI("Dashboard", "Weather updated (OpenWeatherMap): %.1f°C (%s)", fc[0].temp, fc[0].description.c_str());
                return;
            }
        }
    }

    // 2. Free Open-Meteo fallback (No API key needed!)
    float lat = 48.8566f;
    float lon = 2.3522f;

    net::SecureHttpOptions options;
    options.ownerId = net::OWNER_DASHBOARD;
    options.requestTimeoutMs = 3000;
    options.handshakeTimeoutSec = 4;

    if (!city.equalsIgnoreCase("Paris")) {
        String encCity = city;
        encCity.trim();
        encCity.replace(" ", "%20");
        String geoUrl = "https://geocoding-api.open-meteo.com/v1/search?name=" + encCity + "&count=1&language=" + lang + "&format=json";
        auto geoRes = net::SecureHttpClient::get(geoUrl, options);
        if (geoRes.ok()) {
            DynamicJsonDocument geoDoc(2048);
            if (deserializeJson(geoDoc, geoRes.stream()) == DeserializationError::Ok) {
                if (geoDoc["results"].is<JsonArray>() && geoDoc["results"].size() > 0) {
                    lat = geoDoc["results"][0]["latitude"].as<float>();
                    lon = geoDoc["results"][0]["longitude"].as<float>();
                }
            }
        }
    }

    String metUrl = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) + "&current=temperature_2m,weather_code";
    auto metRes = net::SecureHttpClient::get(metUrl, options);
    if (metRes.ok()) {
        DynamicJsonDocument metDoc(2048);
        if (deserializeJson(metDoc, metRes.stream()) == DeserializationError::Ok) {
                float temp = metDoc["current"]["temperature_2m"].as<float>();
                int wCode = metDoc["current"]["weather_code"].as<int>();

                Lang l = I18n::parseLang(lang);

                WeatherData wd;
                wd.temp = temp;
                wd.iconCode = "01d";
                wd.description = "Clear";

                if (wCode >= 1 && wCode <= 3) { wd.iconCode = "02d"; wd.description = "Clouds"; }
                else if (wCode >= 45 && wCode <= 48) { wd.iconCode = "50d"; wd.description = "Fog"; }
                else if (wCode >= 51 && wCode <= 67) { wd.iconCode = "10d"; wd.description = "Rain"; }
                else if (wCode >= 71 && wCode <= 77) { wd.iconCode = "13d"; wd.description = "Snow"; }
                else if (wCode >= 80 && wCode <= 82) { wd.iconCode = "09d"; wd.description = "Drizzle"; }
                else if (wCode >= 95) { wd.iconCode = "11d"; wd.description = "Storm"; }

                wd.description = I18n::getWeatherCondition(wd.description, l);

                uint8_t pubIdx = m_netPublishedIdx.load(std::memory_order_relaxed);
                uint8_t writeIdx = 1 - pubIdx;
                m_netBuffers[writeIdx] = m_netBuffers[pubIdx];
                m_netBuffers[writeIdx].weather = wd;
                m_netBuffers[writeIdx].weatherValid = true;
                m_netPublishedIdx.store(writeIdx, std::memory_order_release);
                m_netHasNewData.store(true, std::memory_order_release);
                LOGI("Dashboard", "Weather updated (Open-Meteo): %.1f°C (%s)", wd.temp, wd.description.c_str());
            }
        }
    }

void DashboardDataProvider::preloadIconsFromSd() {
    std::vector<String> symbols;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        String raw = m_cachedTrackedMarkets;
        if (raw.isEmpty()) raw = "BTC,ETH,SOL,NVDA";
        int start = 0;
        while (start < (int)raw.length()) {
            int comma = raw.indexOf(',', start);
            String token = (comma == -1) ? raw.substring(start) : raw.substring(start, comma);
            token.trim();
            token.toUpperCase();
            if (token.length() > 0) symbols.push_back(token);
            if (comma == -1) break;
            start = comma + 1;
        }
    }

    bool anyUpdated = false;
    for (const auto& sym : symbols) {
        String symUpper = sym;
        symUpper.toUpperCase();
        symUpper.trim();

        auto it = m_iconCache.find(symUpper);
        if (it != m_iconCache.end() && (it->second.valid || it->second.notFound)) {
            continue;
        }

        uint16_t pixels[64];
        bool loaded = false;
        if (iconService.hasIconOnSd("crypto", symUpper) && iconService.decodeIconFromSd("crypto", symUpper, pixels, 8, 8)) {
            loaded = true;
        } else if (iconService.hasIconOnSd("stock", symUpper) && iconService.decodeIconFromSd("stock", symUpper, pixels, 8, 8)) {
            loaded = true;
        }

        if (loaded) {
            CachedIcon entry;
            entry.valid = true;
            entry.notFound = false;
            entry.lastAttemptMs = millis();
            memcpy(entry.pixels, pixels, sizeof(entry.pixels));
            m_iconCache[symUpper] = entry;

            uint8_t pubIdx = m_netPublishedIdx.load(std::memory_order_relaxed);
            uint8_t writeIdx = 1 - pubIdx;
            m_netBuffers[writeIdx] = m_netBuffers[pubIdx];
            for (uint8_t i = 0; i < m_netBuffers[writeIdx].marketCount; ++i) {
                if (m_netBuffers[writeIdx].marketItems[i].symbol == symUpper) {
                    m_netBuffers[writeIdx].marketItems[i].hasIcon = true;
                    memcpy(m_netBuffers[writeIdx].marketItems[i].iconPixels, pixels, sizeof(pixels));
                    anyUpdated = true;
                    break;
                }
            }
            m_netPublishedIdx.store(writeIdx, std::memory_order_release);
            m_netHasNewData.store(true, std::memory_order_release);
        }
    }
    if (anyUpdated) {
        LOGI("Dashboard", "Preloaded market icons from SD card on Core 0 via IconService.");
    }
}

bool DashboardDataProvider::resolveMarketIcon(const String& symbol, const String& yahooImgUrl, uint16_t outPixels[64]) {
    String symUpper = symbol;
    symUpper.toUpperCase();
    symUpper.trim();

    // Check memory cache first (including negative cache with 1-hour TTL)
    auto it = m_iconCache.find(symUpper);
    if (it != m_iconCache.end()) {
        if (it->second.valid) {
            memcpy(outPixels, it->second.pixels, 64 * sizeof(uint16_t));
            return true;
        }
        if (it->second.notFound && (millis() - it->second.lastAttemptMs < 3600000UL)) {
            return false;
        }
    }

    // Delegate directly to centralized IconService (checks SD .jpg/.png, downloads via HTTP proxy, decodes via JPEGDEC/PNGdec)
    if (iconService.loadOrFetchMarketIcon(symUpper, yahooImgUrl, outPixels, 8, 8)) {
        CachedIcon entry;
        entry.valid = true;
        entry.notFound = false;
        entry.lastAttemptMs = millis();
        memcpy(entry.pixels, outPixels, 64 * sizeof(uint16_t));
        m_iconCache[symUpper] = entry;
        LOGI("Dashboard", "Loaded icon for %s via IconService", symUpper.c_str());
        return true;
    }

    // Not found on SD or web: record negative cache entry (1 hour TTL)
    CachedIcon entry;
    entry.valid = false;
    entry.notFound = true;
    entry.lastAttemptMs = millis();
    m_iconCache[symUpper] = entry;
    LOGI("Dashboard", "Icon for %s not found on SD or web; negative cached for 1h.", symUpper.c_str());
    return false;
}

void DashboardDataProvider::fetchMarkets() {
    if (WiFi.status() != WL_CONNECTED) return;

    std::vector<String> symbols;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        String raw = m_cachedTrackedMarkets;
        int start = 0;
        while (start < (int)raw.length()) {
            int comma = raw.indexOf(',', start);
            String token = (comma == -1) ? raw.substring(start) : raw.substring(start, comma);
            token.trim();
            token.toUpperCase();
            if (token.length() > 0) symbols.push_back(token);
            if (comma == -1) break;
            start = comma + 1;
        }
    }

    if (symbols.empty()) return;

    // Check TLS headroom before attempting any network requests
    if (!NetworkBudget::canStartTlsSession()) {
        LOGW("Dashboard", "Aborting market fetch: internal heap too low (free=%u, largest=%u). Retrying next cycle.",
             (unsigned)NetworkBudget::freeInternal(),
             (unsigned)NetworkBudget::largestInternalBlock());
        return;
    }

    std::map<String, StockQuote> stockQuotes;
    std::map<String, CryptoQuote> cryptoQuotes;

    // Step 1: Batch-query Yahoo Finance for all symbols (stocks and cryptos with -USD mapping)
    // in a single keepalive TLS session
    YahooFinanceProvider yahooProvider;
    yahooProvider.fetchQuotes(symbols, stockQuotes, net::OWNER_DASHBOARD);

    // Step 2: For any symbols not resolved by Yahoo Finance, try CoinGecko as fallback
    std::vector<String> remainingSymbols;
    for (const auto& sym : symbols) {
        if (stockQuotes.find(sym) == stockQuotes.end() || !stockQuotes[sym].valid) {
            remainingSymbols.push_back(sym);
        }
    }

    if (!remainingSymbols.empty() && m_isActive.load(std::memory_order_acquire)) {
        CoinGeckoProvider cgProvider;
        cgProvider.fetchQuotes(remainingSymbols, cryptoQuotes);
    }

    // Step 3: Populate the published double-buffered network items
    uint8_t pubIdx = m_netPublishedIdx.load(std::memory_order_relaxed);
    uint8_t writeIdx = 1 - pubIdx;
    m_netBuffers[writeIdx] = m_netBuffers[pubIdx];

    uint8_t validCount = 0;
    for (size_t s = 0; s < symbols.size() && s < 8; ++s) {
        const auto& sym = symbols[s];
        float fetchedPrice = 0.0f;
        float fetchedChange = 0.0f;
        String fetchedImgUrl = "";
        bool fetchSuccess = false;

        auto itS = stockQuotes.find(sym);
        if (itS != stockQuotes.end() && itS->second.valid) {
            fetchedPrice = itS->second.price;
            fetchedChange = itS->second.change24h;
            fetchedImgUrl = itS->second.imageUrl;
            fetchSuccess = true;
        } else {
            auto itC = cryptoQuotes.find(sym);
            if (itC != cryptoQuotes.end() && itC->second.valid) {
                fetchedPrice = itC->second.price;
                fetchedChange = itC->second.change24h;
                fetchedImgUrl = itC->second.imageUrl;
                fetchSuccess = true;
            }
        }

        if (s >= m_netBuffers[writeIdx].marketCount) {
            m_netBuffers[writeIdx].marketCount = (uint8_t)(s + 1);
        }
        auto& item = m_netBuffers[writeIdx].marketItems[s];
        item.symbol = sym;

        if (fetchSuccess) {
            item.price = fetchedPrice;
            item.change24h = fetchedChange;
            item.valid = true;
            uint16_t iconPixels[64];
            item.hasIcon = resolveMarketIcon(sym, fetchedImgUrl, iconPixels);
            if (item.hasIcon) {
                memcpy(item.iconPixels, iconPixels, sizeof(iconPixels));
            }
            validCount++;
            LOGI("Dashboard", "Market [%u] %s: %.2f (%.2f%%) icon=%d", (unsigned)s, sym.c_str(), fetchedPrice, fetchedChange, (int)item.hasIcon);
        } else {
            LOGW("Dashboard", "No quote available for %s; preserving cache.", sym.c_str());
        }
    }

    if (validCount > 0) {
        m_netPublishedIdx.store(writeIdx, std::memory_order_release);
        m_netHasNewData.store(true, std::memory_order_release);
    }
}

void DashboardDataProvider::update(const DashboardConfigParams& config) {
    // 0. Consume Core 0 network updates lock-free if ready
    if (m_netHasNewData.load(std::memory_order_acquire)) {
        uint8_t pubIdx = m_netPublishedIdx.load(std::memory_order_acquire);
        const auto& net = m_netBuffers[pubIdx];
        m_snapshot.weather = net.weather;
        m_snapshot.weatherValid = net.weatherValid;

        m_snapshot.marketItems.resize(net.marketCount);
        for (uint8_t i = 0; i < net.marketCount; ++i) {
            m_snapshot.marketItems[i] = net.marketItems[i];
        }
        m_netHasNewData.store(false, std::memory_order_release);
    }

    updateSnapshot(config);
}

void DashboardDataProvider::updateSnapshot(const DashboardConfigParams& config) {
    uint32_t now = millis();

    // 1. Time & Synchronized Sub-Second Progress
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        m_snapshot.time.hours = timeinfo.tm_hour;
        m_snapshot.time.minutes = timeinfo.tm_min;
        m_snapshot.time.seconds = timeinfo.tm_sec;
        m_snapshot.time.day = timeinfo.tm_mday;
        m_snapshot.time.month = timeinfo.tm_mon + 1;
        m_snapshot.time.year = timeinfo.tm_year + 1900;
        m_snapshot.time.dayOfWeek = timeinfo.tm_wday;

        if (timeinfo.tm_sec != m_lastSecondSeen) {
            m_lastSecondSeen = timeinfo.tm_sec;
            m_secondStartMillis = now;
        }
    }

    if (config.smoothSeconds) {
        uint32_t elapsed = now - m_secondStartMillis;
        m_snapshot.subSecondFraction = (elapsed < 1000) ? ((float)elapsed / 1000.0f) : 0.999f;
    } else {
        m_snapshot.subSecondFraction = 0.0f;
    }

    // 2. Compute World Clocks Time
    time_t rawtime;
    time(&rawtime);
    struct tm* gm = gmtime(&rawtime);
    if (gm) {
        int utcMinTotal = gm->tm_hour * 60 + gm->tm_min;
        for (auto& item : m_snapshot.worldTimes) {
            int localMin = (utcMinTotal + item.offsetMinutes + 1440) % 1440;
            item.hours = localMin / 60;
            item.minutes = localMin % 60;
        }
    }

    // 3. Indoor Sensor Snapshot (every 2 seconds)
    if (config.showIndoorTemp && (m_lastSensorFetch == 0 || (now - m_lastSensorFetch >= 2000UL))) {
        m_lastSensorFetch = now;
        EnvironmentData env = hardwareHAL.readEnvironment(0.0f);
        m_snapshot.indoor.valid = env.available;
        m_snapshot.indoor.temperatureC = env.temperatureC;
        m_snapshot.indoor.temperatureF = env.temperatureF;
        m_snapshot.indoor.humidityPct = env.humidity;
    }

    // 4. System Metrics Snapshot (every 1 second)
    if (config.showSysInfo && (m_lastSystemFetch == 0 || (now - m_lastSystemFetch >= 1000UL))) {
        m_lastSystemFetch = now;
        uint32_t heapSize = ESP.getHeapSize();
        m_snapshot.system.ramUsagePct = (heapSize > 0) ? ((1.0f - ((float)ESP.getFreeHeap() / (float)heapSize)) * 100.0f) : 0.0f;
        m_snapshot.system.cpuLoadPct = CpuLoad::total();
        m_snapshot.system.wifiRssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
        m_snapshot.system.uptimeSec = now / 1000;
    }
}
