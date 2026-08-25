#include "WeatherEngine.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"
#include <WiFi.h>
#include <esp_heap_caps.h>





#include "../api/OpenWeatherMapProvider.h"

WeatherEngine::WeatherEngine() : matrix(nullptr) {
    validData = false;
    lastFetchTime = 0;
    numForecasts = 0;
    activeSlide = 0;
    lastSlideChange = 0;
}

WeatherEngine::~WeatherEngine() {
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
        for (const auto& inst : config.instances) {
            if (inst.engine_id == "weather") {
                onConfigChanged(&inst.config);
                break;
            }
        }
    }
    
    return EngineError::OK;
}

void WeatherEngine::activate() {
    if (config_api_key.isEmpty() || config_city.isEmpty()) {
        extern ConfigLoader config;
        for (const auto& inst : config.instances) {
            if (inst.engine_id == "weather") {
                onConfigChanged(&inst.config);
                break;
            }
        }
    }
}

void WeatherEngine::update(EngineContext* context) {
    loop();
}

void WeatherEngine::render(EngineContext* context) {}

void WeatherEngine::deactivate() {}

void WeatherEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;
    String newKey = engineConfig->getString("api_key", "");
    String newCity = engineConfig->getString("city", "");
    String newLang = engineConfig->getString("lang", "");
    if (newLang.isEmpty()) {
        extern ConfigLoader config;
        newLang = config.system.lang.length() > 0 ? config.system.lang : "fr";
    }
    String newUnits = engineConfig->getString("units", "metric");
    
    if (newKey != config_api_key || newCity != config_city || newLang != config_lang || newUnits != config_units) {
        config_api_key = newKey;
        config_city = newCity;
        config_lang = newLang;
        config_units = newUnits;
        validData = false;
        forceUpdate(); // Force fetch immediately with new settings
    }
    config_offset_x = engineConfig->getInt("weather_offset_x", 0);
    config_offset_y = engineConfig->getInt("weather_offset_y", 0);
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

void WeatherEngine::updateWeather(const String& apiKey, const String& city, const String& units) {
    if (apiKey.isEmpty() || city.isEmpty()) {
        static unsigned long lastWarn = 0;
        if (millis() - lastWarn > 10000) {
            LOGW("WeatherEngine", "Cannot fetch weather: API Key ('%s') or City ('%s') is missing!", apiKey.c_str(), city.c_str());
            lastWarn = millis();
        }
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastWarnWifi = 0;
        if (millis() - lastWarnWifi > 10000) {
            LOGW("WeatherEngine", "Cannot fetch weather: Wi-Fi not connected!");
            lastWarnWifi = millis();
        }
        return;
    }
    
    // Only update every 15 minutes on success, or retry every 30 seconds on failure.
    uint32_t interval = validData ? 900000 : 30000;
    if (lastFetchTime > 0 && millis() - lastFetchTime < interval) return;

    // Set lastFetchTime immediately so we don't spam the API on failure
    lastFetchTime = millis();

    String reqLang = config_lang;
    if (reqLang.length() == 0) reqLang = "fr";
    
    bool fetched = false;
    for (IWeatherProvider* provider : providers) {
        if (provider->fetchForecast(apiKey, city, reqLang, units, forecasts, MAX_FORECAST_DAYS, numForecasts)) {
            fetched = true;
            break;
        }
    }
    
    if (fetched && numForecasts > 0) {
        validData = true;
        activeSlide = 0;
        lastSlideChange = millis();
        LOGI("WeatherEngine", "Success! Parsed %d forecast days in %s units.", numForecasts, units.c_str());
    } else {
        LOGE("WeatherEngine", "Error: Failed to parse weather data or 0 forecast entries parsed.");
    }
}

