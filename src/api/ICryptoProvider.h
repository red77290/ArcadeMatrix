#pragma once
#include <Arduino.h>

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
};
