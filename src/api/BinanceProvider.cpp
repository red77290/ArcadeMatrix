#include "BinanceProvider.h"
#include "../core/Logger.h"
#include "../core/net/SecureHttpClient.h"

static String buildApiSymbol(const String& symbol, const String& currency) {
    String apiSymbol = symbol;
    String quotePair = currency;
    quotePair.toUpperCase();
    if (quotePair.isEmpty() || quotePair == "USD") {
        quotePair = "USDT";
    }
    if (!apiSymbol.endsWith(quotePair)) {
        apiSymbol += quotePair;
    }
    return apiSymbol;
}

bool BinanceProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String apiSymbol = buildApiSymbol(symbol, m_currency);
    String url = "https://api.binance.com/api/v3/ticker/24hr?symbol=" + apiSymbol;

    auto res = net::SecureHttpClient::get(url);
    if (res.ok() && parsePayload(res.stream(), outPrice, outChange)) {
        return true;
    }
    return false;
}

bool BinanceProvider::parsePayload(Stream& stream, float& outPrice, float& outChange) {
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, stream);
    if (!err) {
        outPrice = doc["lastPrice"].as<float>();
        outChange = doc["priceChangePercent"].as<float>();
        return (outPrice > 0.0f);
    }
    return false;
}

bool BinanceProvider::parsePayload(const String& payload, float& outPrice, float& outChange) {
    StaticJsonDocument<1024> doc;
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

    String apiSymbol = buildApiSymbol(symbol, m_currency);
    const char* interval = "1h";
    int limit = 24;
    switch (tf) {
        case Timeframe::Hourly:  interval = "1m"; limit = 60; break;
        case Timeframe::Daily:   interval = "1h"; limit = 24; break;
        case Timeframe::Weekly:  interval = "4h"; limit = 42; break;
        case Timeframe::Monthly: interval = "1d"; limit = 30; break;
    }

    String url = "https://api.binance.com/api/v3/klines?symbol=" + apiSymbol + "&interval=" + String(interval) + "&limit=" + String(limit);

    auto res = net::SecureHttpClient::get(url);
    if (res.ok() && parseKlines(res.stream(), outPoints, maxPoints, outCount, outMin, outMax)) {
        return true;
    }
    return false;
}

bool BinanceProvider::parseKlines(Stream& stream, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, stream);
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

bool BinanceProvider::fetchQuoteAndHistory(const String& symbol,
    float& outPrice, float& outChange, String& outImageUrl,
    Timeframe tf, float* outPoints, size_t maxPoints,
    size_t& outCount, float& outMin, float& outMax)
{
    String apiSymbol = buildApiSymbol(symbol, m_currency);
    net::SecureHttpSession session("api.binance.com");

    // ── Request 1: Quote (ticker/24hr) ──
    auto quoteRes = session.get("/api/v3/ticker/24hr?symbol=" + apiSymbol);
    bool quoteOk = quoteRes.ok() && parsePayload(quoteRes.stream(), outPrice, outChange);
    if (quoteOk) {
        LOGI("Binance", "[Combined] Quote for %s: %.4f (%.2f%%)", symbol.c_str(), outPrice, outChange);
    }
    quoteRes.consume(); // Ensure stream consumed for clean keepalive reuse

    // ── Request 2: History (klines) on the SAME TLS session ──
    if (quoteOk && outPoints && maxPoints > 0) {
        const char* interval = "1h";
        int limit = 24;
        switch (tf) {
            case Timeframe::Hourly:  interval = "1m"; limit = 60; break;
            case Timeframe::Daily:   interval = "1h"; limit = 24; break;
            case Timeframe::Weekly:  interval = "4h"; limit = 42; break;
            case Timeframe::Monthly: interval = "1d"; limit = 30; break;
        }
        String klinesPath = "/api/v3/klines?symbol=" + apiSymbol
                          + "&interval=" + String(interval) + "&limit=" + String(limit);

        auto histRes = session.get(klinesPath);
        if (histRes.ok()) {
            if (parseKlines(histRes.stream(), outPoints, maxPoints, outCount, outMin, outMax)) {
                LOGI("Binance", "[Combined] History for %s: %d points (%s)", symbol.c_str(), (int)outCount, interval);
            }
        }
        histRes.consume();
    }

    return quoteOk;
}
