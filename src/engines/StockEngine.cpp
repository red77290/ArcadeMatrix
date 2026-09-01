#include "StockEngine.h"
#include "../hal/HardwareHAL.h"
#include "../core/Logger.h"
#include "../core/SDUtils.h"
#include "../api/YahooFinanceProvider.h"
#include <HTTPClient.h>
#include <WiFiClient.h>

StockEngine* StockEngine::instance = nullptr;

StockEngine::StockEngine() 
    : currentSymbolIndex(0), lastItemSwitchTime(0),
      currentPrice(0.0f), changePercent24h(0.0f), fetchSuccess(false), currentDecodeBuffer(nullptr) {
    instance = this;
    addProvider(new YahooFinanceProvider());
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

void StockEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;
    config_enabled = engineConfig->getBool("enabled", true);
    config_duration_sec = engineConfig->getInt("duration_sec", 5);
    config_cache_ttl_min = engineConfig->getInt("cache_ttl_min", 15);
    config_show_chart = engineConfig->getBool("show_chart", true);
    
    Timeframe prevTf = config_chart_timeframe;
    config_chart_timeframe = timeframeFromString(engineConfig->getString("chart_timeframe", "daily"));
    String syms = engineConfig->getString("symbols", "AAPL,NVDA,TSLA,MSFT");
    parseSymbols(syms);

    if (config_chart_timeframe != prevTf && !symbolList.empty()) {
        LOGI("StockEngine", "Timeframe changed to %s. Triggering immediate history fetch.", timeframeLabel(config_chart_timeframe));
        if (config_show_chart) {
            String sym = symbolList[currentSymbolIndex % symbolList.size()];
            fetchHistory(sym, config_chart_timeframe);
        }
    }
}

void StockEngine::parseSymbols(const String& syms) {
    symbolList.clear();
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
    symbolsShownThisCycle = 0;
    if (!symbolList.empty()) {
        String sym = symbolList[currentSymbolIndex % symbolList.size()];
        fetchQuote(sym);
        if (config_show_chart) {
            fetchHistory(sym, config_chart_timeframe);
        }
    }
}

void StockEngine::deactivate() {
}

