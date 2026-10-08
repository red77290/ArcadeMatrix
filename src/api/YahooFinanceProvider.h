#pragma once
#include "../api/IStockProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class YahooFinanceProvider : public IStockProvider {
public:
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override;
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl, uint8_t ownerId);
    bool fetchQuotes(const std::vector<String>& symbols, std::map<String, StockQuote>& outQuotes) override;
    bool fetchQuotes(const std::vector<String>& symbols, std::map<String, StockQuote>& outQuotes, uint8_t ownerId);
    bool fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) override;
    bool fetchHistories(const std::vector<String>& symbols, Timeframe tf, std::map<String, StockHistoryData>& outHistories) override;
    
    /**
     * @brief Combined quote + history fetch in a single TLS session via HTTP/1.1 keepalive.
     *
     * Critical for ESP32 STD (no PSRAM): reuses the same TLS session for both chart/1d quote
     * and chart/tf history requests, avoiding post-TLS fragmentation that prevents a 2nd session.
     *
     * @return true if at least the quote was fetched successfully.
     */
    bool fetchQuoteAndHistory(const String& symbol,
        float& outPrice, float& outChange, String& outImageUrl,
        Timeframe tf, float* outPoints, size_t maxPoints,
        size_t& outCount, float& outMin, float& outMax);
    
    // Public parsing methods for TDD & Streaming
    bool parsePayload(const String& payload, float& outPrice, float& outChange);
    bool parsePayload(Stream& stream, float& outPrice, float& outChange);
    bool parseChart(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
    bool parseChart(Stream& stream, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
    bool parseQuoteAndChart(const String& payload, float& outPrice, float& outChange, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
    bool parseQuoteAndChart(Stream& stream, float& outPrice, float& outChange, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
};
