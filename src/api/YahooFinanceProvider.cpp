#include "YahooFinanceProvider.h"
#include "../core/Logger.h"
#include "../core/net/SecureHttpClient.h"
#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

static bool isCryptoSymbol(const String& sym) {
    if (sym.endsWith("-USD") || sym.endsWith("USDT")) return true;
    static const char* const CRYPTO_LIST[] = {
        "BTC", "ETH", "SOL", "BNB", "XRP", "DOGE", "ADA", "AVAX", "DOT", "LINK",
        "TRX", "MATIC", "POL", "SHIB", "LTC", "BCH", "UNI", "NEAR", "APT", "ATOM",
        "XLM", "XMR", "FIL", "ICP", "HBAR", "VET", "ALGO", "PEPE", "SUI", "RENDER"
    };
    for (const char* c : CRYPTO_LIST) {
        if (sym.equalsIgnoreCase(c)) return true;
    }
    return false;
}

bool YahooFinanceProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    return fetchQuote(symbol, outPrice, outChange, outImageUrl, net::OWNER_ANY);
}

bool YahooFinanceProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl, uint8_t ownerId) {
    net::SecureHttpOptions options;
    options.ownerId = ownerId;
    options.requestTimeoutMs = 3000;
    options.handshakeTimeoutSec = 4;

    String cleanSym = symbol;
    cleanSym.trim();
    cleanSym.toUpperCase();
    String querySym = cleanSym;
    if (isCryptoSymbol(cleanSym) && !cleanSym.endsWith("-USD") && cleanSym.indexOf('-') == -1) {
        querySym = cleanSym + "-USD";
    }

    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + querySym + "?interval=1d&range=1d";
    auto res = net::SecureHttpClient::get(url, options);
    if (!res.ok()) {
        String fallbackUrl = "https://query2.finance.yahoo.com/v8/finance/chart/" + querySym + "?interval=1d&range=1d";
        res = net::SecureHttpClient::get(fallbackUrl, options);
    }

    if (res.ok() && parsePayload(res.stream(), outPrice, outChange)) {
        String lowerSymbol = cleanSym;
        lowerSymbol.toLowerCase();
        outImageUrl = isCryptoSymbol(cleanSym) ? "" : ("https://eodhd.com/img/logos/US/" + lowerSymbol + ".png");
        return true;
    }
    return false;
}

