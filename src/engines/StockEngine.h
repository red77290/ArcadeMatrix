#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include <map>
#include "../../include/core/EngineContract.h"
#include "../core/ConfigLoader.h"
#include "../api/IStockProvider.h"
#include "../api/Timeframe.h"
#include "icons/CryptoStockIcons.h"
#include "renderers/SparklineRenderer.h"
#include <PNGdec.h>
#include "../core/SDUtils.h"

#ifndef ASSET_QUOTE_CACHE_H
#define ASSET_QUOTE_CACHE_H
struct AssetQuoteCache {
    float price = 0.0f;
    float changePercent24h = 0.0f;
    uint32_t lastFetchTime = 0;
    bool hasData = false;
    String imageUrl = "";
    uint16_t iconPixels[256]; // 16x16 RGB565 buffer
    bool hasIcon = false;
};
#endif

#ifndef ASSET_HISTORY_CACHE_H
#define ASSET_HISTORY_CACHE_H
struct AssetHistoryCache {
    float points[64];
    size_t count = 0;
    float minPrice = 0.0f;
    float maxPrice = 0.0f;
    uint32_t lastFetchTime = 0;
    bool hasData = false;
};
#endif

/**
 * @class StockEngine
 * @brief Displays real-time stock market quotes, % change badges, company logos, and sparklines.
 */
class StockEngine : public IEngine {
public:
    enum class DisplayPage {
        Info,
        Chart
    };

    StockEngine();
    
    EngineError initialize(EngineContext* context, const EngineConfig* engineConfig) override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void activate() override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* engineConfig) override;
    bool isFinished() const override;

    void addProvider(IStockProvider* provider);

private:
    int config_duration_sec = 5;
    bool config_enabled = true;
    int config_cache_ttl_min = 15;
    bool config_show_chart = true;
    Timeframe config_chart_timeframe = Timeframe::Daily;
    
    std::vector<String> symbolList;
    size_t currentSymbolIndex;
    size_t symbolsShownThisCycle = 0;
    uint32_t lastItemSwitchTime;
    DisplayPage currentPage = DisplayPage::Info;
    
    std::vector<IStockProvider*> providers;
    
    // Per-symbol quote cache map
    std::map<String, AssetQuoteCache> quoteCache;
    // Per-symbol history cache map
    std::map<String, AssetHistoryCache> historyCache;
    
    String activeSymbol;
    float currentPrice;
    float changePercent24h;
    bool fetchSuccess;
    String currentImageUrl;
    
    PNG* pngPtr = nullptr;
    uint16_t* currentDecodeBuffer;
    static int pngDraw(PNGDRAW *pDraw);
    static StockEngine* instance;
    
    void parseSymbols(const String& syms);
    void fetchQuote(const String& symbol);
    void fetchHistory(const String& symbol, Timeframe tf);
    void renderQuote(EngineContext* context);
    void renderChart(EngineContext* context);
};
