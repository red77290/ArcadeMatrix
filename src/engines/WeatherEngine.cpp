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
    matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return EngineError::InitializationFailed;
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
        validData = false;
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
    
    // Invalidate if system language changed
    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    String sysLang = guard->system.lang.length() > 0 ? guard->system.lang : "fr";
    if (sysLang != config_lang) {
        config_lang = sysLang;
        validData = false;
        lastFetchTime = 0;
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
        requestRedraw();
        activeSlide = 0;
        lastSlideChange = millis();
        LOGI("WeatherEngine", "Success! Parsed %d forecast days in %s units.", numForecasts, units.c_str());
    } else {
        LOGE("WeatherEngine", "Error: Failed to parse weather data or 0 forecast entries parsed.");
    }
}

void WeatherEngine::drawIcon(const String& icon, int x, int y, int scale) {
    // 24x24 pixel design, scaled by an integer factor for larger panels.
    const int s = max(1, scale);
    auto X = [&](int v) { return x + v * s; };
    auto Y = [&](int v) { return y + v * s; };
    auto line = [&](int x0, int y0, int x1, int y1, uint16_t c) {
        // A scaled 1-px line becomes an s-px thick stroke: draw s parallel offsets.
        for (int o = 0; o < s; o++) matrix->drawLine(X(x0) + o, Y(y0), X(x1) + o, Y(y1), c);
    };
    if (icon.indexOf("01") != -1) { // Sun
        matrix->fillCircle(X(12), Y(12), 6 * s, matrix->color565(255, 255, 0));
        uint16_t ray = matrix->color565(255, 200, 0);
        line(12, 2, 12, 4, ray);
        line(12, 20, 12, 22, ray);
        line(2, 12, 4, 12, ray);
        line(20, 12, 22, 12, ray);
        line(5, 5, 7, 7, ray);
        line(19, 19, 17, 17, ray);
        line(19, 5, 17, 7, ray);
        line(5, 19, 7, 17, ray);
    } else if (icon.indexOf("02") != -1 || icon.indexOf("03") != -1 || icon.indexOf("04") != -1) { // Clouds
        if (icon.indexOf("02") != -1) { // Sun behind cloud
            matrix->fillCircle(X(8), Y(8), 4 * s, matrix->color565(255, 255, 0));
        }
        matrix->fillCircle(X(8), Y(14), 5 * s, matrix->color565(200, 200, 200));
        matrix->fillCircle(X(14), Y(11), 6 * s, matrix->color565(255, 255, 255));
        matrix->fillCircle(X(20), Y(14), 5 * s, matrix->color565(200, 200, 200));
        matrix->fillRect(X(8), Y(14), 12 * s, 6 * s, matrix->color565(200, 200, 200));
    } else if (icon.indexOf("09") != -1 || icon.indexOf("10") != -1) { // Rain
        matrix->fillCircle(X(8), Y(10), 5 * s, matrix->color565(150, 150, 150));
        matrix->fillCircle(X(14), Y(8), 6 * s, matrix->color565(200, 200, 200));
        matrix->fillCircle(X(20), Y(10), 5 * s, matrix->color565(150, 150, 150));
        matrix->fillRect(X(8), Y(10), 12 * s, 6 * s, matrix->color565(150, 150, 150));
        uint16_t drop = matrix->color565(0, 150, 255);
        line(8, 18, 6, 22, drop);
        line(14, 18, 12, 22, drop);
        line(20, 18, 18, 22, drop);
    } else if (icon.indexOf("11") != -1) { // Thunder
        matrix->fillCircle(X(8), Y(10), 5 * s, matrix->color565(100, 100, 100));
        matrix->fillCircle(X(14), Y(8), 6 * s, matrix->color565(150, 150, 150));
        matrix->fillCircle(X(20), Y(10), 5 * s, matrix->color565(100, 100, 100));
        matrix->fillRect(X(8), Y(10), 12 * s, 6 * s, matrix->color565(100, 100, 100));
        uint16_t bolt = matrix->color565(255, 255, 0);
        line(14, 16, 10, 20, bolt);
        line(10, 20, 16, 20, bolt);
        line(16, 20, 12, 24, bolt);
    } else if (icon.indexOf("13") != -1) { // Snow
        uint16_t white = matrix->color565(255, 255, 255);
        matrix->fillCircle(X(14), Y(14), 2 * s, white);
        line(14, 8, 14, 20, white);
        line(8, 14, 20, 14, white);
        line(10, 10, 18, 18, white);
        line(18, 10, 10, 18, white);
    } else { // Unknown
        matrix->fillCircle(X(12), Y(12), 6 * s, matrix->color565(0, 255, 0)); // Green dot
    }
}

