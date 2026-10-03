#pragma once
#include "../api/ICryptoProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <map>

class CoinGeckoProvider : public ICryptoProvider {
public:
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override;
    bool fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) override;
    
    // Public parsing methods for TDD & Direct Streaming
    bool parsePrimary(Stream& stream, float& outPrice, float& outChange, String& outImageUrl, String* outId = nullptr);
    bool parsePrimary(const String& payload, float& outPrice, float& outChange, String& outImageUrl, String* outId = nullptr);
    bool parseSimple(Stream& stream, const String& coinId, float& outPrice, float& outChange);
    bool parseSimple(const String& payload, const String& coinId, float& outPrice, float& outChange);
    bool parseMarketChart(Stream& stream, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
    bool parseMarketChart(Stream& stream, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
    bool parseMarketChart(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
    bool parseMarketChart(const String& payload, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);

private:
    std::map<String, String> m_symbolToId;
};
