#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include "../api/IWeatherProvider.h"

#include "../../include/core/EngineContract.h"
#include "../core/AppEngineContext.h"

class WeatherEngine : public IEngine {
public:
    static const int MAX_FORECAST_DAYS = 3;

    WeatherEngine();
    ~WeatherEngine();
    
    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;

    void addProvider(IWeatherProvider* provider);
    
    String config_api_key;
    String config_city;
    String config_lang;
    String config_units = "metric";
    int config_offset_x = 0;
    int config_offset_y = 0;
    
    void updateWeather(const String& apiKey, const String& city, const String& units = "metric");
    
    bool loop();
    void setCharacter(int characterId);
    void forceUpdate() { lastFetchTime = 0; }

    bool hasValidData() const { return validData; }

private:
    MatrixPanel_I2S_DMA* matrix;
    std::vector<IWeatherProvider*> providers;
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