bool WeatherEngine::loop() {
    bool hadData = validData;
    updateWeather(config_api_key, config_city, config_units);
    if (hadData != validData) requestRedraw();   // data appeared or was lost

    // Cycle through Today/Tomorrow/Day3 every slideDurationMs. Simplified vs. the RPi's eased
    // horizontal-scroll transition (see WeatherEngine.h for rationale).
    if (validData && numForecasts > 1 && millis() - lastSlideChange >= slideDurationMs) {
        activeSlide = (activeSlide + 1) % numForecasts;
        lastSlideChange = millis();
        requestRedraw();
    }

    if (m_redrawFrames == 0) return false;   // both DMA buffers already show this screen
    m_redrawFrames--;

    matrix->fillScreen(0);
    if (validData && numForecasts > 0) {
        drawForecast(forecasts[activeSlide % numForecasts]);
    }
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
        // --- 256x64 wide layout ---
        // Three columns that use the whole width: a 2x icon on the left, the unabbreviated day and
        // condition in the middle, and the temperatures right-aligned on the right so three digits
        // and a minus sign always fit. Long text falls back to the short label / size-1 condition.
        const int margin = 8;
        const int iconScale = 2;
        int iconX = margin + config_offset_x;
        int iconY = (mh - 24 * iconScale) / 2 + config_offset_y;
        drawIcon(data.iconCode, iconX, iconY, iconScale);

        const int rowTopY = 10 + config_offset_y;
        const int rowBottomY = 38 + config_offset_y;
        auto textW = [](const String& t, int size) { return t.length() > 0 ? (int)t.length() * 6 * size - size : 0; };

        // Right column: widest temperature decides the column, both right-aligned to the same edge.
        int rightEdge = mw - margin + config_offset_x;
        int tempW = max(textW(tempMinStr, 2), textW(tempMaxStr, 2));
        matrix->setTextSize(2);
        matrix->setTextColor(shadowColor);
        matrix->setCursor(rightEdge - textW(tempMinStr, 2) + 1, rowTopY + 1);
        matrix->print(tempMinStr);
        matrix->setTextColor(colorMorning);
        matrix->setCursor(rightEdge - textW(tempMinStr, 2), rowTopY);
        matrix->print(tempMinStr);
        matrix->setTextColor(shadowColor);
        matrix->setCursor(rightEdge - textW(tempMaxStr, 2) + 1, rowBottomY + 1);
        matrix->print(tempMaxStr);
        matrix->setTextColor(colorAfternoon);
        matrix->setCursor(rightEdge - textW(tempMaxStr, 2), rowBottomY);
        matrix->print(tempMaxStr);

        // Middle column: between the icon and the temperature column.
        int midX = iconX + 24 * iconScale + margin;
        int midW = (rightEdge - tempW - margin) - midX;

        String label = data.labelLong.length() > 0 ? data.labelLong : data.label;
        if (textW(label, 2) > midW) label = data.label;            // "AUJOURD'HUI" -> "AUJ."
        matrix->setTextSize(2);
        matrix->setTextColor(colorLabel);
        matrix->setCursor(midX, rowTopY);
        matrix->print(label);

        String desc = data.descriptionLong.length() > 0 ? data.descriptionLong : data.description;
        if (desc.length() > 0) {
            int size = (textW(desc, 2) <= midW) ? 2 : 1;
            if (size == 1 && textW(desc, 1) > midW) desc = data.description;   // last resort: short form
            matrix->setTextSize(size);
            matrix->setTextColor(colorDesc);
            matrix->setCursor(midX, size == 2 ? rowBottomY : rowBottomY + 4);
            matrix->print(desc);
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

        // Afternoon (bottom row)
        matrix->setTextColor(shadowColor);
        matrix->setCursor(tempX + 1, 18 + 1 + config_offset_y);
        matrix->print(tempMaxStr);
        matrix->setTextColor(colorAfternoon);
        matrix->setCursor(tempX, 18 + config_offset_y);
        matrix->print(tempMaxStr);

        // Right Column: Label on top, Condition on bottom
        int rightX = tempX + textW + 6;
        if (rightX < 60 + config_offset_x) rightX = 60 + config_offset_x;
        int maxRightX = mw - 42;
        if (rightX > maxRightX) rightX = maxRightX;

        // Label
        matrix->setTextColor(colorLabel);
        matrix->setCursor(rightX, 4 + config_offset_y);
        matrix->print(data.label);

        // Condition
        if (data.description.length() > 0) {
            int availW = mw - rightX - 2;
            int maxChars = max(1, availW / 6);
            String desc = data.description;
            if ((int)desc.length() > maxChars) {
                if (maxChars > 3) {
                    desc = desc.substring(0, maxChars - 1) + ".";
                } else {
                    desc = desc.substring(0, maxChars);
                }
            }
            matrix->setTextColor(colorDesc);
            matrix->setCursor(rightX, 18 + config_offset_y);
            matrix->print(desc);
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

        // Afternoon (bottom row) - Size 2
        matrix->setTextColor(shadowColor);
        matrix->setCursor(tempX + 1, 38 + 1 + config_offset_y);
        matrix->print(tempMaxStr);
        matrix->setTextColor(colorAfternoon);
        matrix->setCursor(tempX, 38 + config_offset_y);
        matrix->print(tempMaxStr);

        // Right Column
        int rightX = tempX + textW + 8;
        if (rightX < 72 + config_offset_x) rightX = 72 + config_offset_x;
        int maxRightX = mw - 46;
        if (rightX > maxRightX) rightX = maxRightX;

        matrix->setTextSize(1);
        matrix->setTextColor(colorLabel);
        matrix->setCursor(rightX, 14 + config_offset_y);
        matrix->print(data.label);

        if (data.description.length() > 0) {
            int availW = mw - rightX - 2;
            int maxChars = max(1, availW / 6);
            String desc = data.description;
            if ((int)desc.length() > maxChars) {
                if (maxChars > 3) {
                    desc = desc.substring(0, maxChars - 1) + ".";
                } else {
                    desc = desc.substring(0, maxChars);
                }
            }
            matrix->setTextColor(colorDesc);
            matrix->setCursor(rightX, 38 + config_offset_y);
            matrix->print(desc);
        }
    } else if (mw <= 64 && mh <= 32) {
        // --- 64x32 Compact Horizontal Layout ---
        int iconX = 2 + config_offset_x;
        int iconY = (mh - 24) / 2 + config_offset_y;
        drawIcon(data.iconCode, iconX, iconY);

        int rightX = iconX + 26;
        int availW = mw - rightX - 1;
        int maxChars = max(1, availW / 6);

        matrix->setTextSize(1);
        // Line 1: Day Label
        matrix->setTextColor(colorLabel);
        matrix->setCursor(rightX, 2 + config_offset_y);
        matrix->print(data.label);

        // Line 2: Temperatures (Min + Max)
        matrix->setCursor(rightX, 11 + config_offset_y);
        matrix->setTextColor(colorMorning);
        matrix->print(tempMinStr);
        matrix->setCursor(rightX + (strlen(tempMinStr) * 6) + 2, 11 + config_offset_y);
        matrix->setTextColor(colorAfternoon);
        matrix->print(tempMaxStr);

        // Line 3: Condition
        if (data.description.length() > 0) {
            String desc = data.description;
            if ((int)desc.length() > maxChars) {
                if (maxChars > 3) {
                    desc = desc.substring(0, maxChars - 1) + ".";
                } else {
                    desc = desc.substring(0, maxChars);
                }
            }
            matrix->setTextColor(colorDesc);
            matrix->setCursor(rightX, 21 + config_offset_y);
            matrix->print(desc);
        }
    } else {
        // --- 64x64 or Vertical Layout ---
        int iconX = (mw - 24) / 2 + config_offset_x;
        int iconY = 14 + config_offset_y;
        drawIcon(data.iconCode, iconX, iconY);

        matrix->setTextSize(1);
        int labelW = data.label.length() * 6;
        matrix->setTextColor(colorLabel);
        matrix->setCursor((mw - labelW) / 2 + config_offset_x, 3 + config_offset_y);
        matrix->print(data.label);

        if (data.description.length() > 0) {
            int descW = data.description.length() * 6;
            matrix->setTextColor(colorDesc);
            matrix->setCursor((mw - descW) / 2 + config_offset_x, 40 + config_offset_y);
            matrix->print(data.description);
        }

        int minW = strlen(tempMinStr) * 6;
        int maxW = strlen(tempMaxStr) * 6;
        
        matrix->setTextColor(colorMorning);
        matrix->setCursor((mw - minW) / 2 + config_offset_x, 49 + config_offset_y);
        matrix->print(tempMinStr);

        matrix->setTextColor(colorAfternoon);
        matrix->setCursor((mw - maxW) / 2 + config_offset_x, 57 + config_offset_y);
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

