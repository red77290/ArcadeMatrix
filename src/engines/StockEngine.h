#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include <map>
#include "../core/ConfigLoader.h"
#include "../api/IStockProvider.h"
#include "icons/CryptoStockIcons.h"
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

/**
 * @class StockEngine
 * @brief Displays real-time stock market quotes, % change badges, and company logos.
 */
class StockEngine {
public:
    StockEngine();
    
    void begin(MatrixPanel_I2S_DMA* display);
    void addProvider(IStockProvider* provider);
    void updateConfig(const StockConfig& cfg);
    void onDisplayStart();
    bool loop();

private:
    MatrixPanel_I2S_DMA* matrix;
    StockConfig config;
    
    std::vector<String> symbolList;
    size_t currentSymbolIndex;
    uint32_t lastItemSwitchTime;
    
    std::vector<IStockProvider*> providers;
    
    // Per-symbol quote cache map
    std::map<String, AssetQuoteCache> quoteCache;
    
    String activeSymbol;
    float currentPrice;
    float changePercent24h;
    bool fetchSuccess;
    String currentImageUrl;
    
    PNG* pngPtr = nullptr;
    uint16_t* currentDecodeBuffer;
    static int pngDraw(PNGDRAW *pDraw);
    static StockEngine* instance;
    
    void parseSymbols();
    void fetchQuote(const String& symbol);
    void renderQuote();
};
