#include "CoinGeckoProvider.h"
#include "../core/Logger.h"

bool CoinGeckoProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String lowerSymbol = symbol;
    lowerSymbol.toLowerCase();
    
    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    // Primary API
    String cgUrl = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&symbols=" + lowerSymbol;
    if (http.begin(cgUrl)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            if (parsePrimary(payload, outPrice, outChange, outImageUrl)) {
                http.end();
                return true;
            }
        }
        http.end();
    }
    
    // Simple API fallback
    String coinId = lowerSymbol;
    if (lowerSymbol == "erg") coinId = "ergo";
    
    String cgSimpleUrl = "https://api.coingecko.com/api/v3/simple/price?ids=" + coinId + "&vs_currencies=usd&include_24hr_change=true";
    if (http.begin(cgSimpleUrl)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            if (parseSimple(payload, coinId, outPrice, outChange)) {
                http.end();
                return true;
            }
        }
        http.end();
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
