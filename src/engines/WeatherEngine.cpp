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
    
    return EngineError::OK;
}

void WeatherEngine::activate() {}

void WeatherEngine::update(EngineContext* context) {
    loop();
}

void WeatherEngine::render(EngineContext* context) {}

void WeatherEngine::deactivate() {}

void WeatherEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;
    config_api_key = engineConfig->getString("api_key", "");
    config_city = engineConfig->getString("city", "");
    config_lang = engineConfig->getString("lang", "fr");
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

void WeatherEngine::updateWeather(const String& apiKey, const String& city) {
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
        if (provider->fetchForecast(apiKey, city, reqLang, forecasts, MAX_FORECAST_DAYS, numForecasts)) {
            fetched = true;
            break;
        }
    }
    
    if (fetched && numForecasts > 0) {
        validData = true;
        activeSlide = 0;
        lastSlideChange = millis();
        LOGI("WeatherEngine", "Success! Parsed %d forecast days.", numForecasts);
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
    updateWeather(config_api_key, config_city);

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
    // Reset font to default GLCD font to avoid drawing from baseline (which pushes text off-screen)
    // if another engine left a custom GFX font active.
    matrix->setFont(nullptr);
    
    char tempStr[16];
    sprintf(tempStr, "%.0fC", data.temp);
    
    int textSize = 1;
    int charWidth = 6;
    int charHeight = 8;
    
    // Only use size 2 if we have at least 128 width or 64 width but stack vertically
    if (matrix->width() >= 128) {
        textSize = 2;
        charWidth = 12;
        charHeight = 16;
    }
    
    matrix->setTextSize(textSize);
    
    int textWidth = strlen(tempStr) * charWidth;
    int iconWidth = 24;
    int iconHeight = 24;
    
    int totalWidth = iconWidth + 4 + textWidth;
    int startX = (matrix->width() - totalWidth) / 2 + config_offset_x;
    
    int iconX = startX;
    int textX = startX + iconWidth + 4;
    int y = (matrix->height() - charHeight) / 2 + config_offset_y;
    int iconY = (matrix->height() - iconHeight) / 2 + config_offset_y;
    
    // If it doesn't fit horizontally (e.g. 64x64), stack vertically
    if (totalWidth > matrix->width() && matrix->height() >= 64) {
        iconX = (matrix->width() - iconWidth) / 2 + config_offset_x;
        textX = (matrix->width() - textWidth) / 2 + config_offset_x;
        iconY = (matrix->height() / 2 - iconHeight) / 2 + config_offset_y;
        y = matrix->height() / 2 + (matrix->height() / 2 - charHeight) / 2 + config_offset_y;
    }
    
    // Draw icon
    drawIcon(data.iconCode, iconX, iconY);

    // Draw day label (TODAY/TMRW/weekday) in the top-left corner, small size to avoid overlapping.
    matrix->setTextSize(1);
    matrix->setTextColor(matrix->color565(180, 180, 255));
    matrix->setCursor(2, 2);
    matrix->print(data.label);

    matrix->setTextSize(textSize);

    // Draw shadow
    matrix->setTextColor(shadowColor);
    matrix->setCursor(textX + 1, y + 1);
    matrix->print(tempStr);
    
    // Draw text
    matrix->setTextColor(textColor);
    matrix->setCursor(textX, y);
    matrix->print(tempStr);
}