void WeatherEngine::drawIcon(const String& icon, int x, int y) {
    // 24x24 pixel area for icons
    if (icon.indexOf("01") != -1) { // Sun
        matrix->fillCircle(x + 12, y + 12, 6, matrix->color565(255, 255, 0));
        matrix->drawLine(x + 12, y + 2, x + 12, y + 4, matrix->color565(255, 200, 0));
        matrix->drawLine(x + 12, y + 20, x + 12, y + 22, matrix->color565(255, 200, 0));
        matrix->drawLine(x + 2, y + 12, x + 4, y + 12, matrix->color565(255, 200, 0));
        matrix->drawLine(x + 20, y + 12, x + 22, y + 12, matrix->color565(255, 200, 0));
        matrix->drawLine(x + 5, y + 5, x + 7, y + 7, matrix->color565(255, 200, 0));
        matrix->drawLine(x + 19, y + 19, x + 17, y + 17, matrix->color565(255, 200, 0));
        matrix->drawLine(x + 19, y + 5, x + 17, y + 7, matrix->color565(255, 200, 0));
        matrix->drawLine(x + 5, y + 19, x + 7, y + 17, matrix->color565(255, 200, 0));
    } else if (icon.indexOf("02") != -1 || icon.indexOf("03") != -1 || icon.indexOf("04") != -1) { // Clouds
        if (icon.indexOf("02") != -1) { // Sun behind cloud
            matrix->fillCircle(x + 8, y + 8, 4, matrix->color565(255, 255, 0));
        }
        matrix->fillCircle(x + 8, y + 14, 5, matrix->color565(200, 200, 200));
        matrix->fillCircle(x + 14, y + 11, 6, matrix->color565(255, 255, 255));
        matrix->fillCircle(x + 20, y + 14, 5, matrix->color565(200, 200, 200));
        matrix->fillRect(x + 8, y + 14, 12, 6, matrix->color565(200, 200, 200));
    } else if (icon.indexOf("09") != -1 || icon.indexOf("10") != -1) { // Rain
        matrix->fillCircle(x + 8, y + 10, 5, matrix->color565(150, 150, 150));
        matrix->fillCircle(x + 14, y + 8, 6, matrix->color565(200, 200, 200));
        matrix->fillCircle(x + 20, y + 10, 5, matrix->color565(150, 150, 150));
        matrix->fillRect(x + 8, y + 10, 12, 6, matrix->color565(150, 150, 150));
        matrix->drawLine(x + 8, y + 18, x + 6, y + 22, matrix->color565(0, 150, 255));
        matrix->drawLine(x + 14, y + 18, x + 12, y + 22, matrix->color565(0, 150, 255));
        matrix->drawLine(x + 20, y + 18, x + 18, y + 22, matrix->color565(0, 150, 255));
    } else if (icon.indexOf("11") != -1) { // Thunder
        matrix->fillCircle(x + 8, y + 10, 5, matrix->color565(100, 100, 100));
        matrix->fillCircle(x + 14, y + 8, 6, matrix->color565(150, 150, 150));
        matrix->fillCircle(x + 20, y + 10, 5, matrix->color565(100, 100, 100));
        matrix->fillRect(x + 8, y + 10, 12, 6, matrix->color565(100, 100, 100));
        matrix->drawLine(x + 14, y + 16, x + 10, y + 20, matrix->color565(255, 255, 0));
        matrix->drawLine(x + 10, y + 20, x + 16, y + 20, matrix->color565(255, 255, 0));
        matrix->drawLine(x + 16, y + 20, x + 12, y + 24, matrix->color565(255, 255, 0));
    } else if (icon.indexOf("13") != -1) { // Snow
        matrix->fillCircle(x + 14, y + 14, 2, matrix->color565(255, 255, 255));
        matrix->drawLine(x + 14, y + 8, x + 14, y + 20, matrix->color565(255, 255, 255));
        matrix->drawLine(x + 8, y + 14, x + 20, y + 14, matrix->color565(255, 255, 255));
        matrix->drawLine(x + 10, y + 10, x + 18, y + 18, matrix->color565(255, 255, 255));
        matrix->drawLine(x + 18, y + 10, x + 10, y + 18, matrix->color565(255, 255, 255));
    } else { // Unknown
        matrix->fillCircle(x + 12, y + 12, 6, matrix->color565(0, 255, 0)); // Green dot
    }
}

