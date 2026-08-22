#pragma once
#include "../api/IStockProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class YahooFinanceProvider : public IStockProvider {
public:
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override;
    bool fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) override;
    
    // Public parsing methods for TDD
    bool parsePayload(const String& payload, float& outPrice, float& outChange);
    bool parseChart(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
};
