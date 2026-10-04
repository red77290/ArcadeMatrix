#include "WeatherEngine.h"
#include "renderers/WeatherLayout.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"
#include <WiFi.h>
#include <esp_heap_caps.h>





#include "../api/OpenWeatherMapProvider.h"

WeatherEngine::WeatherEngine() : matrix(nullptr) {
    activeSlide = 0;
    lastSlideChange = 0;
}

WeatherEngine::~WeatherEngine() {
    if (m_fetchTask) {          // stop the fetch before the providers it uses are freed
        // Ask it to finish the round trip it is in and leave the loop itself. Deleting the task
        // outright would strand its TLS socket and heap, and could cut it off mid-read of the
        // providers deleted just below.
        m_stopFetch.store(true, std::memory_order_release);
        for (int i = 0; i < 500 && !m_fetchExited.load(std::memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(10));   // up to 5 s, which covers an HTTPS timeout
        }
        if (!m_fetchExited.load(std::memory_order_acquire)) {
            LOGW("WeatherEngine", "fetch task did not stop in time; deleting it");
            vTaskDelete(m_fetchTask);
        }
        m_fetchTask = nullptr;
    }
    for (auto* provider : providers) {
        delete provider;
    }
}

EngineError WeatherEngine::initialize(EngineContext* context, const EngineConfig* config) {
    matrix = context->getMatrix();
    textColor = matrix->color565(255, 255, 255);
    shadowColor = matrix->color565(0, 0, 0);
    
    // Add default provider
    addProvider(new OpenWeatherMapProvider());

    if (config) {
        onConfigChanged(config);
    } else {
        // Fallback: query active instance from global config
        extern ConfigLoader config;
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        for (const auto& inst : guard->instances) {
            if (inst.engine_id == "weather") {
                onConfigChanged(&inst.config);
                break;
            }
        }
    }
    
    return EngineError::OK;
}

void WeatherEngine::activate() {
    requestRedraw();
    startFetchTask();   // so the first forecast is already on its way before the slot comes round
    if (config_api_key.isEmpty() || config_city.isEmpty()) {
        extern ConfigLoader config;
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        for (const auto& inst : guard->instances) {
            if (inst.engine_id == "weather") {
                onConfigChanged(&inst.config);
                break;
            }
        }
    }
}

void WeatherEngine::update(EngineContext* context) {
    m_presented = loop();
}

void WeatherEngine::render(EngineContext* context) {}

void WeatherEngine::deactivate() {}

void WeatherEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;

    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    String sysLang = guard->system.lang.length() > 0 ? guard->system.lang : "en";
    String sysUnits = guard->system.unit.equalsIgnoreCase("F") ? "imperial" : "metric";

    String newKey = engineConfig->getString("api_key", "");
    String newCity = engineConfig->getString("city", "");
    String newLang = engineConfig->getString("lang", "system");
    if (newLang.isEmpty() || newLang.equalsIgnoreCase("system")) newLang = sysLang;
    String newUnits = engineConfig->getString("units", "system");
    if (newUnits.isEmpty() || newUnits.equalsIgnoreCase("system")) newUnits = sysUnits;
    
    if (newKey != config_api_key || newCity != config_city || newLang != config_lang || newUnits != config_units) {
        config_api_key = newKey;
        config_city = newCity;
        config_lang = newLang;
        config_units = newUnits;
        m_validData.store(false, std::memory_order_release);
        forceUpdate(); // Force fetch immediately with new settings
    }
    config_offset_x = engineConfig->getInt("weather_offset_x", 0);
    config_offset_y = engineConfig->getInt("weather_offset_y", 0);
    requestRedraw();
}

void WeatherEngine::addProvider(IWeatherProvider* provider) {
    if (provider) {
        providers.push_back(provider);
    }
}

void WeatherEngine::setCharacter(int characterId) {
    switch (characterId) {
        case 0: // CHAR_RYU
            textColor = matrix->color565(255, 255, 255); shadowColor = matrix->color565(200, 0, 0); break;
        case 1: // CHAR_MARIO
            textColor = matrix->color565(255, 0, 0); shadowColor = matrix->color565(0, 0, 200); break;
        case 2: // CHAR_MARCO
            textColor = matrix->color565(0, 255, 0); shadowColor = matrix->color565(200, 200, 0); break;
        case 3: // CHAR_MEGAMAN
            textColor = matrix->color565(0, 255, 255); shadowColor = matrix->color565(0, 0, 200); break;
        case 4: // CHAR_SPACE
            textColor = matrix->color565(0, 255, 0); shadowColor = matrix->color565(255, 255, 255); break;
        case 5: // CHAR_BUB
            textColor = matrix->color565(255, 255, 0); shadowColor = matrix->color565(0, 200, 0); break;
        default:
            textColor = matrix->color565(255, 255, 255); shadowColor = matrix->color565(0, 0, 0); break;
    }
}