bool WeatherEngine::loop() {
    updateWeather(config_api_key, config_city, config_units);

    if (!validData || numForecasts == 0) return true;

    // Cycle through Today/Tomorrow/Day3 every slideDurationMs. Simplified vs. the RPi's eased
    // horizontal-scroll transition (see WeatherEngine.h for rationale).
    if (numForecasts > 1 && millis() - lastSlideChange >= slideDurationMs) {
        activeSlide = (activeSlide + 1) % numForecasts;
        lastSlideChange = millis();
    }

    drawForecast(forecasts[activeSlide]);
    return true;
}

void WeatherEngine::drawForecast(const WeatherData& data) {
    // Reset font to default GLCD font to avoid drawing from baseline
    matrix->setFont(nullptr);
    
    char unitChar = (config_units.equalsIgnoreCase("imperial") || config_units.equalsIgnoreCase("fahrenheit") || config_units.equalsIgnoreCase("f")) ? 'F' : 'C';
    char tempMinStr[16];
    char tempMaxStr[16];
    sprintf(tempMinStr, "%.0f%c", data.temp_min, unitChar);
    sprintf(tempMaxStr, "%.0f%c", data.temp_max, unitChar);
    
    int mw = matrix->width();
    int mh = matrix->height();

    uint16_t colorMorning = matrix->color565(120, 200, 255); // Soft Cyan (Morning / Min)
    uint16_t colorAfternoon = matrix->color565(255, 150, 50); // Warm Orange (Afternoon / Max)
    uint16_t colorLabel = matrix->color565(180, 180, 255);    // Lavender
    uint16_t colorDesc = matrix->color565(210, 210, 210);     // Light silver

    if (mw >= 256 && mh >= 64) {
        // --- 256x64 Ultra-Widescreen HD Layout ---
        int iconX = 20 + config_offset_x;
        int iconY = (mh - 24) / 2 + config_offset_y;
        drawIcon(data.iconCode, iconX, iconY);

        int tempX = iconX + 36;
        int textW = max((int)strlen(tempMinStr), (int)strlen(tempMaxStr)) * 12;

        // Matin (Haut) - Size 2
        matrix->setTextSize(2);
        matrix->setTextColor(shadowColor);
        matrix->setCursor(tempX + 1, 10 + 1 + config_offset_y);
        matrix->print(tempMinStr);
        matrix->setTextColor(colorMorning);
        matrix->setCursor(tempX, 10 + config_offset_y);
        matrix->print(tempMinStr);

        // Après-midi (Bas) - Size 2
        matrix->setTextColor(shadowColor);
        matrix->setCursor(tempX + 1, 38 + 1 + config_offset_y);
        matrix->print(tempMaxStr);
        matrix->setTextColor(colorAfternoon);
        matrix->setCursor(tempX, 38 + config_offset_y);
        matrix->print(tempMaxStr);

        // Right Column: Label & Condition
        int rightX = tempX + textW + 18;
        matrix->setTextSize(2);
        matrix->setTextColor(colorLabel);
        matrix->setCursor(rightX, 10 + config_offset_y);
        matrix->print(data.label);

        if (data.description.length() > 0) {
            matrix->setTextColor(colorDesc);
            matrix->setCursor(rightX, 38 + config_offset_y);
            matrix->print(data.description);
        }
    } else if (mw >= 128 && mh <= 32) {
        // --- 128x32 Widescreen Layout ---
        int iconX = 4 + config_offset_x;
        int iconY = (mh - 24) / 2 + config_offset_y;
        drawIcon(data.iconCode, iconX, iconY);

        int tempX = iconX + 28;
        int maxLen = max((int)strlen(tempMinStr), (int)strlen(tempMaxStr));
        int textW = maxLen * 6;

        matrix->setTextSize(1);
        // Matin (Haut)
        matrix->setTextColor(shadowColor);
        matrix->setCursor(tempX + 1, 4 + 1 + config_offset_y);
        matrix->print(tempMinStr);
        matrix->setTextColor(colorMorning);
        matrix->setCursor(tempX, 4 + config_offset_y);
        matrix->print(tempMinStr);

        // Après-midi (Bas)
        matrix->setTextColor(shadowColor);
        matrix->setCursor(tempX + 1, 18 + 1 + config_offset_y);
        matrix->print(tempMaxStr);
        matrix->setTextColor(colorAfternoon);
        matrix->setCursor(tempX, 18 + config_offset_y);
        matrix->print(tempMaxStr);

        // Right Column: Label on top, Condition on bottom
        int rightX = tempX + textW + 8;
        if (rightX < 78 + config_offset_x) rightX = 78 + config_offset_x;
        if (rightX > mw - 40) rightX = mw - 40;

        // Label
        matrix->setTextColor(colorLabel);
        matrix->setCursor(rightX, 4 + config_offset_y);
        matrix->print(data.label);

        // Condition
        if (data.description.length() > 0) {
            matrix->setTextColor(colorDesc);
            matrix->setCursor(rightX, 18 + config_offset_y);
            matrix->print(data.description);
        }
    } else if (mh >= 64 && mw >= 128) {
        // --- 128x64 Layout ---
        int iconX = 6 + config_offset_x;
        int iconY = (mh - 24) / 2 + config_offset_y;
        drawIcon(data.iconCode, iconX, iconY);

        int tempX = iconX + 30;
        int textW = max((int)strlen(tempMinStr), (int)strlen(tempMaxStr)) * 12;

        // Matin (Haut) - Size 2
        matrix->setTextSize(2);
        matrix->setTextColor(shadowColor);
        matrix->setCursor(tempX + 1, 12 + 1 + config_offset_y);
        matrix->print(tempMinStr);
        matrix->setTextColor(colorMorning);
        matrix->setCursor(tempX, 12 + config_offset_y);
        matrix->print(tempMinStr);

        // Après-midi (Bas) - Size 2
        matrix->setTextColor(shadowColor);
        matrix->setCursor(tempX + 1, 38 + 1 + config_offset_y);
        matrix->print(tempMaxStr);
        matrix->setTextColor(colorAfternoon);
        matrix->setCursor(tempX, 38 + config_offset_y);
        matrix->print(tempMaxStr);

        // Right Column
        int rightX = tempX + textW + 10;
        if (rightX < 82 + config_offset_x) rightX = 82 + config_offset_x;

        matrix->setTextSize(1);
        matrix->setTextColor(colorLabel);
        matrix->setCursor(rightX, 14 + config_offset_y);
        matrix->print(data.label);

        if (data.description.length() > 0) {
            matrix->setTextColor(colorDesc);
            matrix->setCursor(rightX, 38 + config_offset_y);
            matrix->print(data.description);
        }
    } else {
        // --- 64x64 or Compact Vertical Layout ---
        int iconX = (mw - 24) / 2 + config_offset_x;
        int iconY = 16 + config_offset_y;
        drawIcon(data.iconCode, iconX, iconY);

        matrix->setTextSize(1);
        int labelW = data.label.length() * 6;
        matrix->setTextColor(colorLabel);
        matrix->setCursor((mw - labelW) / 2 + config_offset_x, 4 + config_offset_y);
        matrix->print(data.label);

        // Morning on left/top, Afternoon on right/bottom
        int minW = strlen(tempMinStr) * 6;
        int maxW = strlen(tempMaxStr) * 6;
        
        matrix->setTextColor(colorMorning);
        matrix->setCursor((mw - minW) / 2 + config_offset_x, 44 + config_offset_y);
        matrix->print(tempMinStr);

        matrix->setTextColor(colorAfternoon);
        matrix->setCursor((mw - maxW) / 2 + config_offset_x, 54 + config_offset_y);
        matrix->print(tempMaxStr);
    }
}

EngineDescriptor WeatherEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_weather;
    desc_weather.metadata = {"weather", "Weather", "info", FIRMWARE_VERSION};
    desc_weather.capabilities.realtime = false;
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
    return desc_weather;
}

