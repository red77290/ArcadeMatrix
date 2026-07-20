#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

struct WeatherData {
    String description;
    float temp;
    String iconCode;
    String label; // "Today" / "Tmrw" / abbreviated weekday name
};

class WeatherEngine {
public:
    static const int MAX_FORECAST_DAYS = 3;

    WeatherEngine(MatrixPanel_I2S_DMA* display);
    ~WeatherEngine();

    // Fetch a new 3-day forecast if the cache interval has passed. Mirrors the RPi's
    // WeatherEngine._fetch_weather(): uses OpenWeatherMap's free /forecast endpoint (3-hour steps
    // over 5 days) and samples index 0 (now), 8 (~+24h) and 16 (~+48h) as Today/Tomorrow/Day3.
    void update(const String& apiKey, const String& city);
    
    void loop();
    void setCharacter(int characterId);

    bool hasValidData() const { return validData; }

private:
    MatrixPanel_I2S_DMA* matrix;
    WeatherData forecasts[MAX_FORECAST_DAYS];
    int numForecasts;
    bool validData;
    uint32_t lastFetchTime;
    
    uint16_t textColor;
    uint16_t shadowColor;

    // Cycles through `forecasts` every slideDurationMs, mirroring the RPi's slideshow (simplified:
    // an instant switch instead of the RPi's eased horizontal scroll animation, since redrawing
    // vector icons every frame during a scroll would be significantly more CPU-expensive on an
    // MCU with no GPU/compositing hardware).
    int activeSlide;
    unsigned long lastSlideChange;
    static const unsigned long slideDurationMs = 5000;

    void drawIcon(const String& icon, int x, int y);
    void drawForecast(const WeatherData& data);
};
