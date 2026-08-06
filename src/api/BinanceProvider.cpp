#include "BinanceProvider.h"
#include "../core/Logger.h"

bool BinanceProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String apiSymbol = symbol;
    if (!apiSymbol.endsWith("USDT") && !apiSymbol.endsWith("USD")) {
        apiSymbol += "USDT";
    }
    String binanceUrl = "https://api.binance.com/api/v3/ticker/24hr?symbol=" + apiSymbol;
    
    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    if (http.begin(binanceUrl)) {
        int code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            if (parsePayload(payload, outPrice, outChange)) {
                http.end();
                return true;
            }
        }
        http.end();
    }
    
    return false;
}

bool BinanceProvider::parsePayload(const String& payload, float& outPrice, float& outChange) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
        outPrice = doc["lastPrice"] | 0.0f;
        outChange = doc["priceChangePercent"] | 0.0f;
        return (outPrice > 0.0f);
    }
    return false;
}