void StockEngine::fetchQuote(const String& symbol) {
    activeSymbol = symbol;
    fetchSuccess = false;
    
    uint32_t now = millis();
    AssetQuoteCache& cache = quoteCache[symbol];
    
    uint32_t ttlMs = (config_cache_ttl_min > 0 ? config_cache_ttl_min : 1) * 60 * 1000;
    
    // 1. Check if cache is fresh (< config_cache_ttl_min minutes old)
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
        String safeName = symbol;
        safeName.toLowerCase();
        String sdPath = "/stock_icons/" + safeName + ".png";
        
        if (!sd.exists(sdPath)) {
            HTTPClient httpImg;
            WiFiClient imgClient;
            String proxyUrl = "http://images.weserv.nl/?url=" + newImgUrl + "&w=16&h=16&output=png";
            httpImg.setTimeout(5000);
            if (httpImg.begin(imgClient, proxyUrl)) {
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
        // Fallback to last known cached price for THIS symbol if HTTP failed (e.g. Rate Limit 429)
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
    // We decode to RGB565. Transparency will be handled by drawing only non-black or by PNG library.
    instance->pngPtr->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0x00000000); // Using black as transparent background
    
    for (int x = 0; x < iWidth; x++) {
        uint16_t color = lineBuffer[x];
        // Only save non-black pixels (assuming black is background/transparent)
        if (color != 0) {
            instance->currentDecodeBuffer[y * 16 + x] = color;
        } else {
            instance->currentDecodeBuffer[y * 16 + x] = 0x0000; // Transparent indicator
        }
    }
    return 1;
}

void StockEngine::fetchHistory(const String& symbol, Timeframe tf) {
    uint32_t now = millis();
    String histKey = symbol + "_" + timeframeLabel(tf);
    AssetHistoryCache& cache = historyCache[histKey];

    uint32_t ttlMs = (config_cache_ttl_min > 0 ? config_cache_ttl_min : 1) * 60 * 1000;

    if (cache.hasData && (now - cache.lastFetchTime < ttlMs)) {
        LOGI("StockEngine", "[Cache Hit] Using cached history for %s (%s)", symbol.c_str(), timeframeLabel(tf));
        return;
    }

    float points[64];
    size_t count = 0;
    float minP = 0.0f;
    float maxP = 0.0f;

    for (IStockProvider* provider : providers) {
        if (provider->fetchHistory(symbol, tf, points, 64, count, minP, maxP)) {
            memcpy(cache.points, points, count * sizeof(float));
            cache.count = count;
            cache.minPrice = minP;
            cache.maxPrice = maxP;
            cache.lastFetchTime = now;
            cache.hasData = true;
            LOGI("StockEngine", "[History Success] Fetched %d points for %s (%s)", (int)count, symbol.c_str(), timeframeLabel(tf));
            return;
        }
    }
}

void StockEngine::update(EngineContext* context) {
    if (symbolList.empty() || !config_enabled) return;
    auto* matrix = context ? context->getMatrix() : nullptr;
    int mH = matrix ? matrix->height() : 32;
    
    uint32_t now = millis();
    uint32_t durationMs = (config_duration_sec > 0 ? config_duration_sec : 5) * 1000;
    if (now - lastItemSwitchTime > durationMs) {
        lastItemSwitchTime = now;
        if (mH >= 64 || !config_show_chart) {
            // Unified display mode: Advance directly to next symbol with quote + chart
            currentPage = DisplayPage::Info;
            symbolsShownThisCycle++;
            currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
            activeSymbol = symbolList[currentSymbolIndex];
            fetchQuote(activeSymbol);
            if (config_show_chart) {
                fetchHistory(activeSymbol, config_chart_timeframe);
            }
        } else {
            // Compact 32px split mode: Alternate between Info and Chart
            if (currentPage == DisplayPage::Info) {
                currentPage = DisplayPage::Chart;
                fetchHistory(symbolList[currentSymbolIndex % symbolList.size()], config_chart_timeframe);
            } else {
                currentPage = DisplayPage::Info;
                symbolsShownThisCycle++;
                currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
                activeSymbol = symbolList[currentSymbolIndex];
                fetchQuote(activeSymbol);
            }
        }
    }
}

bool StockEngine::isFinished() const {
    if (symbolList.empty()) return true;
    return symbolsShownThisCycle >= symbolList.size();
}

void StockEngine::render(EngineContext* context) {
    if (symbolList.empty() || !config_enabled) return;
    auto* matrix = context->getMatrix();
    int mW = matrix->width();
    int mH = matrix->height();

    if (!config_show_chart) {
        if (mH >= 64) {
            renderFullScreenQuote(context);
        } else {
            renderQuote(context);
        }
    } else {
        if (mH >= 64) {
            if (mW <= 64) {
                renderUnifiedVertical(context);
            } else {
                renderUnifiedWide(context);
            }
        } else {
            if (currentPage == DisplayPage::Info) {
                renderQuote(context);
            } else {
                renderChart(context);
            }
        }
    }
}

void StockEngine::renderUnifiedVertical(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "$%.0f", currentPrice);
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

    const uint16_t* icon = ICON_AAPL_8x8;
    if (activeSymbol == "NVDA") icon = ICON_NVDA_8x8;
    else if (activeSymbol == "TSLA") icon = ICON_TSLA_8x8;

    AssetQuoteCache& cache = quoteCache[activeSymbol];
    const char* tfLabel = timeframeLabel(config_chart_timeframe);
    String histKey = activeSymbol + "_" + tfLabel;
    AssetHistoryCache& hist = historyCache[histKey];

    if (mW >= 48) {
        int iconX = 2;
        int iconY = 2;

        if (cache.hasIcon) {
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    uint16_t color = cache.iconPixels[y * 16 + x];
                    if (color != 0) matrix->drawPixel(iconX + x, iconY + y, color);
                }
            }
        } else {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t color = icon[y * 8 + x];
                    if (color != 0) matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
                }
            }
        }

        // Top line: Symbol + Timeframe
        matrix->setTextSize(1);
        matrix->setTextColor(0xFFFF);
        matrix->setCursor(20, 2);
        matrix->print(activeSymbol);

        matrix->setTextColor(matrix->color565(140, 140, 140));
        int tfX = mW - (strlen(tfLabel) * 6 + 2);
        if (tfX < 20 + (int)activeSymbol.length() * 6 + 4) tfX = 20 + activeSymbol.length() * 6 + 4;
        matrix->setCursor(tfX, 2);
        matrix->print(tfLabel);

        // Line 2: Price (Cyan for Stocks)
        matrix->setTextColor(matrix->color565(0, 220, 255));
        matrix->setCursor(20, 10);
        matrix->print(priceBuf);

        // Line 3: 24h Change with indicator
        matrix->setTextColor(badgeColor);
        matrix->setCursor(2, 19);
        if (fetchSuccess && currentPrice > 0.0f) {
            matrix->print(changePercent24h >= 0 ? "^" : "v");
        }
        matrix->print(pctBuf);

        // Separator line
        matrix->drawFastHLine(2, 28, mW - 4, matrix->color565(50, 50, 50));

        // Sparkline Chart (from Y=30 to mH-2)
        int sparkX = 2;
        int sparkY = 30;
        int sparkW = mW - 4;
        int sparkH = mH - 32;

        if (hist.hasData && hist.count > 1) {
            bool isUp = (hist.points[hist.count - 1] >= hist.points[0]);
            uint16_t lineColor = isUp ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60);
            uint16_t fillColor = isUp ? matrix->color565(0, 35, 12) : matrix->color565(40, 12, 12);
            SparklineRenderer::drawSparkline(matrix, hist.points, hist.count, hist.minPrice, hist.maxPrice, sparkX, sparkY, sparkW, sparkH, lineColor, fillColor);
        } else {
            matrix->setTextColor(matrix->color565(120, 120, 120));
            matrix->setCursor(4, sparkY + (sparkH / 2) - 3);
            matrix->print("Loading...");
        }
    } else {
        // Narrow 32x64 layout
        int iconX = 1;
        int iconY = 1;
        if (cache.hasIcon) {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t color = cache.iconPixels[(y * 2) * 16 + (x * 2)];
                    if (color != 0) matrix->drawPixel(iconX + x, iconY + y, color);
                }
            }
        } else {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t color = icon[y * 8 + x];
                    if (color != 0) matrix->drawPixel(iconX + x, iconY + y, color);
                }
            }
        }

        matrix->setTextSize(1);
        matrix->setTextColor(0xFFFF);
        matrix->setCursor(11, 2);
        matrix->print(activeSymbol);

        matrix->setTextColor(matrix->color565(0, 220, 255));
        matrix->setCursor(1, 11);
        matrix->print(priceBuf);

        matrix->setTextColor(badgeColor);
        matrix->setCursor(1, 20);
        matrix->print(pctBuf);

        matrix->drawFastHLine(1, 28, mW - 2, matrix->color565(50, 50, 50));

        int sparkX = 1;
        int sparkY = 30;
        int sparkW = mW - 2;
        int sparkH = mH - 32;

        if (hist.hasData && hist.count > 1) {
            bool isUp = (hist.points[hist.count - 1] >= hist.points[0]);
            uint16_t lineColor = isUp ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60);
            uint16_t fillColor = isUp ? matrix->color565(0, 35, 12) : matrix->color565(40, 12, 12);
            SparklineRenderer::drawSparkline(matrix, hist.points, hist.count, hist.minPrice, hist.maxPrice, sparkX, sparkY, sparkW, sparkH, lineColor, fillColor);
        } else {
            matrix->setTextColor(matrix->color565(120, 120, 120));
            matrix->setCursor(2, sparkY + (sparkH / 2) - 3);
            matrix->print("...");
        }
    }
}

