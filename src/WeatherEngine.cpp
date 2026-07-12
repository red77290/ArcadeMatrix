#include "WeatherEngine.h"
#include "ConfigLoader.h"
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
    // Basic handmade icons
    if (icon.indexOf("01") != -1) { // Sun
        matrix->fillCircle(x + 5, y + 5, 4, matrix->color565(255, 255, 0));
    } else if (icon.indexOf("02") != -1 || icon.indexOf("03") != -1 || icon.indexOf("04") != -1) { // Clouds
        matrix->fillCircle(x + 4, y + 6, 3, matrix->color565(200, 200, 200));
        matrix->fillCircle(x + 7, y + 4, 4, matrix->color565(255, 255, 255));
        matrix->fillCircle(x + 10, y + 6, 3, matrix->color565(200, 200, 200));
    } else if (icon.indexOf("09") != -1 || icon.indexOf("10") != -1) { // Rain
        matrix->fillCircle(x + 7, y + 4, 4, matrix->color565(150, 150, 150));
        matrix->drawLine(x + 4, y + 9, x + 3, y + 11, matrix->color565(0, 0, 255));
        matrix->drawLine(x + 7, y + 9, x + 6, y + 11, matrix->color565(0, 0, 255));
        matrix->drawLine(x + 10, y + 9, x + 9, y + 11, matrix->color565(0, 0, 255));
    } else if (icon.indexOf("11") != -1) { // Thunder
        matrix->fillCircle(x + 7, y + 4, 4, matrix->color565(100, 100, 100));
        matrix->drawLine(x + 6, y + 8, x + 4, y + 11, matrix->color565(255, 255, 0));
        matrix->drawLine(x + 4, y + 11, x + 8, y + 11, matrix->color565(255, 255, 0));
        matrix->drawLine(x + 8, y + 11, x + 6, y + 15, matrix->color565(255, 255, 0));
    } else if (icon.indexOf("13") != -1) { // Snow
        matrix->drawPixel(x + 7, y + 7, matrix->color565(255, 255, 255));
        matrix->drawLine(x + 5, y + 5, x + 9, y + 9, matrix->color565(255, 255, 255));
        matrix->drawLine(x + 9, y + 5, x + 5, y + 9, matrix->color565(255, 255, 255));
    } else { // Unknown
        matrix->fillCircle(x + 5, y + 5, 4, matrix->color565(0, 255, 0)); // Green dot
    }
}

void WeatherEngine::loop() {
    if (!validData) return;
    
    matrix->setTextSize(1);
    
    char tempStr[16];
    sprintf(tempStr, "%.1fC", currentData.temp);
    
    int textWidth = strlen(tempStr) * 6;
    int totalWidth = 14 + textWidth;
    int startX = (matrix->width() - totalWidth) / 2 + config.weather.weather_offset_x;
    
    int iconX = startX;
    int textX = startX + 14;
    int y = (matrix->height() - 8) / 2 + config.weather.weather_offset_y;
    
    // Draw icon
    drawIcon(currentData.iconCode, iconX, y - 2);

    // Draw shadow
    matrix->setTextColor(shadowColor);
    matrix->setCursor(textX + 1, y + 1);
    matrix->print(tempStr);
    
    // Draw text
    matrix->setTextColor(textColor);
    matrix->setCursor(textX, y);
    matrix->print(tempStr);
}
