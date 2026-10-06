#pragma once
#include <Arduino.h>
#include "Timeframe.h"

#include <vector>
#include <map>

struct CryptoQuote {
    float price = 0.0f;
    float change24h = 0.0f;
    String imageUrl = "";
    bool valid = false;
};

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
     * @brief Fetches quotes for multiple cryptocurrency symbols in a single batch.
     * @param symbols List of symbols (e.g. {"BTC", "ETH", "SOL"}).
     * @param outQuotes Map of uppercase symbol -> CryptoQuote.
     * @return true if at least one quote was fetched successfully.
     */
    virtual bool fetchQuotes(const std::vector<String>& symbols, std::map<String, CryptoQuote>& outQuotes) {
        bool any = false;
        for (const auto& sym : symbols) {
            CryptoQuote q;
            if (fetchQuote(sym, q.price, q.change24h, q.imageUrl)) {
                q.valid = (q.price > 0.0f);
                if (q.valid) {
                    String upper = sym;
                    upper.toUpperCase();
                    outQuotes[upper] = q;
                    any = true;
                }
            }
        }
        return any;
    }

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