namespace {
WeatherEngine::FetchState g_fetchState{ false, 0, 0, "not started" };
uint32_t g_lastAttemptMs = 0;
}

WeatherEngine::FetchState WeatherEngine::fetchState() {
    WeatherEngine::FetchState s = g_fetchState;
    s.lastAttemptAgeS = g_lastAttemptMs ? (millis() - g_lastAttemptMs) / 1000 : 0;
    return s;
}

void WeatherEngine::startFetchTask() {
    if (m_fetchTask) return;
    // Core 0 keeps the render loop on Core 1 free; 8 KB covers a TLS handshake and JSON parse.
    if (xTaskCreatePinnedToCore(fetchTaskEntry, "weather_fetch", 8192, this, 1, &m_fetchTask, 0) != pdPASS) {
        m_fetchTask = nullptr;
        LOGW("WeatherEngine", "fetch task did not start; falling back to no updates");
    }
}

void WeatherEngine::fetchTaskEntry(void* arg) {
    auto* self = static_cast<WeatherEngine*>(arg);
    while (!self->m_stopFetch.load(std::memory_order_acquire)) {
        self->fetchOnce();
        // Sleep in slices so a stop request is picked up promptly instead of after a full wait.
        for (int i = 0; i < 50 && !self->m_stopFetch.load(std::memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    self->m_fetchExited.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}

void WeatherEngine::fetchOnce() {
    updateWeather(config_api_key, config_city, config_units);
}

void WeatherEngine::updateWeather(const String& apiKey, const String& city, const String& units) {
    if (apiKey.isEmpty() || city.isEmpty()) {
        static unsigned long lastWarn = 0;
        if (millis() - lastWarn > 10000) {
            LOGW("WeatherEngine", "Cannot fetch weather: API Key ('%s') or City ('%s') is missing!", apiKey.c_str(), city.c_str());
            strlcpy(g_fetchState.lastError, "no api key or city", sizeof(g_fetchState.lastError));
            lastWarn = millis();
        }
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastWarnWifi = 0;
        if (millis() - lastWarnWifi > 10000) {
            LOGW("WeatherEngine", "Cannot fetch weather: Wi-Fi not connected!");
            strlcpy(g_fetchState.lastError, "wifi down", sizeof(g_fetchState.lastError));
            lastWarnWifi = millis();
        }
        return;
    }
    
    // Invalidate if system language changed
    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    String sysLang = guard->system.lang.length() > 0 ? guard->system.lang : "fr";
    if (sysLang != config_lang) {
        config_lang = sysLang;
        m_validData.store(false, std::memory_order_release);
        lastFetchTime.store(0, std::memory_order_relaxed);
    }

    // Only update every 15 minutes on success, or retry every 30 seconds on failure.
    uint32_t interval = m_validData.load(std::memory_order_acquire) ? 900000 : 30000;
    const uint32_t lastFetch = lastFetchTime.load(std::memory_order_relaxed);
    if (lastFetch > 0 && millis() - lastFetch < interval) return;

    // Set lastFetchTime immediately so we don't spam the API on failure
    const uint32_t now = millis();
    lastFetchTime.store(now, std::memory_order_relaxed);
    g_lastAttemptMs = now;
    strlcpy(g_fetchState.lastError, "fetching", sizeof(g_fetchState.lastError));

    String reqLang = config_lang;
    if (reqLang.length() == 0) reqLang = "fr";
    
    WeatherData fresh[MAX_FORECAST_DAYS];
    int freshCount = 0;
    bool fetched = false;
    for (IWeatherProvider* provider : providers) {
        if (provider->fetchForecast(apiKey, city, reqLang, units, fresh, MAX_FORECAST_DAYS, freshCount)) {
            fetched = true;
            break;
        }
    }

    if (fetched && freshCount > 0) {
        // Fill the buffer the render loop is not reading, then publish it with one release store.
        const uint8_t back = m_activeBuf.load(std::memory_order_relaxed) ^ 1;
        int n = 0;
        for (int i = 0; i < freshCount && i < MAX_FORECAST_DAYS; i++) m_forecastBuf[back][n++] = fresh[i];
        m_forecastCount[back] = (uint8_t)n;
        m_activeBuf.store(back, std::memory_order_release);
        m_validData.store(true, std::memory_order_release);
        m_newData.store(true, std::memory_order_release);
        g_fetchState.valid = true;
        g_fetchState.days = (uint8_t)freshCount;
        strlcpy(g_fetchState.lastError, "ok", sizeof(g_fetchState.lastError));
        LOGI("WeatherEngine", "Success! Parsed %d forecast days in %s units.", n, units.c_str());
    } else {
        LOGE("WeatherEngine", "Error: Failed to parse weather data or 0 forecast entries parsed.");
    }
}


bool WeatherEngine::loop() {
    // Nothing here touches the network: the fetch task owns that, and a redraw is requested when
    // fresh data lands. Weather therefore draws from cache the moment the rotation arrives.
    startFetchTask();
    // One acquire load pairs with the fetch task's release store: everything written into the
    // buffer before it was published is visible here, and the buffer cannot change under us.
    const uint8_t buf = m_activeBuf.load(std::memory_order_acquire);
    const int days = (int)m_forecastCount[buf];
    const bool haveData = m_validData.load(std::memory_order_acquire) && days > 0;

    if (m_newData.exchange(false, std::memory_order_acq_rel)) {
        activeSlide = 0;                 // a fresh forecast starts again at today
        lastSlideChange = millis();
        requestRedraw();
    }
    if (!haveData) requestRedraw();   // keep the notice alive until the first forecast lands

    // Cycle through Today/Tomorrow/Day3 every slideDurationMs. Simplified vs. the RPi's eased
    // horizontal-scroll transition (see WeatherEngine.h for rationale).
    if (haveData && days > 1 && millis() - lastSlideChange >= slideDurationMs) {
        activeSlide = (activeSlide + 1) % days;
        lastSlideChange = millis();
        requestRedraw();
    }

    if (m_redrawFrames == 0) return false;   // both DMA buffers already show this screen
    m_redrawFrames--;

    matrix->fillScreen(0);
    if (haveData) {
        drawForecast(m_forecastBuf[buf][activeSlide % days]);
    } else {
        // No forecast yet: say so rather than leaving the slot black for its whole duration, which
        // is what it looked like after every restart until the first fetch landed.
        matrix->setFont(nullptr);
        matrix->setTextSize((matrix->width() >= 128) ? 2 : 1);
        matrix->setTextColor(matrix->color565(120, 170, 255));
        int16_t bx, by; uint16_t bw, bh;
        const char* msg = "WEATHER...";
        matrix->getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);
        matrix->setCursor((matrix->width() - (int)bw) / 2 - bx, (matrix->height() - (int)bh) / 2 - by);
        matrix->print(msg);
    }
    return true;
}

void WeatherEngine::drawForecast(const WeatherData& data) {
    // Drawn by the shared weather page layout ("90°F", high above low in °F, icon | day + condition |
    // temperatures).
    weather_layout::Page page;
    const bool fahrenheit = config_units.equalsIgnoreCase("imperial") || config_units.equalsIgnoreCase("fahrenheit") ||
                            config_units.equalsIgnoreCase("f");
    page.icon = data.iconCode.c_str();
    strlcpy(page.label, data.label.c_str(), sizeof(page.label));
    strlcpy(page.labelLong, data.labelLong.c_str(), sizeof(page.labelLong));
    strlcpy(page.desc, data.description.c_str(), sizeof(page.desc));
    strlcpy(page.descLong, data.descriptionLong.c_str(), sizeof(page.descLong));
    weather_layout::setRange(page, data.temp_min, data.temp_max, fahrenheit);
    weather_layout::draw(matrix, page, config_offset_x, config_offset_y, shadowColor);
}

EngineDescriptor WeatherEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_weather;
    desc_weather.metadata = {"weather", "Weather", "info", FIRMWARE_VERSION};
    desc_weather.capabilities.realtime = false;
    desc_weather.requirements.needsAudio = false;
    desc_weather.requirements.needsNetwork = true;
    desc_weather.schema.fields = {
        ConfigField("api_key", ConfigType::STRING, "API Key", "OpenWeatherMap API Key", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("city", ConfigType::STRING, "City", "City (e.g. Paris,FR or for US: Tucson,AZ,US)", "Paris,FR", true, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("units", ConfigType::ENUM, "Units", "Temperature unit (°C or °F)", "system", false, "", "", "", "system:System (General),metric:Celsius (°C),imperial:Fahrenheit (°F)", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Language", "Weather description language", "system", false, "", "", "", "system:System (General),en:English,fr:Français,es:Español,de:Deutsch,it:Italiano", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("weather_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("weather_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_weather.factory = []() { return std::unique_ptr<IEngine>(new WeatherEngine()); };
    return desc_weather;
}

