#pragma once
#include <Arduino.h>

class IStockProvider {
public:
    virtual ~IStockProvider() = default;
    
    /**
     * @brief Fetches the current quote for a stock symbol.
     * @param symbol The symbol to fetch (e.g. "AAPL").
     * @param outPrice The fetched price in USD.
     * @param outChange The 24h percentage change.
     * @param outImageUrl The URL to the image icon.
     * @return true if successful, false otherwise.
     */
    virtual bool fetchQuote(const String& symbol, float& outPrice, float& outChange, String& outImageUrl) = 0;
};
