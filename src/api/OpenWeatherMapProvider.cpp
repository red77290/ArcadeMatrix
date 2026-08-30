#include "OpenWeatherMapProvider.h"
#include "../core/Logger.h"
#include "core/I18n.h"
#include <esp_heap_caps.h>

struct SpiRamAllocator {
  void* allocate(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(size);
    return p;
  }
  void deallocate(void* pointer) {
    free(pointer);
  }
  void* reallocate(void* ptr, size_t new_size) {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = realloc(ptr, new_size);
    return p;
  }
};

using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;

bool OpenWeatherMapProvider::fetchForecast(const String& apiKey, const String& city, const String& lang, const String& units, WeatherData outForecasts[], int maxDays, int& outNumForecasts) {
    HTTPClient http;
    String reqLang = lang.length() > 0 ? lang : "fr";
    String reqUnits = (units.equalsIgnoreCase("imperial") || units.equalsIgnoreCase("fahrenheit") || units.equalsIgnoreCase("f")) ? "imperial" : "metric";
    
    String encodedCity = city;
    encodedCity.trim();
    encodedCity.replace(" ", "%20");
    
    String url = "http://api.openweathermap.org/data/2.5/forecast?q=" + encodedCity + "&units=" + reqUnits + "&appid=" + apiKey + "&lang=" + reqLang;
    
    http.setTimeout(3000);
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        
        struct tm timeinfo;
        bool haveTime = getLocalTime(&timeinfo, 0);
        int currentWday = haveTime ? timeinfo.tm_wday : 0;
        
        bool success = parsePayload(payload, outForecasts, maxDays, outNumForecasts, reqLang, haveTime, currentWday);
        http.end();
        return success;
    }
    http.end();
    return false;
}

bool OpenWeatherMapProvider::parsePayload(const String& payload, WeatherData outForecasts[], int maxDays, int& outNumForecasts, const String& lang, bool haveTime, int currentWday) {
    SpiRamJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error && doc["list"].is<JsonArray>()) {
        JsonArray list = doc["list"].as<JsonArray>();
        const int sampleIndices[3] = {0, 8, 16};

        outNumForecasts = 0;
        for (int i = 0; i < maxDays && i < 3; i++) {
            int idx = sampleIndices[i];
            if (idx >= (int)list.size()) break;

            JsonObject item = list[idx];
            WeatherData& d = outForecasts[outNumForecasts];

            // Calculate min (morning) and max (afternoon) temps across day's intervals
            float dayMin = 999.0f;
            float dayMax = -999.0f;
            int startIdx = i * 8;
            int endIdx = min((i + 1) * 8, (int)list.size());
            for (int k = startIdx; k < endIdx; k++) {
                JsonObject entry = list[k];
                float t = entry["main"]["temp"].as<float>();
                float tMin = entry["main"]["temp_min"] | t;
                float tMax = entry["main"]["temp_max"] | t;
                if (tMin < dayMin) dayMin = tMin;
                if (tMax > dayMax) dayMax = tMax;
                if (t < dayMin) dayMin = t;
                if (t > dayMax) dayMax = t;
            }
            if (dayMin > 900.0f) dayMin = item["main"]["temp"].as<float>();
            if (dayMax < -900.0f) dayMax = item["main"]["temp"].as<float>();

            d.temp = item["main"]["temp"].as<float>();
            d.temp_min = dayMin;
            d.temp_max = dayMax;

            String rawMain = item["weather"][0]["main"] | "";
            String rawDesc = item["weather"][0]["description"] | "";
            String combined = rawMain + " " + rawDesc;
            d.description = I18n::getWeatherCondition(combined);
            d.iconCode = item["weather"][0]["icon"].as<String>();

            if (i == 0) {
                d.label = I18n::getWeatherDayLabel(currentWday, true, false);
            } else if (i == 1) {
                d.label = I18n::getWeatherDayLabel((currentWday + 1) % 7, false, true);
            } else {
                d.label = I18n::getWeatherDayLabel((currentWday + 2) % 7, false, false);
            }
            outNumForecasts++;
        }
        return true;
    }
    return false;
}
