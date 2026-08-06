#include "YahooFinanceProvider.h"
#include "../core/Logger.h"

bool YahooFinanceProvider::fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) {
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
    
    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    int code = -1;
    if (http.begin(url)) {
        code = http.GET();
        if (code != 200) {
            http.end();
            String fallbackUrl = "https://query2.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
            if (http.begin(fallbackUrl)) {
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
                return true;
            }
        }
        http.end();
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
