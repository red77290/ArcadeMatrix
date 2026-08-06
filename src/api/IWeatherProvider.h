#pragma once
#include <Arduino.h>

struct WeatherData {
    String description;
    float temp;
    String iconCode;
    String label; // "Today" / "Tmrw" / abbreviated weekday name
};

class IWeatherProvider {
public:
    virtual ~IWeatherProvider() = default;
    
    /**
     * @brief Fetches the weather forecast.
     * @param apiKey The API key.
     * @param city The city to fetch weather for.
     * @param lang The language for the forecast (e.g. "en", "fr").
     * @param outForecasts Array to store the forecasts.
     * @param maxDays Maximum number of days to fetch (size of outForecasts array).
     * @param outNumForecasts The actual number of forecasts fetched.
     * @return true if successful, false otherwise.
     */
    virtual bool fetchForecast(const String& apiKey, const String& city, const String& lang, WeatherData outForecasts[], int maxDays, int& outNumForecasts) = 0;
};
