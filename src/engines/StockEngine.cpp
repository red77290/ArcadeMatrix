#include "StockEngine.h"
#include "../core/Logger.h"
#include <HTTPClient.h>

StockEngine* StockEngine::instance = nullptr;

StockEngine::StockEngine() 
    : currentSymbolIndex(0), lastItemSwitchTime(0),
      currentPrice(0.0f), changePercent24h(0.0f), fetchSuccess(false), currentDecodeBuffer(nullptr) {
    instance = this;
}

EngineError StockEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

void StockEngine::addProvider(IStockProvider* provider) {
    if (provider) {
        providers.push_back(provider);
    }
}

extern ConfigLoader config;

void StockEngine::onConfigChanged(const EngineConfig* engineConfig) {
    this->config = ::config.stock;
    parseSymbols();
}

void StockEngine::parseSymbols() {
    symbolList.clear();
    String syms = config.symbols;
    int start = 0;
    int comma = 0;
    while ((comma = syms.indexOf(',', start)) != -1) {
        String s = syms.substring(start, comma);
        s.trim();
        s.toUpperCase();
        if (s.length() > 0) symbolList.push_back(s);
        start = comma + 1;
    }
    String s = syms.substring(start);
    s.trim();
    s.toUpperCase();
    if (s.length() > 0) symbolList.push_back(s);
    
    if (symbolList.empty()) {
        symbolList.push_back("AAPL");
        symbolList.push_back("NVDA");
        symbolList.push_back("TSLA");
        symbolList.push_back("MSFT");
    }
}

void StockEngine::activate() {
    lastItemSwitchTime = millis();
    if (!symbolList.empty()) {
        fetchQuote(symbolList[currentSymbolIndex % symbolList.size()]);
    }
}

void StockEngine::deactivate() {
}

