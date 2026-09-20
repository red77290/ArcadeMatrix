#include "DashboardDataProvider.h"
#include "../../core/Logger.h"
#include "../../core/CpuLoad.h"
#include "../../core/I18n.h"
#include "../../hal/HardwareHAL.h"
#include "../../api/YahooFinanceProvider.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "../../core/NetworkBudget.h"
#include <ArduinoJson.h>
#include <PNGdec.h>
#include "../../core/Globals.h"
#include "../../core/SDUtils.h"
#include "../../core/SdLockGuard.h"

struct DashPngDecodeContext {
    uint16_t* outPixels;
    PNG* png;
    int srcW;
    int srcH;
    uint16_t* lineBuf;
};

static DashPngDecodeContext s_dashPngContext;

static int dashPngDrawCallback(PNGDRAW* pDraw) {
    if (!s_dashPngContext.outPixels || !s_dashPngContext.png || !s_dashPngContext.lineBuf) return 0;
    int y = pDraw->y;
    int srcW = pDraw->iWidth;
    int srcH = (s_dashPngContext.srcH > 0) ? s_dashPngContext.srcH : 8;

    int targetY = (y * 8) / srcH;
    if (targetY < 0 || targetY >= 8) return 1;

    int rowBucketStart = (targetY * srcH) / 8;
    if (y != rowBucketStart) return 1;

    s_dashPngContext.png->getLineAsRGB565(pDraw, s_dashPngContext.lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);

    for (int tx = 0; tx < 8; tx++) {
        int srcX = (tx * srcW) / 8;
        if (srcX < srcW) {
            uint16_t col = s_dashPngContext.lineBuf[srcX];
            s_dashPngContext.outPixels[targetY * 8 + tx] = col;
        }
    }
    return 1;
}

static bool decodePngTo8x8(const uint8_t* buf, size_t size, uint16_t outPixels[64]) {
    if (!buf || size == 0 || !outPixels) return false;

    memset(outPixels, 0, 64 * sizeof(uint16_t));

    PNG* png = new PNG();
    if (!png) return false;

    s_dashPngContext.outPixels = outPixels;
    s_dashPngContext.png = png;
    s_dashPngContext.lineBuf = nullptr;

    int rc = png->openRAM((uint8_t*)buf, size, dashPngDrawCallback);
    if (rc != PNG_SUCCESS) {
        s_dashPngContext.png = nullptr;
        s_dashPngContext.outPixels = nullptr;
        delete png;
        return false;
    }

    s_dashPngContext.srcW = png->getWidth();
    s_dashPngContext.srcH = png->getHeight();

    // Reject degenerate or excessively large images (> 256x256) to protect memory and avoid crashes
    if (s_dashPngContext.srcW <= 0 || s_dashPngContext.srcH <= 0 ||
        s_dashPngContext.srcW > 256 || s_dashPngContext.srcH > 256) {
        png->close();
        s_dashPngContext.png = nullptr;
        s_dashPngContext.outPixels = nullptr;
        delete png;
        return false;
    }

    uint16_t* lineBuf = (uint16_t*)malloc(s_dashPngContext.srcW * sizeof(uint16_t));
    if (!lineBuf) {
        png->close();
        s_dashPngContext.png = nullptr;
        s_dashPngContext.outPixels = nullptr;
        delete png;
        return false;
    }
    s_dashPngContext.lineBuf = lineBuf;

    rc = png->decode(NULL, 0);
    png->close();

    free(lineBuf);
    s_dashPngContext.lineBuf = nullptr;
    s_dashPngContext.png = nullptr;
    s_dashPngContext.outPixels = nullptr;
    delete png;
    return (rc == PNG_SUCCESS);
}

DashboardDataProvider::DashboardDataProvider()
    : m_weatherProvider(nullptr),
      m_fetchTaskHandle(nullptr),
      m_taskRunning(false),
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
    stop();
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
    if (m_taskRunning) return;
    m_taskRunning = true;
    m_isActive = true;

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
        m_taskRunning = false;
        m_fetchTaskHandle = nullptr;
    } else {
        LOGI("Dashboard", "DashFetch task spawned successfully on Core 0.");
    }
}

