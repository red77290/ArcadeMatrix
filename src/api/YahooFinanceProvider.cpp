#include "YahooFinanceProvider.h"
#include "../core/Logger.h"
#include <WiFiClientSecure.h>

bool YahooFinanceProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
    
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    int code = -1;
    if (http.begin(client, url)) {
        code = http.GET();
        if (code != 200) {
            http.end();
            String fallbackUrl = "https://query2.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
            if (http.begin(client, fallbackUrl)) {
                code = http.GET();
            }
        }
        
        if (code == 200) {
            String payload = http.getString();
            if (parsePayload(payload, outPrice, outChange)) {
                String lowerSymbol = symbol;
                lowerSymbol.toLowerCase();
                outImageUrl = "https://eodhd.com/img/logos/US/" + lowerSymbol + ".png";
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

bool YahooFinanceProvider::parsePayload(const String& payload, float& outPrice, float& outChange) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
        JsonObject meta = doc["chart"]["result"][0]["meta"];
        if (!meta.isNull()) {
            outPrice = meta["regularMarketPrice"] | 0.0f;
            float prevClose = meta["previousClose"] | meta["chartPreviousClose"] | outPrice;
            if (prevClose > 0.0f && outPrice > 0.0f) {
                outChange = ((outPrice - prevClose) / prevClose) * 100.0f;
            } else {
                outChange = 0.0f;
            }
            return (outPrice > 0.0f);
        }
    }
    return false;
}

bool YahooFinanceProvider::fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    const char* range = "1d";
    const char* interval = "5m";
    switch (tf) {
        case Timeframe::Hourly:
            range = "1d";
            interval = "2m";
            break;
        case Timeframe::Daily:
            range = "1d";
            interval = "5m";
            break;
        case Timeframe::Weekly:
            range = "5d";
            interval = "15m";
            break;
        case Timeframe::Monthly:
            range = "1mo";
            interval = "1d";
            break;
    }

    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=" + String(interval) + "&range=" + String(range);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

    int code = -1;
    if (http.begin(client, url)) {
        code = http.GET();
        if (code != 200) {
            http.end();
            String fallbackUrl = "https://query2.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=" + String(interval) + "&range=" + String(range);
            if (http.begin(client, fallbackUrl)) {
                code = http.GET();
            }
        }

        if (code == 200) {
            String payload = http.getString();
            if (parseChart(payload, outPoints, maxPoints, outCount, outMin, outMax)) {
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

bool YahooFinanceProvider::parseChart(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
        JsonArray closes = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"].as<JsonArray>();
        if (closes.isNull()) return false;

        outCount = 0;
        outMin = 1e9f;
        outMax = -1e9f;

        size_t n = closes.size();
        if (n == 0) return false;

        // Downsample or take step if n > maxPoints
        size_t step = (n > maxPoints) ? (n / maxPoints) : 1;
        if (step == 0) step = 1;

        for (size_t i = 0; i < n && outCount < maxPoints; i += step) {
            if (!closes[i].isNull()) {
                float val = closes[i].as<float>();
                if (val > 0.0f) {
                    outPoints[outCount++] = val;
                    if (val < outMin) outMin = val;
                    if (val > outMax) outMax = val;
                }
            }
        }
        return (outCount > 0 && outMin <= outMax);
    }
    return false;
}
