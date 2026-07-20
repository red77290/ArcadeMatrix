#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

struct WeatherData {
    String description;
    float temp;
    String iconCode;
};

class WeatherEngine {
public:
    WeatherEngine(MatrixPanel_I2S_DMA* display);
    ~WeatherEngine();

    // Fetch new weather data if interval has passed
    void update(const String& apiKey, const String& city);
    
    void loop();
    void setCharacter(int characterId);

    bool hasValidData() const { return validData; }

private:
    MatrixPanel_I2S_DMA* matrix;
    WeatherData currentData;
    bool validData;
    uint32_t lastFetchTime;
    
    uint16_t textColor;
    uint16_t shadowColor;

    void drawIcon(const String& icon, int x, int y);
};
