#include "CoinGeckoProvider.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#include "../core/net/SecureHttpClient.h"

bool CoinGeckoProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String upperSymbol = symbol;
    upperSymbol.toUpperCase();
    upperSymbol.trim();
    std::map<String, CryptoQuote> quotes;
    if (fetchQuotes({upperSymbol}, quotes)) {
        auto it = quotes.find(upperSymbol);
        if (it != quotes.end() && it->second.valid) {
            outPrice = it->second.price;
            outChange = it->second.change24h;
            outImageUrl = it->second.imageUrl;
            return true;
        }
    }
    return false;
}

bool CoinGeckoProvider::fetchQuotes(const std::vector<String>& symbols, std::map<String, CryptoQuote>& outQuotes) {
    if (symbols.empty()) return false;
    
    String vsCur = m_currency;
    vsCur.toLowerCase();
    if (vsCur.isEmpty()) vsCur = "usd";

    String joinedSymbols = "";
    for (size_t i = 0; i < symbols.size(); ++i) {
        String sym = symbols[i];
        sym.toLowerCase();
        sym.trim();
        if (sym.isEmpty()) continue;
        if (!joinedSymbols.isEmpty()) joinedSymbols += ",";
        joinedSymbols += sym;
    }
    if (joinedSymbols.isEmpty()) return false;

    // Single batch query to CoinGecko /coins/markets
    String cgUrl = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=" + vsCur + "&symbols=" + joinedSymbols;
    auto res = net::SecureHttpClient::get(cgUrl);
    bool anyFound = false;
    if (res.ok() && parsePrimaryBatch(res.stream(), outQuotes)) {
        anyFound = !outQuotes.empty();
    }

    // Check if any requested symbols were missed
    std::vector<String> missingSymbols;
    for (const auto& s : symbols) {
        String upper = s;
        upper.toUpperCase();
        upper.trim();
        if (outQuotes.find(upper) == outQuotes.end() || !outQuotes[upper].valid) {
            missingSymbols.push_back(upper);
        }
    }

    // If some were missed and we have their IDs, try simple/price with multiple IDs
    if (!missingSymbols.empty()) {
        String joinedIds = "";
        for (const auto& sym : missingSymbols) {
            auto it = m_symbolToId.find(sym);
            if (it != m_symbolToId.end() && !it->second.isEmpty()) {
                if (!joinedIds.isEmpty()) joinedIds += ",";
                joinedIds += it->second;
            }
        }
        if (!joinedIds.isEmpty()) {
            String cgSimpleUrl = "https://api.coingecko.com/api/v3/simple/price?ids=" + joinedIds + "&vs_currencies=" + vsCur + "&include_24hr_change=true";
            auto resSimple = net::SecureHttpClient::get(cgSimpleUrl);
            if (resSimple.ok()) {
                DynamicJsonDocument doc(2048);
                if (deserializeJson(doc, resSimple.stream()) == DeserializationError::Ok) {
                    for (const auto& sym : missingSymbols) {
                        auto it = m_symbolToId.find(sym);
                        if (it != m_symbolToId.end() && doc.containsKey(it->second)) {
                            JsonObject item = doc[it->second];
                            float p = item[vsCur] | 0.0f;
                            float c = item[vsCur + "_24h_change"] | 0.0f;
                            if (p > 0.0f) {
                                CryptoQuote q;
                                q.price = p;
                                q.change24h = c;
                                q.valid = true;
                                outQuotes[sym] = q;
                                anyFound = true;
                            }
                        }
                    }
                }
            }
        }
    }

    return anyFound;
}

bool CoinGeckoProvider::parsePrimaryBatch(Stream& stream, std::map<String, CryptoQuote>& outQuotes) {
    StaticJsonDocument<256> filter;
    filter[0]["symbol"] = true;
    filter[0]["current_price"] = true;
    filter[0]["price_change_percentage_24h"] = true;
    filter[0]["image"] = true;
    filter[0]["id"] = true;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    if (!err && doc.is<JsonArray>()) {
        for (JsonObject coin : doc.as<JsonArray>()) {
            String sym = coin["symbol"].as<String>();
            sym.toUpperCase();
            CryptoQuote q;
            q.price = coin["current_price"] | 0.0f;
            q.change24h = coin["price_change_percentage_24h"] | 0.0f;
            q.imageUrl = coin["image"].as<String>();
            q.valid = (q.price > 0.0f);
            if (q.valid) {
                outQuotes[sym] = q;
                if (coin.containsKey("id")) {
                    m_symbolToId[sym] = coin["id"].as<String>();
                }
            }
        }
        return !outQuotes.empty();
    }
    return false;
}

bool CoinGeckoProvider::parsePrimaryBatch(const String& payload, std::map<String, CryptoQuote>& outQuotes) {
    StaticJsonDocument<256> filter;
    filter[0]["symbol"] = true;
    filter[0]["current_price"] = true;
    filter[0]["price_change_percentage_24h"] = true;
    filter[0]["image"] = true;
    filter[0]["id"] = true;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (!err && doc.is<JsonArray>()) {
        for (JsonObject coin : doc.as<JsonArray>()) {
            String sym = coin["symbol"].as<String>();
            sym.toUpperCase();
            CryptoQuote q;
            q.price = coin["current_price"] | 0.0f;
            q.change24h = coin["price_change_percentage_24h"] | 0.0f;
            q.imageUrl = coin["image"].as<String>();
            q.valid = (q.price > 0.0f);
            if (q.valid) {
                outQuotes[sym] = q;
                if (coin.containsKey("id")) {
                    m_symbolToId[sym] = coin["id"].as<String>();
                }
            }
        }
        return !outQuotes.empty();
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
