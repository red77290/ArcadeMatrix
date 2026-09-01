#include "CoinGeckoProvider.h"
#include "../core/Logger.h"
#include <WiFiClientSecure.h>

bool CoinGeckoProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String lowerSymbol = symbol;
    lowerSymbol.toLowerCase();
    
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(5000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    String vsCur = m_currency;
    vsCur.toLowerCase();
    if (vsCur.isEmpty()) vsCur = "usd";

    // Primary API
    String cgUrl = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=" + vsCur + "&symbols=" + lowerSymbol;
    if (http.begin(client, cgUrl)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            if (parsePrimary(payload, outPrice, outChange, outImageUrl)) {
                http.end();
                client.stop();
                return true;
            }
        }
        http.end();
        client.stop();
    }
    
    // Simple API fallback
    String coinId = lowerSymbol;
    if (lowerSymbol == "erg") coinId = "ergo";
    
    String cgSimpleUrl = "https://api.coingecko.com/api/v3/simple/price?ids=" + coinId + "&vs_currencies=" + vsCur + "&include_24hr_change=true";
    if (http.begin(client, cgSimpleUrl)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            if (parseSimple(payload, coinId, outPrice, outChange)) {
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

bool CoinGeckoProvider::parsePrimary(const String& payload, float& outPrice, float& outChange, String& outImageUrl) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
        JsonObject coin = doc[0];
        outPrice = coin["current_price"] | 0.0f;
        outChange = coin["price_change_percentage_24h"] | 0.0f;
        outImageUrl = coin["image"].as<String>();
        return (outPrice > 0.0f);
    }
    return false;
}

bool CoinGeckoProvider::parseSimple(const String& payload, const String& coinId, float& outPrice, float& outChange) {
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc.containsKey(coinId)) {
        JsonObject item = doc[coinId];
        outPrice = item["usd"] | 0.0f;
        outChange = item["usd_24h_change"] | 0.0f;
        return (outPrice > 0.0f);
    }
    return false;
}

bool CoinGeckoProvider::fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    String lower = symbol;
    lower.toLowerCase();
    String coinId = lower;
    if (lower == "btc") coinId = "bitcoin";
    else if (lower == "eth") coinId = "ethereum";
    else if (lower == "sol") coinId = "solana";
    else if (lower == "erg") coinId = "ergo";
    else if (lower == "doge") coinId = "dogecoin";
    else if (lower == "ada") coinId = "cardano";
    else if (lower == "xrp") coinId = "ripple";
    else if (lower == "dot") coinId = "polkadot";
    else if (lower == "link") coinId = "chainlink";
    else if (lower == "avax") coinId = "avalanche-2";

    const char* days = "1";
    switch (tf) {
        case Timeframe::Hourly:
            days = "1";
            break;
        case Timeframe::Daily:
            days = "1";
            break;
        case Timeframe::Weekly:
            days = "7";
            break;
        case Timeframe::Monthly:
            days = "30";
            break;
    }

    String vsCur = m_currency;
    vsCur.toLowerCase();
    if (vsCur.isEmpty()) vsCur = "usd";

    String url = "https://api.coingecko.com/api/v3/coins/" + coinId + "/market_chart?vs_currency=" + vsCur + "&days=" + String(days);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

    if (http.begin(client, url)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            if (parseMarketChart(payload, tf, outPoints, maxPoints, outCount, outMin, outMax)) {
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

bool CoinGeckoProvider::parseMarketChart(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    return parseMarketChart(payload, Timeframe::Daily, outPoints, maxPoints, outCount, outMin, outMax);
}

bool CoinGeckoProvider::parseMarketChart(const String& payload, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    int pricesPos = payload.indexOf("\"prices\":");
    if (pricesPos < 0) return false;

    int startArr = payload.indexOf('[', pricesPos);
    if (startArr < 0) return false;

    std::vector<float> allPrices;
    allPrices.reserve(300);

    int cur = startArr + 1;
    int len = payload.length();

    while (cur < len) {
        int openBracket = payload.indexOf('[', cur);
        if (openBracket < 0) break;
        int closeBracket = payload.indexOf(']', openBracket);
        if (closeBracket < 0) break;

        int comma = payload.indexOf(',', openBracket);
        if (comma > 0 && comma < closeBracket) {
            String valStr = payload.substring(comma + 1, closeBracket);
            valStr.trim();
            float val = valStr.toFloat();
            if (val > 0.0f) {
                allPrices.push_back(val);
            }
        }
        cur = closeBracket + 1;
        // If we reach the end of the outer prices array
        while (cur < len && (payload[cur] == ' ' || payload[cur] == '\n' || payload[cur] == '\r' || payload[cur] == '\t')) {
            cur++;
        }
        if (cur < len && payload[cur] == ']') break;
    }

    size_t n = allPrices.size();
    if (n == 0) return false;

    outCount = 0;
    outMin = 1e9f;
    outMax = -1e9f;

    size_t startIdx = 0;
    size_t endIdx = n;

    if (tf == Timeframe::Hourly && n > 12) {
        startIdx = n - 12;
    }

    size_t range = endIdx - startIdx;
    size_t step = (range > maxPoints) ? (range / maxPoints) : 1;
    if (step == 0) step = 1;

    for (size_t i = startIdx; i < endIdx && outCount < maxPoints; i += step) {
        float val = allPrices[i];
        outPoints[outCount++] = val;
        if (val < outMin) outMin = val;
        if (val > outMax) outMax = val;
    }

    return (outCount > 0 && outMin <= outMax);
}
