#pragma once
#include <Arduino.h>
#include "Timeframe.h"

class ICryptoProvider {
public:
    virtual ~ICryptoProvider() = default;
    
    /**
     * @brief Fetches the current quote for a cryptocurrency symbol.
     * @param symbol The symbol to fetch (e.g. "BTC").
     * @param outPrice The fetched price in USD.
     * @param outChange The 24h percentage change.
     * @param outImageUrl The URL to the image icon.
     * @return true if successful, false otherwise.
     */
    virtual bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) = 0;

    /**
     * @brief Fetches historical price series for sparkline chart rendering.
     */
    virtual bool fetchHistory(const String& symbol, Timeframe tf, float* outPoints, size_t maxPoints, size_t& outCount, float& outMin, float& outMax) {
        (void)symbol; (void)tf; (void)outPoints; (void)maxPoints; (void)outCount; (void)outMin; (void)outMax;
        return false;
    }

    virtual void setCurrency(const String& currency) {
        m_currency = currency;
        m_currency.toUpperCase();
    }

protected:
    String m_currency = "USD";
};