void StockEngine::renderUnifiedWide(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "$%.0f", currentPrice);
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

    const uint16_t* icon = ICON_AAPL_8x8;
    if (activeSymbol == "NVDA") icon = ICON_NVDA_8x8;
    else if (activeSymbol == "TSLA") icon = ICON_TSLA_8x8;

    AssetQuoteCache& cache = quoteCache[activeSymbol];
    const char* tfLabel = timeframeLabel(config_chart_timeframe);
    String histKey = activeSymbol + "_" + tfLabel;
    AssetHistoryCache& hist = historyCache[histKey];

    int iconX = 4;
    int iconY = 4;
    if (cache.hasIcon) {
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                uint16_t color = cache.iconPixels[y * 16 + x];
                if (color != 0) matrix->drawPixel(iconX + x, iconY + y, color);
            }
        }
    } else {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint16_t color = icon[y * 8 + x];
                if (color != 0) matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
            }
        }
    }

    matrix->setTextSize(1);
    matrix->setTextColor(0xFFFF);
    matrix->setCursor(24, 4);
    matrix->print(activeSymbol);

    matrix->setTextColor(matrix->color565(140, 140, 140));
    matrix->setCursor(24, 13);
    matrix->print(tfLabel);

    matrix->setTextColor(matrix->color565(0, 220, 255));
    matrix->setCursor(4, 24);
    matrix->print(priceBuf);

    matrix->setTextColor(badgeColor);
    matrix->setCursor(4, 35);
    if (fetchSuccess && currentPrice > 0.0f) {
        matrix->print(changePercent24h >= 0 ? "^ " : "v ");
    }
    matrix->print(pctBuf);

    int divX = 58;
    matrix->drawFastVLine(divX, 4, mH - 8, matrix->color565(50, 50, 50));

    int sparkX = 62;
    int sparkY = 6;
    int sparkW = mW - 66;
    int sparkH = mH - 12;

    if (hist.hasData && hist.count > 1) {
        bool isUp = (hist.points[hist.count - 1] >= hist.points[0]);
        uint16_t lineColor = isUp ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60);
        uint16_t fillColor = isUp ? matrix->color565(0, 35, 12) : matrix->color565(40, 12, 12);
        SparklineRenderer::drawSparkline(matrix, hist.points, hist.count, hist.minPrice, hist.maxPrice, sparkX, sparkY, sparkW, sparkH, lineColor, fillColor);
    } else {
        matrix->setTextColor(matrix->color565(120, 120, 120));
        matrix->setCursor(sparkX + 4, sparkY + (sparkH / 2) - 3);
        matrix->print("Loading chart...");
    }
}

