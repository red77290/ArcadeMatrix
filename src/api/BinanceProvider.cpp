#include "BinanceProvider.h"
#include "../core/Logger.h"
#include <WiFiClientSecure.h>

bool BinanceProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String apiSymbol = symbol;
    if (!apiSymbol.endsWith("USDT") && !apiSymbol.endsWith("USD")) {
        apiSymbol += "USDT";
    }
    String binanceUrl = "https://api.binance.com/api/v3/ticker/24hr?symbol=" + apiSymbol;
    
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    if (http.begin(client, binanceUrl)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            if (parsePayload(payload, outPrice, outChange)) {
                http.end();
                client.stop();
                return true;
            }
        }
        http.end();
        client.stop();
    }
    
    return false;
}

bool BinanceProvider::parsePayload(const String& payload, float& outPrice, float& outChange) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
        outPrice = doc["lastPrice"].as<float>();
        outChange = doc["priceChangePercent"].as<float>();
        return (outPrice > 0.0f);
    }
    return false;
}

bool BinanceProvider::fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    String apiSymbol = symbol;
    if (!apiSymbol.endsWith("USDT") && !apiSymbol.endsWith("USD")) {
        apiSymbol += "USDT";
    }

    const char* interval = "1h";
    int limit = 24;
    switch (tf) {
        case Timeframe::Hourly:
            interval = "1m";
            limit = 60;
            break;
        case Timeframe::Daily:
            interval = "1h";
            limit = 24;
            break;
        case Timeframe::Weekly:
            interval = "4h";
            limit = 42;
            break;
        case Timeframe::Monthly:
            interval = "1d";
            limit = 30;
            break;
    }

    String url = "https://api.binance.com/api/v3/klines?symbol=" + apiSymbol + "&interval=" + String(interval) + "&limit=" + String(limit);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

    if (http.begin(client, url)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            if (parseKlines(payload, outPoints, maxPoints, outCount, outMin, outMax)) {
                http.end();
                client.stop();
                return true;
            }
        }
        http.end();
        client.stop();
    }
    return false;
}

bool BinanceProvider::parseKlines(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        size_t n = arr.size();
        if (n == 0) return false;

        outCount = 0;
        outMin = 1e9f;
        outMax = -1e9f;

        for (size_t i = 0; i < n && outCount < maxPoints; ++i) {
            JsonArray kline = arr[i];
            if (kline.size() >= 5) {
                float closePrice = kline[4].as<float>();
                if (closePrice > 0.0f) {
                    outPoints[outCount++] = closePrice;
                    if (closePrice < outMin) outMin = closePrice;
                    if (closePrice > outMax) outMax = closePrice;
                }
            }
        }
        return (outCount > 0 && outMin <= outMax);
    }
    return false;
}