bool YahooFinanceProvider::parsePayload(Stream& stream, float& outPrice, float& outChange) {
    StaticJsonDocument<256> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["previousClose"] = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
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

bool YahooFinanceProvider::parsePayload(const String& payload, float& outPrice, float& outChange) {
    StaticJsonDocument<256> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["previousClose"] = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
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

    String cleanSym = symbol;
    cleanSym.trim();
    cleanSym.toUpperCase();
    if (cleanSym.isEmpty()) return false;

    String querySym = cleanSym;
    if (isCryptoSymbol(cleanSym) && !cleanSym.endsWith("-USD") && cleanSym.indexOf('-') == -1) {
        querySym = cleanSym + "-USD";
    }

    const char* range = "1d";
    const char* interval = "5m";
    switch (tf) {
        case Timeframe::Hourly:  range = "1d";  interval = "2m";  break;
        case Timeframe::Daily:   range = "1d";  interval = "5m";  break;
        case Timeframe::Weekly:  range = "5d";  interval = "15m"; break;
        case Timeframe::Monthly: range = "1mo"; interval = "1d";  break;
    }

    esp_task_wdt_reset();
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + querySym + "?interval=" + String(interval) + "&range=" + String(range);
    auto res = net::SecureHttpClient::get(url);
    if (!res.ok()) {
        esp_task_wdt_reset();
        String fallbackUrl = "https://query2.finance.yahoo.com/v8/finance/chart/" + querySym + "?interval=" + String(interval) + "&range=" + String(range);
        res = net::SecureHttpClient::get(fallbackUrl);
    }
    esp_task_wdt_reset();

    if (res.ok() && parseChart(res.stream(), outPoints, maxPoints, outCount, outMin, outMax)) {
        return true;
    }
    return false;
}

bool YahooFinanceProvider::parseChart(Stream& stream, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    StaticJsonDocument<256> filter;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    if (!err) {
        JsonArray closes = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"].as<JsonArray>();
        if (closes.isNull()) return false;

        outCount = 0;
        outMin = 1e9f;
        outMax = -1e9f;

        size_t n = closes.size();
        if (n == 0) return false;

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

bool YahooFinanceProvider::parseChart(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
    if (!outPoints || maxPoints == 0) return false;

    StaticJsonDocument<256> filter;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (!err) {
        JsonArray closes = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"].as<JsonArray>();
        if (closes.isNull()) return false;

        outCount = 0;
        outMin = 1e9f;
        outMax = -1e9f;

        size_t n = closes.size();
        if (n == 0) return false;

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

bool YahooFinanceProvider::parseQuoteAndChart(Stream& stream, float& outPrice, float& outChange,
                                              float* outPoints, size_t maxPoints, size_t& outCount,
                                              float& outMin, float& outMax) {
    StaticJsonDocument<384> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["previousClose"] = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    if (err) {
        LOGW("Yahoo", "parseQuoteAndChart JSON deserialize failed: %s", err.c_str());
        return false;
    }

    JsonObject meta = doc["chart"]["result"][0]["meta"];
    if (meta.isNull()) return false;

    outPrice = meta["regularMarketPrice"] | 0.0f;
    float prevClose = meta["previousClose"] | meta["chartPreviousClose"] | outPrice;
    if (prevClose > 0.0f && outPrice > 0.0f) {
        outChange = ((outPrice - prevClose) / prevClose) * 100.0f;
    } else {
        outChange = 0.0f;
    }

    if (outPoints && maxPoints > 0) {
        JsonArray closes = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"].as<JsonArray>();
        outCount = 0;
        outMin = 1e9f;
        outMax = -1e9f;
        if (!closes.isNull()) {
            size_t n = closes.size();
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
        }
    }

    return (outPrice > 0.0f);
}

bool YahooFinanceProvider::parseQuoteAndChart(const String& payload, float& outPrice, float& outChange,
                                              float* outPoints, size_t maxPoints, size_t& outCount,
                                              float& outMin, float& outMax) {
    StaticJsonDocument<384> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["previousClose"] = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (err) {
        LOGW("Yahoo", "parseQuoteAndChart JSON deserialize failed: %s", err.c_str());
        return false;
    }

    JsonObject meta = doc["chart"]["result"][0]["meta"];
    if (meta.isNull()) return false;

    outPrice = meta["regularMarketPrice"] | 0.0f;
    float prevClose = meta["previousClose"] | meta["chartPreviousClose"] | outPrice;
    if (prevClose > 0.0f && outPrice > 0.0f) {
        outChange = ((outPrice - prevClose) / prevClose) * 100.0f;
    } else {
        outChange = 0.0f;
    }

    if (outPoints && maxPoints > 0) {
        JsonArray closes = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"].as<JsonArray>();
        outCount = 0;
        outMin = 1e9f;
        outMax = -1e9f;
        if (!closes.isNull()) {
            size_t n = closes.size();
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
        }
    }

    return (outPrice > 0.0f);
}

bool YahooFinanceProvider::fetchQuoteAndHistory(const String& symbol,
    float& outPrice, float& outChange, String& outImageUrl,
    Timeframe tf, float* outPoints, size_t maxPoints,
    size_t& outCount, float& outMin, float& outMax)
{
    String cleanSym = symbol;
    cleanSym.trim();
    cleanSym.toUpperCase();
    if (cleanSym.isEmpty()) return false;

    String querySym = cleanSym;
    if (isCryptoSymbol(cleanSym) && !cleanSym.endsWith("-USD") && cleanSym.indexOf('-') == -1) {
        querySym = cleanSym + "-USD";
    }

    const char* range = "1d";
    const char* interval = "5m";
    switch (tf) {
        case Timeframe::Hourly:  range = "1d";  interval = "2m";  break;
        case Timeframe::Daily:   range = "1d";  interval = "5m";  break;
        case Timeframe::Weekly:  range = "5d";  interval = "15m"; break;
        case Timeframe::Monthly: range = "1mo"; interval = "1d";  break;
    }

    String path = "/v8/finance/chart/" + querySym + "?interval=" + String(interval) + "&range=" + String(range);
    String url = "https://query1.finance.yahoo.com" + path;

    esp_task_wdt_reset();
    auto res = net::SecureHttpClient::get(url);
    if (!res.ok()) {
        esp_task_wdt_reset();
        String fallbackUrl = "https://query2.finance.yahoo.com" + path;
        res = net::SecureHttpClient::get(fallbackUrl);
    }
    esp_task_wdt_reset();

    if (res.ok() && parseQuoteAndChart(res.stream(), outPrice, outChange, outPoints, maxPoints, outCount, outMin, outMax)) {
        String lowerSymbol = cleanSym;
        lowerSymbol.toLowerCase();
        outImageUrl = isCryptoSymbol(cleanSym) ? "" : ("https://eodhd.com/img/logos/US/" + lowerSymbol + ".png");
        LOGI("Yahoo", "[Combined] Quote for %s: %.2f (%.2f%%) + %d history points (%s/%s)",
             cleanSym.c_str(), outPrice, outChange, (int)outCount, range, interval);
        return true;
    }

    LOGW("Yahoo", "[Combined] Fetch failed for %s", cleanSym.c_str());
    return false;
}

bool YahooFinanceProvider::fetchQuotes(const std::vector<String>& symbols, std::map<String, StockQuote>& outQuotes) {
    return fetchQuotes(symbols, outQuotes, net::OWNER_ANY);
}

bool YahooFinanceProvider::fetchQuotes(const std::vector<String>& symbols, std::map<String, StockQuote>& outQuotes, uint8_t ownerId) {
    (void)ownerId;
    if (symbols.empty()) return false;

    net::SecureHttpSession session("query1.finance.yahoo.com");
    bool anyOk = false;

    for (const auto& sym : symbols) {
        String cleanSym = sym;
        cleanSym.trim();
        cleanSym.toUpperCase();
        if (cleanSym.isEmpty()) continue;

        String querySym = cleanSym;
        if (isCryptoSymbol(cleanSym) && !cleanSym.endsWith("-USD") && cleanSym.indexOf('-') == -1) {
            querySym = cleanSym + "-USD";
        }

        String path = "/v8/finance/chart/" + querySym + "?interval=1d&range=1d";
        auto res = session.get(path);
        
        if (!res.ok()) {
            session.close(); // Cleanly close session 1 to avoid dual TLS sessions concurrently
            net::SecureHttpSession session2("query2.finance.yahoo.com");
            auto res2 = session2.get(path);
            if (res2.ok()) {
                float price = 0.0f, change = 0.0f;
                if (parsePayload(res2.stream(), price, change)) {
                    StockQuote q;
                    q.price = price;
                    q.change24h = change;
                    String lowerSymbol = cleanSym;
                    lowerSymbol.toLowerCase();
                    q.imageUrl = isCryptoSymbol(cleanSym) ? "" : ("https://eodhd.com/img/logos/US/" + lowerSymbol + ".png");
                    q.valid = true;
                    outQuotes[cleanSym] = q;
                    anyOk = true;
                    LOGI("Yahoo", "[Batch] Quote for %s (%s): %.2f (%.2f%%)", cleanSym.c_str(), querySym.c_str(), price, change);
                }
                res2.consume();
            }
            continue;
        }

        float price = 0.0f, change = 0.0f;
        if (parsePayload(res.stream(), price, change)) {
            StockQuote q;
            q.price = price;
            q.change24h = change;
            String lowerSymbol = cleanSym;
            lowerSymbol.toLowerCase();
            q.imageUrl = isCryptoSymbol(cleanSym) ? "" : ("https://eodhd.com/img/logos/US/" + lowerSymbol + ".png");
            q.valid = true;
            outQuotes[cleanSym] = q;
            anyOk = true;
            LOGI("Yahoo", "[Batch] Quote for %s (%s): %.2f (%.2f%%)", cleanSym.c_str(), querySym.c_str(), price, change);
        }
        res.consume();
    }

    return anyOk;
}

bool YahooFinanceProvider::fetchHistories(const std::vector<String>& symbols, Timeframe tf, std::map<String, StockHistoryData>& outHistories) {
    if (symbols.empty()) return false;
    bool anyOk = false;

    for (const auto& sym : symbols) {
        String cleanSym = sym;
        cleanSym.trim();
        cleanSym.toUpperCase();
        if (cleanSym.isEmpty()) continue;

        StockHistoryData h;
        if (fetchHistory(cleanSym, tf, h.points, 64, h.count, h.minPrice, h.maxPrice)) {
            h.valid = true;
            outHistories[cleanSym] = h;
            anyOk = true;
            LOGI("Yahoo", "[Batch History] Fetched %d points for %s", (int)h.count, cleanSym.c_str());
        }
    }

    return anyOk;
}

