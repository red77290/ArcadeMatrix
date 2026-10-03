#include "CoinGeckoProvider.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#include "../core/net/SecureHttpClient.h"

bool CoinGeckoProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String lowerSymbol = symbol;
    lowerSymbol.toLowerCase();
    String upperSymbol = symbol;
    upperSymbol.toUpperCase();
    
    String vsCur = m_currency;
    vsCur.toLowerCase();
    if (vsCur.isEmpty()) vsCur = "usd";

    // Primary API: query CoinGecko /coins/markets by symbol directly
    String cgUrl = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=" + vsCur + "&symbols=" + lowerSymbol;
    auto res = net::SecureHttpClient::get(cgUrl);
    String discoveredId = "";
    if (res.ok() && parsePrimary(res.stream(), outPrice, outChange, outImageUrl, &discoveredId)) {
        if (!discoveredId.isEmpty()) {
            m_symbolToId[upperSymbol] = discoveredId;
        }
        return true;
    }

    // Simple API fallback (if markets failed softly, query simple/price using dynamic ID if cached or lowerSymbol)
    if (res.statusCode() > 0 && res.statusCode() != 429 && res.statusCode() != 403) {
        auto it = m_symbolToId.find(upperSymbol);
        String coinId = (it != m_symbolToId.end()) ? it->second : lowerSymbol;
        
        String cgSimpleUrl = "https://api.coingecko.com/api/v3/simple/price?ids=" + coinId + "&vs_currencies=" + vsCur + "&include_24hr_change=true";
        auto resSimple = net::SecureHttpClient::get(cgSimpleUrl);
        if (resSimple.ok() && parseSimple(resSimple.stream(), coinId, outPrice, outChange)) {
            return true;
        }
    }
    
    return false;
}

bool CoinGeckoProvider::parsePrimary(Stream& stream, float& outPrice, float& outChange, String& outImageUrl, String* outId) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, stream);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
        JsonObject coin = doc[0];
        outPrice = coin["current_price"] | 0.0f;
        outChange = coin["price_change_percentage_24h"] | 0.0f;
        outImageUrl = coin["image"].as<String>();
        if (outId && coin.containsKey("id")) {
            *outId = coin["id"].as<String>();
        }
        return (outPrice > 0.0f);
    }
    return false;
}

bool CoinGeckoProvider::parsePrimary(const String& payload, float& outPrice, float& outChange, String& outImageUrl, String* outId) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
        JsonObject coin = doc[0];
        outPrice = coin["current_price"] | 0.0f;
        outChange = coin["price_change_percentage_24h"] | 0.0f;
        outImageUrl = coin["image"].as<String>();
        if (outId && coin.containsKey("id")) {
            *outId = coin["id"].as<String>();
        }
        return (outPrice > 0.0f);
    }
    return false;
}

bool CoinGeckoProvider::parseSimple(Stream& stream, const String& coinId, float& outPrice, float& outChange) {
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, stream);
    if (!err && doc.containsKey(coinId)) {
        JsonObject item = doc[coinId];
        outPrice = item["usd"] | 0.0f;
        outChange = item["usd_24h_change"] | 0.0f;
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

    // Safety guard against memory explosion
    if (!NetworkBudget::canStartTlsSession()) {
        LOGW("CoinGecko", "Skipping market_chart for %s: safe DRAM admission threshold not met (free=%u, largest=%u)",
             symbol.c_str(), (unsigned)NetworkBudget::freeInternal(), (unsigned)NetworkBudget::largestInternalBlock());
        return false;
    }

    String upper = symbol;
    upper.toUpperCase();
    auto it = m_symbolToId.find(upper);
    String coinId = (it != m_symbolToId.end()) ? it->second : symbol;
    coinId.toLowerCase();

    const char* days = "1";
    switch (tf) {
        case Timeframe::Hourly:  days = "1";  break;
        case Timeframe::Daily:   days = "1";  break;
        case Timeframe::Weekly:  days = "7";  break;
        case Timeframe::Monthly: days = "30"; break;
    }

    String vsCur = m_currency;
    vsCur.toLowerCase();
    if (vsCur.isEmpty()) vsCur = "usd";

    String url = "https://api.coingecko.com/api/v3/coins/" + coinId + "/market_chart?vs_currency=" + vsCur + "&days=" + String(days);
    auto res = net::SecureHttpClient::get(url);
    if (res.ok() && parseMarketChart(res.stream(), tf, outPoints, maxPoints, outCount, outMin, outMax)) {
        return true;
    }
    return false;
}

bool CoinGeckoProvider::parseMarketChart(Stream& stream, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    return parseMarketChart(stream, Timeframe::Daily, outPoints, maxPoints, outCount, outMin, outMax);
}

bool CoinGeckoProvider::parseMarketChart(Stream& stream, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    // Scan stream for "\"prices\":"
    const char* target = "\"prices\":";
    size_t targetIdx = 0;
    while (stream.available() > 0) {
        int c = stream.read();
        if ((char)c == target[targetIdx]) {
            targetIdx++;
            if (target[targetIdx] == '\0') break;
        } else {
            targetIdx = ((char)c == target[0]) ? 1 : 0;
        }
    }
    if (target[targetIdx] != '\0') return false;

    // Scan until '['
    while (stream.available() > 0) {
        int c = stream.read();
        if (c == '[') break;
    }

    constexpr size_t MAX_RAW_PRICES = 320;
    float rawPrices[MAX_RAW_PRICES];
    size_t n = 0;

    // Stream-parse [timestamp, price] pairs
    while (stream.available() > 0) {
        int c = stream.read();
        if (c == ']') break;
        if (c == '[') {
            // Read until comma
            while (stream.available() > 0) {
                int ch = stream.read();
                if (ch == ',') break;
            }
            // Read price float until ']'
            String numStr = "";
            while (stream.available() > 0) {
                int ch = stream.read();
                if (ch == ']') break;
                if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == 'e' || ch == 'E') {
                    numStr += (char)ch;
                }
            }
            float val = numStr.toFloat();
            if (val > 0.0f && n < MAX_RAW_PRICES) {
                rawPrices[n++] = val;
            }
        }
    }

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
        float val = rawPrices[i];
        outPoints[outCount++] = val;
        if (val < outMin) outMin = val;
        if (val > outMax) outMax = val;
    }

    return (outCount > 0 && outMin <= outMax);
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

    constexpr size_t MAX_RAW_PRICES = 320;
    float rawPrices[MAX_RAW_PRICES];
    size_t n = 0;

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
            if (val > 0.0f && n < MAX_RAW_PRICES) {
                rawPrices[n++] = val;
            }
        }
        cur = closeBracket + 1;
        while (cur < len && (payload[cur] == ' ' || payload[cur] == '\n' || payload[cur] == '\r' || payload[cur] == '\t')) {
            cur++;
        }
        if (cur < len && payload[cur] == ']') break;
    }

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
        float val = rawPrices[i];
        outPoints[outCount++] = val;
        if (val < outMin) outMin = val;
        if (val > outMax) outMax = val;
    }

    return (outCount > 0 && outMin <= outMax);
}
