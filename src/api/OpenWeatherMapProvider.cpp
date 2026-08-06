#include "OpenWeatherMapProvider.h"
#include "../core/Logger.h"
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

bool OpenWeatherMapProvider::fetchForecast(const String& apiKey, const String& city, const String& lang, WeatherData outForecasts[], int maxDays, int& outNumForecasts) {
    HTTPClient http;
    String reqLang = lang.length() > 0 ? lang : "fr";
    
    String url = "http://api.openweathermap.org/data/2.5/forecast?q=" + city + "&units=metric&appid=" + apiKey + "&lang=" + reqLang;
    
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
        
        const char* dayNames[7];
        const char* fixedLabels[3];
        
        if (lang.equalsIgnoreCase("fr")) {
            const char* fr_dayNames[7] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
            const char* fr_fixedLabels[3] = {"AUJ.", "DEMN", nullptr};
            memcpy(dayNames, fr_dayNames, sizeof(dayNames));
            memcpy(fixedLabels, fr_fixedLabels, sizeof(fixedLabels));
        } else if (lang.equalsIgnoreCase("es")) {
            const char* es_dayNames[7] = {"DOM", "LUN", "MAR", "MIE", "JUE", "VIE", "SAB"};
            const char* es_fixedLabels[3] = {"HOY", "MANA", nullptr};
            memcpy(dayNames, es_dayNames, sizeof(dayNames));
            memcpy(fixedLabels, es_fixedLabels, sizeof(fixedLabels));
        } else {
            const char* en_dayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
            const char* en_fixedLabels[3] = {"TODAY", "TMRW", nullptr};
            memcpy(dayNames, en_dayNames, sizeof(dayNames));
            memcpy(fixedLabels, en_fixedLabels, sizeof(fixedLabels));
        }

        const int sampleIndices[3] = {0, 8, 16};

        outNumForecasts = 0;
        for (int i = 0; i < maxDays && i < 3; i++) {
            int idx = sampleIndices[i];
            if (idx >= (int)list.size()) break;

            JsonObject item = list[idx];
            WeatherData& d = outForecasts[outNumForecasts];
            d.temp = item["main"]["temp"].as<float>();
            d.description = item["weather"][0]["main"].as<String>();
            d.iconCode = item["weather"][0]["icon"].as<String>();

            if (fixedLabels[i] != nullptr) {
                d.label = fixedLabels[i];
            } else if (haveTime) {
                int dayOfWeek = (currentWday + 2) % 7;
                d.label = dayNames[dayOfWeek];
            } else {
                d.label = "DAY3";
            }
            outNumForecasts++;
        }
        return true;
    }
    return false;
}
