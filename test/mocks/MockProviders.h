#pragma once
#include "api/ICryptoProvider.h"
#include "api/IStockProvider.h"
#include "api/IWeatherProvider.h"

class MockCryptoProvider : public ICryptoProvider {
public:
    bool mockSuccess = true;
    float mockPrice = 100.0f;
    float mockChange = 5.0f;
    int fetchCount = 0;
    
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override {
        fetchCount++;
        if (mockSuccess) {
            outPrice = mockPrice;
            outChange = mockChange;
            return true;
        }
        return false;
    }
};

class MockStockProvider : public IStockProvider {
public:
    bool mockSuccess = true;
    float mockPrice = 150.0f;
    float mockChange = -2.5f;
    int fetchCount = 0;
    
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override {
        fetchCount++;
        if (mockSuccess) {
            outPrice = mockPrice;
            outChange = mockChange;
            return true;
        }
        return false;
    }
};

class MockWeatherProvider : public IWeatherProvider {
public:
    bool mockSuccess = true;
    int fetchCount = 0;
    
    bool fetchForecast(const String& apiKey, const String& city, const String& lang, WeatherData outForecasts[], int maxDays, int& outNumForecasts) override {
        fetchCount++;
        if (mockSuccess) {
            outNumForecasts = min(3, maxDays);
            for (int i = 0; i < outNumForecasts; i++) {
                outForecasts[i].temp = 20.0f + i;
                outForecasts[i].description = "Clear";
                outForecasts[i].iconCode = "01d";
                outForecasts[i].label = "Day " + String(i);
            }
            return true;
        }
        return false;
    }
};
