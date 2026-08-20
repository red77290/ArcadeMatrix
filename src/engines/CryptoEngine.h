#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include <map>
#include "../../include/core/EngineContract.h"
#include "../core/ConfigLoader.h"
#include "../api/ICryptoProvider.h"
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
 * @class CryptoEngine
 * @brief Displays real-time crypto prices, 24h % change badges, and pixel-art logos.
 */
class CryptoEngine : public IEngine {
public:
    CryptoEngine();
    
    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;

    void addProvider(ICryptoProvider* provider);

private:
    CryptoConfig config;
    
    std::vector<String> symbolList;
    size_t currentSymbolIndex;
    uint32_t lastItemSwitchTime;
    uint32_t lastFetchTime;
    
    std::vector<ICryptoProvider*> providers;
    
    // Per-symbol quote cache map
    std::map<String, AssetQuoteCache> quoteCache;
    
    // Currently displayed market quote
    String activeSymbol;
    float currentPrice;
    float changePercent24h;
    bool fetchSuccess;
    String currentImageUrl;
    
    PNG* pngPtr = nullptr;
    uint16_t* currentDecodeBuffer;
    static int pngDraw(PNGDRAW *pDraw);
    static CryptoEngine* instance;
    
    void parseSymbols();
    void fetchQuote(const String& symbol);
    void renderQuote(EngineContext* context);
};
