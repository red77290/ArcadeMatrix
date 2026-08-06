#pragma once
#include "../api/IStockProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class YahooFinanceProvider : public IStockProvider {
public:
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override;
    
    // Public parsing methods for TDD
    bool parsePayload(const String& payload, float& outPrice, float& outChange);
};
