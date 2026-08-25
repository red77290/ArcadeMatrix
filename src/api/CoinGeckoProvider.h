#pragma once
#include "../api/ICryptoProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class CoinGeckoProvider : public ICryptoProvider {
public:
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override;
    bool fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) override;
    
    // Public parsing methods for TDD
    bool parsePrimary(const String& payload, float& outPrice, float& outChange, String& outImageUrl);
    bool parseSimple(const String& payload, const String& coinId, float& outPrice, float& outChange);
    bool parseMarketChart(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
};