void StockEngine::fetchQuote(const String& symbol) {
    if (ESP.getPsramSize() == 0) {
        LOGW("StockEngine", "PSRAM required for HTTPS Stock fetches. Skipping.");
        activeSymbol = "N/A";
        currentPrice = 0.0f;
        changePercent24h = 0.0f;
        fetchSuccess = false;
        return;
    }
    
    fetchSuccess = false;
    
    uint32_t now = millis();
    AssetQuoteCache& cache = quoteCache[symbol];
    
    uint32_t ttlMs = (config.cache_ttl_min > 0 ? config.cache_ttl_min : 1) * 60 * 1000;
    
    // 1. Check if cache is fresh (< config.cache_ttl_min minutes old)
    if (cache.hasData && (now - cache.lastFetchTime < ttlMs)) {
        currentPrice = cache.price;
        changePercent24h = cache.changePercent24h;
        fetchSuccess = true;
        LOGI("StockEngine", "[Cache Hit] Using cached stock quote for %s: $%.2f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
        return;
    }
    
    float newPrice = 0.0f;
    float newChange = 0.0f;
    String newImgUrl = "";
    bool fetched = false;
    
    // Try each provider until one succeeds
    for (IStockProvider* provider : providers) {
        if (provider->fetchQuote(symbol, newPrice, newChange, newImgUrl)) {
            fetched = true;
            break;
        }
    }
    
    // Download and Cache Icon
    if (fetched && newImgUrl.length() > 0 && !cache.hasIcon) {
        String sdPath = "/stock_icons/" + symbol + ".png";
        if (!sd.exists(sdPath)) {
            String proxyUrl = "https://wsrv.nl/?url=" + newImgUrl + "&w=16&h=16&output=png";
            HTTPClient httpImg;
            httpImg.setTimeout(5000);
            if (httpImg.begin(proxyUrl)) {
                int code = httpImg.GET();
                if (code == 200) {
                    if (!sd.exists("/stock_icons")) sd.mkdir("/stock_icons");
                    FsFile f = sd.open(sdPath, FILE_OPEN_WRITE);
                    if (f) {
                        httpImg.writeToStream(&f);
                        f.close();
                    }
                }
                httpImg.end();
            }
        }
        
        // Load into RAM
        if (sd.exists(sdPath)) {
            FsFile f = sd.open(sdPath, FILE_OPEN_READ);
            if (f) {
                size_t size = f.size();
                uint8_t* buf = (uint8_t*)malloc(size);
                if (buf) {
                    f.read(buf, size);
                    f.close();
                    
                    memset(cache.iconPixels, 0, sizeof(cache.iconPixels));
                    currentDecodeBuffer = cache.iconPixels;
                    PNG* png = new PNG();
                    pngPtr = png;
                    int rc = png->openRAM(buf, size, pngDraw);
                    if (rc == PNG_SUCCESS) {
                        png->decode(NULL, 0);
                        cache.hasIcon = true;
                    }
                    png->close();
                    delete png;
                    pngPtr = nullptr;
                    free(buf);
                    currentDecodeBuffer = nullptr;
                } else {
                    f.close();
                }
            }
        }
    }
    
    // Update cache if successful
    if (fetched && newPrice > 0.0f) {
        cache.price = newPrice;
        cache.changePercent24h = newChange;
        cache.imageUrl = newImgUrl;
        cache.lastFetchTime = now;
        cache.hasData = true;
        
        currentPrice = newPrice;
        changePercent24h = newChange;
        fetchSuccess = true;
        LOGI("StockEngine", "[Fetch Success] Updated cache for %s: $%.2f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
    } else if (cache.hasData) {
        // Fallback to last known cached price for THIS symbol if HTTP failed
        currentPrice = cache.price;
        changePercent24h = cache.changePercent24h;
        fetchSuccess = true;
        LOGW("StockEngine", "[HTTP Failed/429] Reusing last known cached price for %s: $%.2f", symbol.c_str(), currentPrice);
    } else {
        currentPrice = 0.0f;
        changePercent24h = 0.0f;
        fetchSuccess = false;
        LOGW("StockEngine", "No quote available for %s", symbol.c_str());
    }
}

int StockEngine::pngDraw(PNGDRAW *pDraw) {
    if (!instance || !instance->currentDecodeBuffer) return 0;
    
    int iWidth = pDraw->iWidth;
    if (iWidth > 16) iWidth = 16;
    
    int y = pDraw->y;
    if (y >= 16) return 0;
    
    uint16_t lineBuffer[16];
    instance->pngPtr->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
    
    for (int x = 0; x < iWidth; x++) {
        uint16_t color = lineBuffer[x];
        if (color != 0) {
            instance->currentDecodeBuffer[y * 16 + x] = color;
        } else {
            instance->currentDecodeBuffer[y * 16 + x] = 0x0000;
        }
    }
    return 1;
}

void StockEngine::update(EngineContext* context) {
    if (symbolList.empty() || !config.enabled) return;
    
    uint32_t durationMs = (config.duration_sec > 0 ? config.duration_sec : 5) * 1000;
    if (millis() - lastItemSwitchTime > durationMs) {
        lastItemSwitchTime = millis();
        currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
        fetchQuote(symbolList[currentSymbolIndex]);
    }
}

void StockEngine::render(EngineContext* context) {
    if (symbolList.empty() || !config.enabled) return;
    renderQuote(context);
}

void StockEngine::renderQuote(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();
    
    // Select Icon
    const uint16_t* icon = ICON_AAPL_8x8;
    if (activeSymbol == "NVDA") icon = ICON_NVDA_8x8;
    else if (activeSymbol == "TSLA") icon = ICON_TSLA_8x8;
    
    // Format Price & Change strings safely
    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "$%.2f", currentPrice);
    }
    
    char pctBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(pctBuf, sizeof(pctBuf), "--");
    } else {
        snprintf(pctBuf, sizeof(pctBuf), "%s%.2f%%", changePercent24h >= 0 ? "+" : "", changePercent24h);
    }
    uint16_t badgeColor = (!fetchSuccess || currentPrice <= 0.0f) ? matrix->color565(150, 150, 150) : (changePercent24h >= 0 ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60));

    if (mH >= 64) {
        // High Resolution (64px high, e.g. 128x64 or 256x64) - Large Prominent Layout
        // 1. Draw 16x16 Scaled Icon (2x scale)
        int iconX = 6;
        int iconY = 6;
        
        AssetQuoteCache& cache = quoteCache[activeSymbol];
        if (cache.hasIcon) {
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    uint16_t color = cache.iconPixels[y * 16 + x];
                    if (color != 0) {
                        matrix->drawPixel(iconX + x, iconY + y, color);
                    }
                }
            }
        } else {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t color = icon[y * 8 + x];
                    if (color != 0) {
                        matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
                    }
                }
            }
        }
        
        // 2. Draw Symbol (Size 2 = 12x16px per char)
        matrix->setTextColor(0xFFFF); // Bright White
        matrix->setTextSize(2);
        matrix->setCursor(28, 6);
        matrix->print(activeSymbol);
        
        // 3. Draw Price (Size 2 = 12x16px per char)
        matrix->setTextColor(matrix->color565(0, 220, 255)); // Bright Cyan
        int priceX = mW - (strlen(priceBuf) * 12 + 6);
        if (priceX < 28 + (int)activeSymbol.length() * 12 + 8) {
            priceX = 28 + activeSymbol.length() * 12 + 8;
        }
        matrix->setCursor(priceX, 6);
        matrix->print(priceBuf);
        
        // 4. Subtle horizontal divider line
        matrix->drawFastHLine(6, 28, mW - 12, matrix->color565(60, 60, 60));
        
        // 5. Bottom Row: 24h Change Badge (Size 2)
        matrix->setTextColor(badgeColor);
        matrix->setTextSize(2);
        matrix->setCursor(6, 36);
        if (fetchSuccess && currentPrice > 0.0f) {
            matrix->print(changePercent24h >= 0 ? "^ " : "v ");
        }
        matrix->print(pctBuf);
        
    } else {
        // Standard Resolution (32px high, e.g. 128x32 or 64x32)
        int iconX = 2;
        int iconY = (mH - 16) / 2;
        if (iconY < 0) iconY = 0;
        
        AssetQuoteCache& cache = quoteCache[activeSymbol];
        if (cache.hasIcon) {
            // Draw 16x16 scaled down to 8x8 by dropping every other pixel
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t color = cache.iconPixels[(y * 2) * 16 + (x * 2)];
                    if (color != 0) {
                        matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
                    }
                }
            }
        } else {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t color = icon[y * 8 + x];
                    if (color != 0) {
                        matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
                    }
                }
            }
        }
        
        // Symbol + Price top line (Size 1)
        matrix->setTextColor(0xFFFF);
        matrix->setTextSize(1);
        matrix->setCursor(20, 4);
        matrix->print(activeSymbol);
        
        matrix->setTextColor(matrix->color565(0, 220, 255));
        matrix->setCursor(20 + activeSymbol.length() * 6 + 6, 4);
        matrix->print(priceBuf);
        
        // Change Badge bottom line (Size 1)
        matrix->setTextColor(badgeColor);
        matrix->setCursor(20, 18);
        matrix->print(pctBuf);
    }
}