void DashboardDataProvider::stop() {
    m_isActive = false;
    m_taskRunning = false;

    if (m_fetchTaskHandle) {
        int timeoutMs = 500;
        while (m_fetchTaskHandle != nullptr && timeoutMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            timeoutMs -= 10;
        }
        if (m_fetchTaskHandle) {
            vTaskDelete(m_fetchTaskHandle);
            m_fetchTaskHandle = nullptr;
        }
    }
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

void DashboardDataProvider::fetchTaskLoop() {
    LOGI("Dashboard", "DashFetch background task started on Core 0.");

    // Core 0 background preload of any existing icons on SD card (zero impact on Core 1)
    preloadIconsFromSd();

    vTaskDelay(pdMS_TO_TICKS(4000)); // Delay initial network queries so boot settles

    while (m_taskRunning) {
        if (m_isActive && WiFi.status() == WL_CONNECTED) {
            uint32_t now = millis();
            uint32_t intervalMs = (uint32_t)max(1, m_config.refreshIntervalMin) * 60000UL;

            bool shouldFetch = m_forceFetchWeather || m_forceFetchMarkets || (m_lastBatchFetch == 0) || (now - m_lastBatchFetch >= intervalMs);

            if (shouldFetch) {
                m_forceFetchWeather = false;
                m_forceFetchMarkets = false;
                m_lastBatchFetch = now;
                LOGI("Dashboard", "Executing sequential synchronized data fetch (interval=%d min)...", m_config.refreshIntervalMin);

                // 1. Fetch Weather first
                if (m_config.showWeather && m_isActive) {
                    fetchWeather();
                    vTaskDelay(pdMS_TO_TICKS(500)); // Yield to let Core 0 network memory settle
                }

                // 2. Fetch Market items strictly one by one
                if (m_config.showMarkets && m_isActive) {
                    fetchMarkets();
                    vTaskDelay(pdMS_TO_TICKS(300));
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

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    m_fetchTaskHandle = nullptr;
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

    // Serialize the whole weather fetch (geocode + forecast, both TLS) against every other TLS
    // user in the system. See BinanceProvider.cpp / HardwareHAL::begin() for why: mbedTLS's
    // ~32KB combined record buffers must stay in internal DRAM only on this board (PSRAM would
    // corrupt the HUB75 display), so overlapping handshakes compound peak internal DRAM demand.
    NetworkBudget::ScopedTlsHandshakeLock tlsLock;
    if (!tlsLock) {
        if (tlsLock.isDeniedByBudget()) {
            LOGW("Dashboard", "Skipping weather fetch: internal DRAM budget denied TLS admission.");
        } else {
            LOGW("Dashboard", "Skipping weather fetch: another TLS handshake is in progress.");
        }
        return;
    }

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

    if (!city.equalsIgnoreCase("Paris") && NetworkBudget::canStartTlsSession()) {
        WiFiClientSecure geoClient;
        geoClient.setInsecure();
        HTTPClient geoHttp;
        geoHttp.setTimeout(3000);
        String encCity = city;
        encCity.trim();
        encCity.replace(" ", "%20");
        String geoUrl = "https://geocoding-api.open-meteo.com/v1/search?name=" + encCity + "&count=1&language=" + lang + "&format=json";
        if (geoHttp.begin(geoClient, geoUrl)) {
            int code = geoHttp.GET();
            if (code == 200) {
                DynamicJsonDocument geoDoc(2048);
                if (deserializeJson(geoDoc, geoHttp.getStream()) == DeserializationError::Ok) {
                    if (geoDoc["results"].is<JsonArray>() && geoDoc["results"].size() > 0) {
                        lat = geoDoc["results"][0]["latitude"].as<float>();
                        lon = geoDoc["results"][0]["longitude"].as<float>();
                    }
                }
            }
            geoHttp.end();
            geoClient.stop();
        }
    }

    if (!NetworkBudget::canStartTlsSession()) {
        LOGW("Dashboard", "Skipping weather fetch: internal heap too low (free=%u, largest=%u).",
             (unsigned)NetworkBudget::freeInternal(),
             (unsigned)NetworkBudget::largestInternalBlock());
        return;
    }

    WiFiClientSecure metClient;
    metClient.setInsecure();
    HTTPClient metHttp;
    metHttp.setTimeout(3000);
    String metUrl = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) + "&current=temperature_2m,weather_code";
    if (metHttp.begin(metClient, metUrl)) {
        int code = metHttp.GET();
        if (code == 200) {
            DynamicJsonDocument metDoc(2048);
            if (deserializeJson(metDoc, metHttp.getStream()) == DeserializationError::Ok) {
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
        metHttp.end();
        metClient.stop();
    }
}

bool DashboardDataProvider::loadIconFromSd(const String& path, uint16_t outPixels[64]) {
    if (!outPixels) return false;

    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (!guard) {
        LOGW("Dashboard", "Could not acquire SD lock to read icon: %s (timeout)", path.c_str());
        return false;
    }

    FsFile f = sd.open(path, FILE_OPEN_READ);
    if (!f) {
        return false;
    }

    size_t size = f.size();
    if (size == 0 || size > 16384) {
        f.close();
        return false;
    }

    uint8_t* buf = (uint8_t*)malloc(size);
    if (!buf) {
        f.close();
        return false;
    }

    size_t bytesRead = f.read(buf, size);
    f.close();
    guard.unlock(); // Release SD card lock before PNG decoding!

    if (bytesRead != size) {
        free(buf);
        return false;
    }

    bool ok = decodePngTo8x8(buf, size, outPixels);
    free(buf);
    return ok;
}

bool DashboardDataProvider::downloadIconViaProxy(const String& targetUrl, const String& destPath) {
    if (WiFi.status() != WL_CONNECTED || targetUrl.isEmpty()) return false;

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(4000);
    http.setUserAgent("Mozilla/5.0 ArcadeMatrix/3.1");

    // Try fast HTTP via wsrv.nl proxy (resizes directly to 16x16 PNG)
    String proxyUrl = "http://wsrv.nl/?url=" + targetUrl + "&w=16&h=16&output=png";
    bool success = false;
    if (http.begin(client, proxyUrl)) {
        int code = http.GET();
        if (code == 200) {
            int len = http.getSize();
            if (len > 0 && len < 12000) {
                std::unique_ptr<uint8_t[]> iconData(new (std::nothrow) uint8_t[len]);
                if (iconData) {
                    WiFiClient* stream = http.getStreamPtr();
                    size_t totalRead = 0;
                    uint32_t startMs = millis();
                    while (totalRead < (size_t)len && (millis() - startMs < 3000)) {
                        size_t avail = stream->available();
                        if (avail > 0) {
                            size_t toRead = std::min(avail, (size_t)len - totalRead);
                            int bytesRead = stream->read(iconData.get() + totalRead, toRead);
                            if (bytesRead > 0) totalRead += bytesRead;
                        } else {
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                    }
                    if (totalRead == (size_t)len) {
                        SdLockGuard guard(pdMS_TO_TICKS(2000));
                        if (guard) {
                            int lastSlash = destPath.lastIndexOf('/');
                            if (lastSlash > 0) {
                                String dir = destPath.substring(0, lastSlash);
                                if (!sd.exists(dir)) sd.mkdir(dir);
                            }
                            FsFile f = sd.open(destPath, FILE_OPEN_WRITE);
                            if (f) {
                                f.write(iconData.get(), totalRead);
                                f.close();
                                success = true;
                                LOGI("Dashboard", "Saved icon to SD: %s (%u bytes)", destPath.c_str(), (unsigned)totalRead);
                            }
                        } else {
                            LOGW("Dashboard", "Could not acquire SD lock to save icon: %s (timeout)", destPath.c_str());
                        }
                    }
                }
            }
        }
        http.end();
        client.stop();
    }

    // If HTTP failed (e.g. proxy redirected or blocked), try https://wsrv.nl fallback
    if (!success && NetworkBudget::canStartTlsSession()) {
        NetworkBudget::ScopedTlsHandshakeLock tlsLock;
        if (!tlsLock) {
            if (tlsLock.isDeniedByBudget()) {
                LOGW("Dashboard", "Skipping HTTPS icon fallback: internal DRAM budget denied TLS admission.");
            } else {
                LOGW("Dashboard", "Skipping HTTPS icon fallback: another TLS handshake is in progress.");
            }
            return false;
        }
        WiFiClientSecure secureClient;
        secureClient.setInsecure();
        String secureProxyUrl = "https://wsrv.nl/?url=" + targetUrl + "&w=16&h=16&output=png";
        HTTPClient secureHttp;
        secureHttp.setTimeout(4000);
        secureHttp.setUserAgent("Mozilla/5.0 ArcadeMatrix/3.1");
        if (secureHttp.begin(secureClient, secureProxyUrl)) {
            int code = secureHttp.GET();
            if (code == 200) {
                int len = secureHttp.getSize();
                if (len > 0 && len < 12000) {
                    std::unique_ptr<uint8_t[]> iconData(new (std::nothrow) uint8_t[len]);
                    if (iconData) {
                        WiFiClientSecure* stream = static_cast<WiFiClientSecure*>(secureHttp.getStreamPtr());
                        size_t totalRead = 0;
                        uint32_t startMs = millis();
                        while (totalRead < (size_t)len && (millis() - startMs < 3000)) {
                            size_t avail = stream->available();
                            if (avail > 0) {
                                size_t toRead = std::min(avail, (size_t)len - totalRead);
                                int bytesRead = stream->read(iconData.get() + totalRead, toRead);
                                if (bytesRead > 0) totalRead += bytesRead;
                            } else {
                                vTaskDelay(pdMS_TO_TICKS(10));
                            }
                        }
                        if (totalRead == (size_t)len) {
                            SdLockGuard guard(pdMS_TO_TICKS(2000));
                            if (guard) {
                                int lastSlash = destPath.lastIndexOf('/');
                                if (lastSlash > 0) {
                                    String dir = destPath.substring(0, lastSlash);
                                    if (!sd.exists(dir)) sd.mkdir(dir);
                                }
                                FsFile f = sd.open(destPath, FILE_OPEN_WRITE);
                                if (f) {
                                    f.write(iconData.get(), totalRead);
                                    f.close();
                                    success = true;
                                    LOGI("Dashboard", "Saved HTTPS icon to SD: %s (%u bytes)", destPath.c_str(), (unsigned)totalRead);
                                }
                            } else {
                                LOGW("Dashboard", "Could not acquire SD lock to save HTTPS icon: %s (timeout)", destPath.c_str());
                            }
                        }
                    }
                }
            }
            secureHttp.end();
            secureClient.stop();
        }
    }

    return success;
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
        uint16_t pixels[64];
        String symUpper = sym;
        symUpper.toUpperCase();
        String symLower = symUpper;
        symLower.toLowerCase();

        auto it = m_iconCache.find(symUpper);
        if (it != m_iconCache.end() && (it->second.valid || it->second.notFound)) {
            continue;
        }

        String paths[] = {
            "/crypto_icons/" + symLower + ".png",
            "/stock_icons/" + symLower + ".png",
            "/crypto_icons/" + symUpper + ".png",
            "/stock_icons/" + symUpper + ".png"
        };

        for (const auto& p : paths) {
            if (loadIconFromSd(p, pixels)) {
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
                break;
            }
        }
    }
    if (anyUpdated) {
        LOGI("Dashboard", "Preloaded market icons from SD card on Core 0.");
    }
}

bool DashboardDataProvider::resolveMarketIcon(const String& symbol, const String& yahooImgUrl, uint16_t outPixels[64]) {
    String symUpper = symbol;
    symUpper.toUpperCase();
    symUpper.trim();
    String symLower = symUpper;
    symLower.toLowerCase();

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

    // 1. Check SD card candidate paths:
    String paths[] = {
        "/crypto_icons/" + symLower + ".png",
        "/stock_icons/" + symLower + ".png",
        "/crypto_icons/" + symUpper + ".png",
        "/stock_icons/" + symUpper + ".png"
    };

    for (const auto& path : paths) {
        if (loadIconFromSd(path, outPixels)) {
            CachedIcon entry;
            entry.valid = true;
            entry.notFound = false;
            entry.lastAttemptMs = millis();
            memcpy(entry.pixels, outPixels, 64 * sizeof(uint16_t));
            m_iconCache[symUpper] = entry;
            LOGI("Dashboard", "Loaded icon for %s from SD: %s", symUpper.c_str(), path.c_str());
            return true;
        }
    }

    // 2. If not found on SD, attempt download via proxy
    if (WiFi.status() == WL_CONNECTED) {
        std::vector<std::pair<String, String>> candidates;

        // Crypto candidates
        candidates.push_back({"https://assets.coincap.io/assets/icons/" + symLower + "%402x.png", "/crypto_icons/" + symLower + ".png"});
        candidates.push_back({"https://coinicons-api.vercel.app/api/icon/" + symLower, "/crypto_icons/" + symLower + ".png"});

        // Stock candidates
        candidates.push_back({"https://financialmodelingprep.com/image-stock/" + symUpper + ".png", "/stock_icons/" + symLower + ".png"});
        candidates.push_back({"https://eodhd.com/img/logos/US/" + symLower + ".png", "/stock_icons/" + symLower + ".png"});
        if (yahooImgUrl.length() > 0) {
            candidates.push_back({yahooImgUrl, "/stock_icons/" + symLower + ".png"});
        }

        for (const auto& cand : candidates) {
            const String& imgUrl = cand.first;
            const String& destPath = cand.second;

            if (downloadIconViaProxy(imgUrl, destPath)) {
                if (loadIconFromSd(destPath, outPixels)) {
                    CachedIcon entry;
                    entry.valid = true;
                    entry.notFound = false;
                    entry.lastAttemptMs = millis();
                    memcpy(entry.pixels, outPixels, 64 * sizeof(uint16_t));
                    m_iconCache[symUpper] = entry;
                    LOGI("Dashboard", "Downloaded & cached icon for %s via proxy -> %s", symUpper.c_str(), destPath.c_str());
                    return true;
                }
            }
        }
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

    YahooFinanceProvider yahooProvider;

    for (size_t s = 0; s < symbols.size() && s < 8; ++s) {
        const auto& sym = symbols[s];
        if (!m_isActive) break;

        // Abandon the whole round as soon as internal DRAM can no longer sustain a
        // TLS handshake. Without this, every remaining symbol issued a handshake
        // that was guaranteed to fail with MBEDTLS_ERR_SSL_ALLOC_FAILED, fragmenting
        // the heap further and starving the SD/FATFS layer on the same core.
        if (!NetworkBudget::canStartTlsSession()) {
            LOGW("Dashboard", "Aborting market fetch: internal heap too low (free=%u, largest=%u). Retrying next cycle.",
                 (unsigned)NetworkBudget::freeInternal(),
                 (unsigned)NetworkBudget::largestInternalBlock());
            break;
        }

        bool fetchSuccess = false;
        float fetchedPrice = 0.0f;
        float fetchedChange = 0.0f;
        String fetchedImgUrl = "";

        // 1. Check Binance (fast crypto API)
        {
            NetworkBudget::ScopedTlsHandshakeLock tlsLock;
            if (!tlsLock) {
                if (tlsLock.isDeniedByBudget()) {
                    LOGW("Dashboard", "Skipping Binance quote for %s: internal DRAM budget denied TLS admission.", sym.c_str());
                } else {
                    LOGW("Dashboard", "Skipping Binance quote for %s: another TLS handshake is in progress.", sym.c_str());
                }
            } else {
                WiFiClientSecure binanceClient;
                binanceClient.setInsecure();
                HTTPClient http;
                http.setTimeout(2500);
                String url = "https://api.binance.com/api/v3/ticker/24hr?symbol=" + sym + "USDT";

                if (http.begin(binanceClient, url)) {
                    int code = http.GET();
                    if (code == 200) {
                        DynamicJsonDocument doc(1024);
                        if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) {
                            fetchedPrice = doc["lastPrice"].as<float>();
                            fetchedChange = doc["priceChangePercent"].as<float>();
                            fetchSuccess = true;
                        }
                    }
                    http.end();
                }
                binanceClient.stop();
            }
        }

        // 2. Check Yahoo Finance (Stocks & other Cryptos)
        if (!fetchSuccess && m_isActive) {
            if (yahooProvider.fetchQuote(sym, fetchedPrice, fetchedChange, fetchedImgUrl)) {
                fetchSuccess = true;
            } else if (m_isActive) {
                // Cooling-off pause before fallback query: allow mbedTLS and lwIP socket memory to coalesce
                vTaskDelay(pdMS_TO_TICKS(50));
                if (yahooProvider.fetchQuote(sym + "-USD", fetchedPrice, fetchedChange, fetchedImgUrl)) {
                    fetchSuccess = true;
                }
            }
        }

        // 3. Patch quote in-place into double-buffered network payload matching exact configured symbol slot
        if (fetchSuccess) {
            uint16_t iconPixels[64];
            bool hasIcon = resolveMarketIcon(sym, fetchedImgUrl, iconPixels);
            uint8_t pubIdx = m_netPublishedIdx.load(std::memory_order_relaxed);
            uint8_t writeIdx = 1 - pubIdx;
            m_netBuffers[writeIdx] = m_netBuffers[pubIdx];

            if (s >= m_netBuffers[writeIdx].marketCount) {
                m_netBuffers[writeIdx].marketCount = (uint8_t)(s + 1);
            }
            auto& item = m_netBuffers[writeIdx].marketItems[s];
            item.symbol = sym;
            item.price = fetchedPrice;
            item.change24h = fetchedChange;
            item.valid = true;
            if (hasIcon) {
                item.hasIcon = true;
                memcpy(item.iconPixels, iconPixels, sizeof(iconPixels));
            }

            m_netPublishedIdx.store(writeIdx, std::memory_order_release);
            m_netHasNewData.store(true, std::memory_order_release);
            LOGD("Dashboard", "Market ticker [%u] %s updated: price=%.2f, change=%.2f%%", (unsigned)s, sym.c_str(), fetchedPrice, fetchedChange);
        } else {
            LOGW("Dashboard", "Failed to update market ticker %s; keeping previous cached quote.", sym.c_str());
        }

        vTaskDelay(pdMS_TO_TICKS(150));
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
