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

bool OpenWeatherMapProvider::fetchForecast(const String& apiKey, const String& city, const String& lang, const String& units, WeatherData outForecasts[], int maxDays, int& outNumForecasts) {
    HTTPClient http;
    String reqLang = lang.length() > 0 ? lang : "fr";
    String reqUnits = (units.equalsIgnoreCase("imperial") || units.equalsIgnoreCase("fahrenheit") || units.equalsIgnoreCase("f")) ? "imperial" : "metric";
    
    String encodedCity = city;
    encodedCity.trim();
    encodedCity.replace(" ", "%20");
    
    String url = "http://api.openweathermap.org/data/2.5/forecast?q=" + encodedCity + "&units=" + reqUnits + "&appid=" + apiKey + "&lang=" + reqLang;
    
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

static String translateCondition(const String& mainCond, const String& apiDesc, const String& lang) {
    String m = mainCond;
    m.toLowerCase();
    String d = apiDesc;
    d.toLowerCase();
    String l = lang;
    l.toLowerCase();
    
    if (l == "fr") {
        if (m.indexOf("clear") != -1) return "Soleil";
        if (m.indexOf("cloud") != -1) {
            if (d.indexOf("couvert") != -1 || d.indexOf("overcast") != -1) return "Couvert";
            if (d.indexOf("part") != -1 || d.indexOf("peu") != -1 || d.indexOf("scat") != -1) return "Eclaircies";
            return "Nuageux";
        }
        if (m.indexOf("rain") != -1 || m.indexOf("drizzle") != -1) return "Pluie";
        if (m.indexOf("thunder") != -1) return "Orage";
        if (m.indexOf("snow") != -1) return "Neige";
        if (m.indexOf("mist") != -1 || m.indexOf("fog") != -1 || m.indexOf("haze") != -1) return "Brume";
        if (apiDesc.length() > 0) {
            String res = apiDesc;
            res[0] = toupper(res[0]);
            return res;
        }
        return "Meteo";
    } else if (l == "es") {
        if (m.indexOf("clear") != -1) return "Soleado";
        if (m.indexOf("cloud") != -1) return "Nublado";
        if (m.indexOf("rain") != -1 || m.indexOf("drizzle") != -1) return "Lluvia";
        if (m.indexOf("thunder") != -1) return "Tormenta";
        if (m.indexOf("snow") != -1) return "Nieve";
        if (m.indexOf("mist") != -1 || m.indexOf("fog") != -1) return "Niebla";
    }
    
    if (m.indexOf("clear") != -1) return "Clear";
    if (m.indexOf("cloud") != -1) return "Clouds";
    if (m.indexOf("rain") != -1) return "Rain";
    if (m.indexOf("drizzle") != -1) return "Drizzle";
    if (m.indexOf("thunder") != -1) return "Thunder";
    if (m.indexOf("snow") != -1) return "Snow";
    if (m.indexOf("mist") != -1 || m.indexOf("fog") != -1) return "Fog";
    return mainCond;
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
            d.description = translateCondition(rawMain, rawDesc, lang);
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
