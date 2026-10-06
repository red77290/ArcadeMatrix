#pragma once
#include <Arduino.h>
#include "Timeframe.h"

#include <vector>
#include <map>

struct StockQuote {
    float price = 0.0f;
    float change24h = 0.0f;
    String imageUrl = "";
    bool valid = false;
};

struct StockHistoryData {
    float points[64];
    size_t count = 0;
    float minPrice = 0.0f;
    float maxPrice = 0.0f;
    bool valid = false;
};

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

    /**
     * @brief Fetches quotes for multiple stock symbols in a single batch.
     * @param symbols List of symbols (e.g. {"AAPL", "MSFT", "NVDA"}).
     * @param outQuotes Map of uppercase symbol -> StockQuote.
     * @return true if at least one quote was fetched successfully.
     */
    virtual bool fetchQuotes(const std::vector<String>& symbols, std::map<String, StockQuote>& outQuotes) {
        bool any = false;
        for (const auto& sym : symbols) {
            StockQuote q;
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

    /**
     * @brief Fetches historical price series for multiple symbols in a batch.
     * @param symbols List of symbols (e.g. {"AAPL", "MSFT", "NVDA"}).
     * @param tf Timeframe to query.
     * @param outHistories Map of uppercase symbol -> StockHistoryData.
     * @return true if at least one history was fetched successfully.
     */
    virtual bool fetchHistories(const std::vector<String>& symbols, Timeframe tf, std::map<String, StockHistoryData>& outHistories) {
        bool any = false;
        for (const auto& sym : symbols) {
            StockHistoryData h;
            if (fetchHistory(sym, tf, h.points, 64, h.count, h.minPrice, h.maxPrice)) {
                h.valid = (h.count > 0);
                if (h.valid) {
                    String upper = sym;
                    upper.toUpperCase();
                    outHistories[upper] = h;
                    any = true;
                }
            }
        }
        return any;
    }
};
