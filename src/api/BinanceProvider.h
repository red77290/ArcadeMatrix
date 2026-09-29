#pragma once
#include "../api/ICryptoProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class BinanceProvider : public ICryptoProvider {
public:
    bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) override;
    bool fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) override;
    
    /**
     * @brief Combined quote + history fetch in a single TLS session via HTTP/1.1 keepalive.
     *
     * Critical for ESP32 STD (no PSRAM): each TLS handshake permanently fragments the largest
     * contiguous DRAM block from ~51 KB to ~47 KB.  A second TLS session then crashes with
     * mbedTLS -17040.  By reusing the same WiFiClientSecure for both GET requests, only ONE
     * handshake is needed.  History output parameters are optional (pass nullptr for outPoints
     * to skip history).
     *
     * @return true if at least the quote was fetched successfully.
     */
    bool fetchQuoteAndHistory(const String& symbol,
        float& outPrice, float& outChange, String& outImageUrl,
        Timeframe tf, float* outPoints, size_t maxPoints,
        size_t& outCount, float& outMin, float& outMax);

    // Public parsing methods for TDD
    bool parsePayload(const String& payload, float& outPrice, float& outChange);
    bool parseKlines(const String& payload, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax);
};
