#include "WeatherEngine.h"
#include "../core/ConfigLoader.h"
extern ConfigLoader config;
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

WeatherEngine::WeatherEngine(MatrixPanel_I2S_DMA* display) : matrix(display) {
    validData = false;
    lastFetchTime = 0;
    textColor = matrix->color565(255, 255, 255);
    shadowColor = matrix->color565(0, 0, 0);
    currentData.temp = 0;
}

WeatherEngine::~WeatherEngine() {}

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

void WeatherEngine::update(const String& apiKey, const String& city) {
    if (apiKey.length() == 0 || city.length() == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Only update every 15 minutes (900000 ms) to save API calls
    if (lastFetchTime > 0 && millis() - lastFetchTime < 900000) return;

    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "&units=metric&appid=" + apiKey;
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            currentData.temp = doc["main"]["temp"];
            currentData.description = doc["weather"][0]["main"].as<String>();
            currentData.iconCode = doc["weather"][0]["icon"].as<String>();
            validData = true;
            lastFetchTime = millis();
        }
    }
    http.end();
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

void WeatherEngine::loop() {
    if (!validData) return;
    
    char tempStr[16];
    sprintf(tempStr, "%.1fC", currentData.temp);
    
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
    int startX = (matrix->width() - totalWidth) / 2 + config.weather.weather_offset_x;
    
    int iconX = startX;
    int textX = startX + iconWidth + 4;
    int y = (matrix->height() - charHeight) / 2 + config.weather.weather_offset_y;
    int iconY = (matrix->height() - iconHeight) / 2 + config.weather.weather_offset_y;
    
    // If it doesn't fit horizontally (e.g. 64x64), stack vertically
    if (totalWidth > matrix->width() && matrix->height() >= 64) {
        iconX = (matrix->width() - iconWidth) / 2 + config.weather.weather_offset_x;
        textX = (matrix->width() - textWidth) / 2 + config.weather.weather_offset_x;
        iconY = (matrix->height() / 2 - iconHeight) / 2 + config.weather.weather_offset_y;
        y = matrix->height() / 2 + (matrix->height() / 2 - charHeight) / 2 + config.weather.weather_offset_y;
    }
    
    // Draw icon
    drawIcon(currentData.iconCode, iconX, iconY);

    // Draw shadow
    matrix->setTextColor(shadowColor);
    matrix->setCursor(textX + 1, y + 1);
    matrix->print(tempStr);
    
    // Draw text
    matrix->setTextColor(textColor);
    matrix->setCursor(textX, y);
    matrix->print(tempStr);
}