void StockEngine::renderChart(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "$%.2f", currentPrice);
    }

    const char* tfLabel = timeframeLabel(config_chart_timeframe);
    String histKey = activeSymbol + "_" + tfLabel;
    AssetHistoryCache& hist = historyCache[histKey];

    matrix->setTextColor(0xFFFF);
    matrix->setTextSize(1);
    matrix->setCursor(2, 1);
    matrix->printf("%s %s", activeSymbol.c_str(), tfLabel);

    matrix->setTextColor(matrix->color565(0, 220, 255));
    int priceX = mW - (strlen(priceBuf) * 6 + 2);
    if (priceX < 2 + (int)(activeSymbol.length() + 4) * 6) priceX = 2 + (activeSymbol.length() + 4) * 6;
    matrix->setCursor(priceX, 1);
    matrix->print(priceBuf);

    int sparkX = 2;
    int sparkY = 11;
    int sparkW = mW - 4;
    int sparkH = mH - 13;

    if (hist.hasData && hist.count > 1) {
        bool isUp = (hist.points[hist.count - 1] >= hist.points[0]);
        uint16_t lineColor = isUp ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60);
        uint16_t fillColor = isUp ? matrix->color565(0, 35, 12) : matrix->color565(40, 12, 12);
        SparklineRenderer::drawSparkline(matrix, hist.points, hist.count, hist.minPrice, hist.maxPrice, sparkX, sparkY, sparkW, sparkH, lineColor, fillColor);
    } else {
        matrix->setTextColor(matrix->color565(120, 120, 120));
        matrix->setCursor(4, sparkY + 4);
        matrix->print("Loading...");
    }
}

