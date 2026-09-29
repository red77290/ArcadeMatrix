#pragma once
#include "../api/IStockProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class YahooFinanceProvider : public IStockProvider {
public:
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override;
    bool fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) override;
    
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
};
