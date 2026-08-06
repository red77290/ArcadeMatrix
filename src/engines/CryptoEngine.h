#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include <map>
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
class CryptoEngine {
public:
    CryptoEngine();
    
    void begin(MatrixPanel_I2S_DMA* display);
    void addProvider(ICryptoProvider* provider);
    void updateConfig(const CryptoConfig& cfg);
    void onDisplayStart();
    bool loop();

private:
    MatrixPanel_I2S_DMA* matrix;
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
    
    PNG png;
    uint16_t* currentDecodeBuffer;
    static int pngDraw(PNGDRAW *pDraw);
    static CryptoEngine* instance;
    
    void parseSymbols();
    void fetchQuote(const String& symbol);
    void renderQuote();
};