void StockEngine::renderQuote(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();
    
    const uint16_t* icon = ICON_AAPL_8x8;
    if (activeSymbol == "NVDA") icon = ICON_NVDA_8x8;
    else if (activeSymbol == "TSLA") icon = ICON_TSLA_8x8;
    
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

    int iconX = 2;
    int iconY = (mH - 16) / 2;
    if (iconY < 0) iconY = 0;
    
    AssetQuoteCache& cache = quoteCache[activeSymbol];
    if (cache.hasIcon) {
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
    
    if (mW < 48) {
        matrix->setTextColor(0xFFFF);
        matrix->setTextSize(1);
        matrix->setCursor(18, 2);
        matrix->print(activeSymbol);

        matrix->setTextColor(matrix->color565(0, 220, 255));
        matrix->setCursor(2, 12);
        matrix->print(priceBuf);

        matrix->setTextColor(badgeColor);
        matrix->setCursor(2, 22);
        matrix->print(pctBuf);
    } else {
        matrix->setTextColor(0xFFFF);
        matrix->setTextSize(1);
        matrix->setCursor(20, 4);
        matrix->print(activeSymbol);
        
        matrix->setTextColor(matrix->color565(0, 220, 255));
        matrix->setCursor(20 + activeSymbol.length() * 6 + 6, 4);
        matrix->print(priceBuf);
        
        matrix->setTextColor(badgeColor);
        matrix->setCursor(20, 18);
        matrix->print(pctBuf);
    }
}

void StockEngine::renderFullScreenQuote(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "$%.0f", currentPrice);
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

    const uint16_t* icon = ICON_AAPL_8x8;
    if (activeSymbol == "NVDA") icon = ICON_NVDA_8x8;
    else if (activeSymbol == "TSLA") icon = ICON_TSLA_8x8;

    AssetQuoteCache& cache = quoteCache[activeSymbol];

    if (mW <= 64) {
        // Vertical / Square display without chart (64x64, 32x64, 64x128)
        int iconSize = (mW >= 48) ? 16 : 8;
        int iconX = (mW - iconSize) / 2;
        int priceLen = strlen(priceBuf);
        bool useBigPrice = (mW >= 64 && priceLen * 12 <= mW - 4);
        
        int totalH = useBigPrice ? (iconSize + 3 + 8 + 4 + 16 + 4 + 9) : (iconSize + 4 + 8 + 4 + 8 + 4 + 9);
        int startY = (mH > totalH) ? ((mH - totalH) / 2) : 2;

        int iconY = startY;
        if (iconSize == 16) {
            if (cache.hasIcon) {
                for (int y = 0; y < 16; y++) {
                    for (int x = 0; x < 16; x++) {
                        uint16_t color = cache.iconPixels[y * 16 + x];
                        if (color != 0) matrix->drawPixel(iconX + x, iconY + y, color);
                    }
                }
            } else {
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                        uint16_t color = icon[y * 8 + x];
                        if (color != 0) matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
                    }
                }
            }
        } else {
            if (cache.hasIcon) {
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                        uint16_t color = cache.iconPixels[(y * 2) * 16 + (x * 2)];
                        if (color != 0) matrix->drawPixel(iconX + x, iconY + y, color);
                    }
                }
            } else {
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                        uint16_t color = icon[y * 8 + x];
                        if (color != 0) matrix->drawPixel(iconX + x, iconY + y, color);
                    }
                }
            }
        }

        // Symbol centered
        matrix->setTextColor(0xFFFF);
        matrix->setTextSize(1);
        int symX = (mW - (int)activeSymbol.length() * 6) / 2;
        if (symX < 0) symX = 0;
        int symY = iconY + iconSize + (useBigPrice ? 3 : 4);
        matrix->setCursor(symX, symY);
        matrix->print(activeSymbol);

        // Price centered (Cyan for Stocks)
        matrix->setTextColor(matrix->color565(0, 220, 255));
        int priceY = symY + 8 + 4;
        if (useBigPrice) {
            matrix->setTextSize(2);
            int pX = (mW - priceLen * 12) / 2;
            matrix->setCursor(pX, priceY);
            matrix->print(priceBuf);
            priceY += 16 + 4;
        } else {
            matrix->setTextSize(1);
            int pX = (mW - priceLen * 6) / 2;
            if (pX < 0) pX = 0;
            matrix->setCursor(pX, priceY);
            matrix->print(priceBuf);
            priceY += 8 + 4;
        }

        // 24h change badge with pill background
        matrix->setTextSize(1);
        int arrowLen = (fetchSuccess && currentPrice > 0.0f) ? 2 : 0;
        int pctLen = strlen(pctBuf) + arrowLen;
        int pctW = pctLen * 6;
        int pctX = (mW - pctW) / 2;
        if (pctX < 2) pctX = 2;
        int pctY = priceY;
        if (pctY > mH - 10) pctY = mH - 10;

        uint16_t pillBg = changePercent24h >= 0 ? matrix->color565(0, 35, 12) : matrix->color565(45, 10, 10);
        uint16_t pillBorder = changePercent24h >= 0 ? matrix->color565(0, 80, 25) : matrix->color565(90, 20, 20);
        matrix->fillRoundRect(pctX - 3, pctY - 1, pctW + 6, 10, 2, pillBg);
        matrix->drawRoundRect(pctX - 3, pctY - 1, pctW + 6, 10, 2, pillBorder);

        matrix->setTextColor(badgeColor);
        matrix->setCursor(pctX, pctY);
        if (fetchSuccess && currentPrice > 0.0f) {
            matrix->print(changePercent24h >= 0 ? "^ " : "v ");
        }
        matrix->print(pctBuf);
    } else {
        // Widescreen display without chart (128x64, 256x64)
        int iconX = (mW / 4) - 16;
        if (iconX < 4) iconX = 4;
        int iconY = (mH - 32) / 2;
        if (iconY < 2) iconY = 2;

        if (cache.hasIcon) {
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    uint16_t color = cache.iconPixels[y * 16 + x];
                    if (color != 0) matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
                }
            }
        } else {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t color = icon[y * 8 + x];
                    if (color != 0) matrix->fillRect(iconX + (x * 4), iconY + (y * 4), 4, 4, color);
                }
            }
        }

        matrix->drawFastVLine(mW / 2 - 2, 8, mH - 16, matrix->color565(40, 40, 40));

        int textX = mW / 2 + 8;
        int totalH = 16 + 4 + 16 + 4 + 10;
        int startY = (mH > totalH) ? ((mH - totalH) / 2) : 4;

        matrix->setTextColor(0xFFFF);
        matrix->setTextSize(2);
        matrix->setCursor(textX, startY);
        matrix->print(activeSymbol);

        matrix->setTextColor(matrix->color565(0, 220, 255));
        matrix->setCursor(textX, startY + 19);
        matrix->print(priceBuf);

        int pctY = startY + 38;
        int arrowLen = (fetchSuccess && currentPrice > 0.0f) ? 2 : 0;
        int pctLen = strlen(pctBuf) + arrowLen;
        int pctW = pctLen * 6;
        uint16_t pillBg = changePercent24h >= 0 ? matrix->color565(0, 35, 12) : matrix->color565(45, 10, 10);
        uint16_t pillBorder = changePercent24h >= 0 ? matrix->color565(0, 80, 25) : matrix->color565(90, 20, 20);
        matrix->fillRoundRect(textX - 2, pctY - 1, pctW + 4, 10, 2, pillBg);
        matrix->drawRoundRect(textX - 2, pctY - 1, pctW + 4, 10, 2, pillBorder);

        matrix->setTextSize(1);
        matrix->setTextColor(badgeColor);
        matrix->setCursor(textX, pctY);
        if (fetchSuccess && currentPrice > 0.0f) {
            matrix->print(changePercent24h >= 0 ? "^ " : "v ");
        }
        matrix->print(pctBuf);
        matrix->setTextColor(matrix->color565(140, 140, 140));
        matrix->setCursor(textX + pctW + 6, pctY);
        matrix->print("24h");
    }
}

EngineDescriptor StockEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_stock;
    desc_stock.metadata = {"stock", "Stock Ticker", "finance", FIRMWARE_VERSION};
    desc_stock.capabilities.realtime = false;
    desc_stock.requirements.needsPsram = true;
    desc_stock.requirements.needsNetwork = true;
    desc_stock.schema.fields = {
        ConfigField("symbols", ConfigType::STRING, "Symbols", "Comma-separated stock symbols", "AAPL,TSLA,NVDA", true, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("show_chart", ConfigType::BOOLEAN, "Show Chart", "Display historical price sparkline chart", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("chart_timeframe", ConfigType::ENUM, "Chart Timeframe", "Historical chart timeframe", "daily", false, "", "", "", "hourly,daily,weekly,monthly", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("duration_sec", ConfigType::INTEGER, "Page Duration (s)", "Seconds to dwell on each view", "5", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("provider", ConfigType::ENUM, "Provider", "Market data provider", "yahoo", false, "", "", "", "yahoo", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("cache_ttl_min", ConfigType::INTEGER, "Cache TTL (min)", "Minutes between fresh API requests", "5", false, "1", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("stock_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("stock_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_stock.factory = []() { return std::unique_ptr<IEngine>(new StockEngine()); };
    return desc_stock;
}

