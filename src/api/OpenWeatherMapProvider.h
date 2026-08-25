#pragma once
#include "../api/IWeatherProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class OpenWeatherMapProvider : public IWeatherProvider {
public:
    bool fetchForecast(const String& apiKey, const String& city, const String& lang, const String& units, WeatherData outForecasts[], int maxDays, int& outNumForecasts) override;
    bool fetchForecast(const String& apiKey, const String& city, const String& lang, WeatherData outForecasts[], int maxDays, int& outNumForecasts) override {
        return fetchForecast(apiKey, city, lang, "metric", outForecasts, maxDays, outNumForecasts);
    }
    
    // Public parsing methods for TDD
    bool parsePayload(const String& payload, WeatherData outForecasts[], int maxDays, int& outNumForecasts, const String& lang, bool haveTime, int currentWday);
};
