#include "BinanceProvider.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

bool BinanceProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    // Refuse the handshake rather than let mbedTLS fail with -32512 and fragment the
    // internal heap further. A failed attempt still costs allocations, CPU on Core 0
    // and SD bus time, so retrying blindly makes the starvation worse.
    if (!NetworkBudget::canStartTlsSession()) {
        LOGW("Binance", "Skipping quote for %s: insufficient internal DRAM for TLS (free=%u, largest=%u).",
             symbol.c_str(), (unsigned)NetworkBudget::freeInternal(), (unsigned)NetworkBudget::largestInternalBlock());
        return false;
    }

    NetworkBudget::ScopedTlsHandshakeLock tlsLock;
    if (!tlsLock) {
        LOGW("Binance", "Skipping quote for %s: another TLS handshake is in progress.", symbol.c_str());
        return false;
    }

    String apiSymbol = symbol;
    String quotePair = m_currency;
    quotePair.toUpperCase();
    if (quotePair.isEmpty() || quotePair == "USD") {
        quotePair = "USDT";
    }

    if (!apiSymbol.endsWith(quotePair)) {
        apiSymbol += quotePair;
    }
    String binanceUrl = "https://api.binance.com/api/v3/ticker/24hr?symbol=" + apiSymbol;
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(4);

    HTTPClient http;
    http.setTimeout(4500);
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

    String apiSymbol = symbol;
    String quotePair = m_currency;
    quotePair.toUpperCase();
    if (quotePair.isEmpty() || quotePair == "USD") {
        quotePair = "USDT";
    }

    if (!apiSymbol.endsWith(quotePair)) {
        apiSymbol += quotePair;
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

    if (!NetworkBudget::canStartTlsSession()) {
        LOGW("Binance", "Skipping history for %s: insufficient internal DRAM for TLS.", symbol.c_str());
        return false;
    }

    NetworkBudget::ScopedTlsHandshakeLock tlsLock;
    if (!tlsLock) {
        LOGW("Binance", "Skipping history for %s: another TLS handshake is in progress.", symbol.c_str());
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(4);

    HTTPClient http;
    http.setTimeout(4500);
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

bool BinanceProvider::fetchQuoteAndHistory(const String& symbol,
    float& outPrice, float& outChange, String& outImageUrl,
    Timeframe tf, float* outPoints, size_t maxPoints,
    size_t& outCount, float& outMin, float& outMax)
{
    if (!NetworkBudget::canStartTlsSession()) {
        LOGW("Binance", "Skipping combined fetch for %s: insufficient internal DRAM for TLS (free=%u, largest=%u).",
             symbol.c_str(), (unsigned)NetworkBudget::freeInternal(), (unsigned)NetworkBudget::largestInternalBlock());
        return false;
    }

    NetworkBudget::ScopedTlsHandshakeLock tlsLock;
    if (!tlsLock) {
        LOGW("Binance", "Skipping combined fetch for %s: another TLS handshake is in progress.", symbol.c_str());
        return false;
    }

    // Build API symbol (e.g. BTC -> BTCUSDT)
    String apiSymbol = symbol;
    String quotePair = m_currency;
    quotePair.toUpperCase();
    if (quotePair.isEmpty() || quotePair == "USD") {
        quotePair = "USDT";
    }
    if (!apiSymbol.endsWith(quotePair)) {
        apiSymbol += quotePair;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(4);

    HTTPClient http;
    http.setTimeout(4500);
    http.setUserAgent("Mozilla/5.0 ArcadeMatrix/4.0");
    http.setReuse(true);  // HTTP/1.1 keepalive: reuse TLS session across requests

    bool quoteOk = false;

    // ── Request 1: Quote (ticker/24hr) ──
    String quoteUrl = "https://api.binance.com/api/v3/ticker/24hr?symbol=" + apiSymbol;
    if (http.begin(client, quoteUrl)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            quoteOk = parsePayload(payload, outPrice, outChange);
            if (quoteOk) {
                LOGI("Binance", "[Combined] Quote for %s: %.4f (%.2f%%)", symbol.c_str(), outPrice, outChange);
            }
        }
        http.end();  // Closes HTTP request, NOT the TLS socket (reuse=true)
    }

    esp_task_wdt_reset();

    // ── Request 2: History (klines) on the SAME TLS session ──
    if (quoteOk && outPoints && maxPoints > 0 && client.connected()) {
        const char* interval = "1h";
        int limit = 24;
        switch (tf) {
            case Timeframe::Hourly:  interval = "1m"; limit = 60; break;
            case Timeframe::Daily:   interval = "1h"; limit = 24; break;
            case Timeframe::Weekly:  interval = "4h"; limit = 42; break;
            case Timeframe::Monthly: interval = "1d"; limit = 30; break;
        }
        String histUrl = "https://api.binance.com/api/v3/klines?symbol=" + apiSymbol
                       + "&interval=" + String(interval) + "&limit=" + String(limit);

        if (http.begin(client, histUrl)) {
            int code = http.GET();
            if (code == 200) {
                String payload = http.getString();
                if (parseKlines(payload, outPoints, maxPoints, outCount, outMin, outMax)) {
                    LOGI("Binance", "[Combined] History for %s: %d points (%s)", symbol.c_str(), (int)outCount, interval);
                }
            }
            http.end();
        }
    }

    client.stop();  // Now close the TLS session
    return quoteOk;
}

