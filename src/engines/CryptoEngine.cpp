#include "CryptoEngine.h"
#include "../hal/HardwareHAL.h"
#include "../core/Logger.h"
#include "../core/SDUtils.h"
#include "../api/CoinGeckoProvider.h"
#include "../api/BinanceProvider.h"
#include <HTTPClient.h>
#include <WiFiClient.h>

CryptoEngine* CryptoEngine::instance = nullptr;

CryptoEngine::CryptoEngine() 
    : currentSymbolIndex(0), lastItemSwitchTime(0), lastFetchTime(0),
      currentPrice(0.0f), changePercent24h(0.0f), fetchSuccess(false), currentDecodeBuffer(nullptr) {
    instance = this;
    addProvider(new CoinGeckoProvider());
    addProvider(new BinanceProvider());
}

EngineError CryptoEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

void CryptoEngine::addProvider(ICryptoProvider* provider) {
    if (provider) {
        providers.push_back(provider);
    }
}

void CryptoEngine::parseSymbols(const String& syms) {
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
        symbolList.push_back("BTC");
        symbolList.push_back("ETH");
        symbolList.push_back("SOL");
        symbolList.push_back("DOGE");
    }
}

void CryptoEngine::activate() {
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

void CryptoEngine::fetchQuote(const String& symbol) {
    activeSymbol = symbol;
    fetchSuccess = false;
    
    uint32_t now = millis();
    AssetQuoteCache& cache = quoteCache[symbol];
    
    uint32_t ttlMs = (config_cache_ttl_min > 0 ? config_cache_ttl_min : 1) * 60 * 1000;
    
    if (cache.hasData && (now - cache.lastFetchTime < ttlMs)) {
        currentPrice = cache.price;
        changePercent24h = cache.changePercent24h;
        fetchSuccess = true;
        LOGI("CryptoEngine", "[Cache Hit] Using cached quote for %s: $%.4f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
        return;
    }
    
    float newPrice = 0.0f;
    float newChange = 0.0f;
    String newImgUrl = "";
    bool fetched = false;
    
    for (size_t i = 0; i < providers.size(); i++) {
        size_t idx = (config_provider == "binance") ? (providers.size() - 1 - i) : i;
        ICryptoProvider* provider = providers[idx];
        provider->setCurrency(config_currency);
        if (provider->fetchQuote(symbol, newPrice, newChange, newImgUrl)) {
            fetched = true;
            break;
        }
    }
    
    if (fetched && newImgUrl.length() > 0 && !cache.hasIcon) {
        String safeName = symbol;
        safeName.toLowerCase();
        String sdPath = "/crypto_icons/" + safeName + ".png";
        
        if (!sd.exists(sdPath)) {
            HTTPClient httpImg;
            WiFiClient imgClient;
            String proxyUrl = "http://images.weserv.nl/?url=" + newImgUrl + "&w=16&h=16&output=png";
            httpImg.setTimeout(5000);
            if (httpImg.begin(imgClient, proxyUrl)) {
                int code = httpImg.GET();
                if (code == 200) {
                    if (!sd.exists("/crypto_icons")) sd.mkdir("/crypto_icons");
                    FsFile f = sd.open(sdPath, FILE_OPEN_WRITE);
                    if (f) {
                        httpImg.writeToStream(&f);
                        f.close();
                    }
                }
                httpImg.end();
                imgClient.stop();
            }
        }
        
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
    
    if (fetched && newPrice > 0.0f) {
        cache.price = newPrice;
        cache.changePercent24h = newChange;
        cache.imageUrl = newImgUrl;
        cache.lastFetchTime = now;
        cache.hasData = true;
        
        currentPrice = newPrice;
        changePercent24h = newChange;
        fetchSuccess = true;
        LOGI("CryptoEngine", "[Fetch Success] Updated cache for %s: $%.4f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
    } else if (cache.hasData) {
        currentPrice = cache.price;
        changePercent24h = cache.changePercent24h;
        fetchSuccess = true;
        LOGW("CryptoEngine", "[HTTP Failed/429] Reusing last known cached price for %s: $%.4f", symbol.c_str(), currentPrice);
    } else {
        currentPrice = 0.0f;
        changePercent24h = 0.0f;
        fetchSuccess = false;
        LOGW("CryptoEngine", "No quote available for %s", symbol.c_str());
    }
}

int CryptoEngine::pngDraw(PNGDRAW *pDraw) {
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

void CryptoEngine::fetchHistory(const String& symbol, Timeframe tf) {
    uint32_t now = millis();
    String histKey = symbol + "_" + timeframeLabel(tf);
    AssetHistoryCache& cache = historyCache[histKey];
    
    uint32_t ttlMs = (config_cache_ttl_min > 0 ? config_cache_ttl_min : 1) * 60 * 1000;
    
    if (cache.hasData && (now - cache.lastFetchTime < ttlMs)) {
        LOGI("CryptoEngine", "[Cache Hit] Using cached history for %s (%s)", symbol.c_str(), timeframeLabel(tf));
        return;
    }
    
    float points[64];
    size_t count = 0;
    float minP = 0.0f;
    float maxP = 0.0f;
    
    for (size_t i = 0; i < providers.size(); i++) {
        size_t idx = (config_provider == "binance") ? (providers.size() - 1 - i) : i;
        ICryptoProvider* provider = providers[idx];
        provider->setCurrency(config_currency);
        if (provider->fetchHistory(symbol, tf, points, 64, count, minP, maxP)) {
            memcpy(cache.points, points, count * sizeof(float));
            cache.count = count;
            cache.minPrice = minP;
            cache.maxPrice = maxP;
            cache.lastFetchTime = now;
            cache.hasData = true;
            LOGI("CryptoEngine", "[History Success] Fetched %d points for %s (%s)", (int)count, symbol.c_str(), timeframeLabel(tf));
            return;
        }
    }
}

void CryptoEngine::update(EngineContext* context) {
    if (symbolList.empty() || !config_enabled) return;
    auto* matrix = context ? context->getMatrix() : nullptr;
    int mH = matrix ? matrix->height() : 32;
    
    uint32_t now = millis();
    uint32_t durationMs = (config_duration_sec > 0 ? config_duration_sec : 5) * 1000;
    if (now - lastItemSwitchTime > durationMs) {
        lastItemSwitchTime = now;
        if (mH >= 64 || !config_show_chart) {
            currentPage = DisplayPage::Info;
            symbolsShownThisCycle++;
            currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
            String nextSym = symbolList[currentSymbolIndex];
            fetchQuote(nextSym);
            if (config_show_chart) {
                fetchHistory(nextSym, config_chart_timeframe);
            }
        } else {
            if (currentPage == DisplayPage::Info) {
                currentPage = DisplayPage::Chart;
                fetchHistory(symbolList[currentSymbolIndex % symbolList.size()], config_chart_timeframe);
            } else {
                currentPage = DisplayPage::Info;
                symbolsShownThisCycle++;
                currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
                fetchQuote(symbolList[currentSymbolIndex]);
            }
        }
    }
}

bool CryptoEngine::isFinished() const {
    if (symbolList.empty()) return true;
    return (symbolsShownThisCycle >= symbolList.size());
}

void CryptoEngine::render(EngineContext* context) {
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

void CryptoEngine::deactivate() {
}

static const char* getCurrencyPrefix(const String& currency) {
    if (currency == "EUR") return "E";
    if (currency == "GBP") return "L";
    if (currency == "JPY") return "Y";
    return "$";
}

void CryptoEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;
    config_enabled = engineConfig->getBool("enabled", true);
    config_duration_sec = engineConfig->getInt("duration_sec", 5);
    config_cache_ttl_min = engineConfig->getInt("cache_ttl_min", 15);
    config_show_chart = engineConfig->getBool("show_chart", true);
    config_chart_timeframe = timeframeFromString(engineConfig->getString("chart_timeframe", "daily"));
    
    Timeframe prevTf = config_chart_timeframe;
    config_chart_timeframe = timeframeFromString(engineConfig->getString("chart_timeframe", "daily"));
    
    String prevCurrency = config_currency;
    config_currency = engineConfig->getString("currency", "USD");
    config_currency.toUpperCase();
    if (config_currency.isEmpty()) config_currency = "USD";

    String prevProvider = config_provider;
    config_provider = engineConfig->getString("provider", "coingecko");
    config_provider.toLowerCase();
    if (config_provider.isEmpty()) config_provider = "coingecko";

    String syms = engineConfig->getString("symbols", "BTC,ETH,SOL");
    parseSymbols(syms);

    bool needQuoteFetch = (config_currency != prevCurrency || config_provider != prevProvider);
    bool needHistFetch = needQuoteFetch || (config_chart_timeframe != prevTf);

    if (needQuoteFetch) {
        LOGI("CryptoEngine", "Config hot-reloaded: currency=%s (was %s), provider=%s (was %s). Flushing cache.", 
             config_currency.c_str(), prevCurrency.c_str(), config_provider.c_str(), prevProvider.c_str());
        quoteCache.clear();
        historyCache.clear();
        fetchSuccess = false;
        currentPrice = 0.0f;
    }

    if (needHistFetch && !symbolList.empty()) {
        String sym = symbolList[currentSymbolIndex % symbolList.size()];
        if (needQuoteFetch) {
            fetchQuote(sym);
        }
        if (config_show_chart) {
            fetchHistory(sym, config_chart_timeframe);
        }
    }
}

void CryptoEngine::renderUnifiedVertical(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    const char* curSym = getCurrencyPrefix(config_currency);
    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.0f", curSym, currentPrice);
    } else if (currentPrice >= 1.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.2f", curSym, currentPrice);
    } else if (currentPrice >= 0.001f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.4f", curSym, currentPrice);
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.6f", curSym, currentPrice);
    }

    char pctBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(pctBuf, sizeof(pctBuf), "--");
    } else {
        snprintf(pctBuf, sizeof(pctBuf), "%s%.2f%%", changePercent24h >= 0 ? "+" : "", changePercent24h);
    }
    uint16_t badgeColor = (!fetchSuccess || currentPrice <= 0.0f) ? matrix->color565(150, 150, 150) : (changePercent24h >= 0 ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60));

    const uint16_t* icon = ICON_BTC_8x8;
    if (activeSymbol == "ETH") icon = ICON_ETH_8x8;
    else if (activeSymbol == "SOL") icon = ICON_SOL_8x8;

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

        matrix->setTextSize(1);
        matrix->setTextColor(0xFFFF);
        matrix->setCursor(20, 2);
        matrix->print(activeSymbol);

        matrix->setTextColor(matrix->color565(140, 140, 140));
        int tfX = mW - (strlen(tfLabel) * 6 + 2);
        if (tfX < 20 + (int)activeSymbol.length() * 6 + 4) tfX = 20 + activeSymbol.length() * 6 + 4;
        matrix->setCursor(tfX, 2);
        matrix->print(tfLabel);

        matrix->setTextColor(matrix->color565(255, 215, 0));
        matrix->setCursor(20, 10);
        matrix->print(priceBuf);

        matrix->setTextColor(badgeColor);
        matrix->setCursor(2, 19);
        if (fetchSuccess && currentPrice > 0.0f) {
            matrix->print(changePercent24h >= 0 ? "^" : "v");
        }
        matrix->print(pctBuf);

        matrix->drawFastHLine(2, 28, mW - 4, matrix->color565(50, 50, 50));

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

        matrix->setTextColor(matrix->color565(255, 215, 0));
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

void CryptoEngine::renderUnifiedWide(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    const char* curSym = getCurrencyPrefix(config_currency);
    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.0f", curSym, currentPrice);
    } else if (currentPrice >= 1.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.2f", curSym, currentPrice);
    } else if (currentPrice >= 0.001f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.4f", curSym, currentPrice);
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.6f", curSym, currentPrice);
    }

    char pctBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(pctBuf, sizeof(pctBuf), "--");
    } else {
        snprintf(pctBuf, sizeof(pctBuf), "%s%.2f%%", changePercent24h >= 0 ? "+" : "", changePercent24h);
    }
    uint16_t badgeColor = (!fetchSuccess || currentPrice <= 0.0f) ? matrix->color565(150, 150, 150) : (changePercent24h >= 0 ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60));

    const uint16_t* icon = ICON_BTC_8x8;
    if (activeSymbol == "ETH") icon = ICON_ETH_8x8;
    else if (activeSymbol == "SOL") icon = ICON_SOL_8x8;

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

    matrix->setTextColor(matrix->color565(255, 215, 0));
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

void CryptoEngine::renderChart(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    const char* curSym = getCurrencyPrefix(config_currency);
    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.0f", curSym, currentPrice);
    } else if (currentPrice >= 1.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.2f", curSym, currentPrice);
    } else if (currentPrice >= 0.001f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.4f", curSym, currentPrice);
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.6f", curSym, currentPrice);
    }

    const char* tfLabel = timeframeLabel(config_chart_timeframe);
    String histKey = activeSymbol + "_" + tfLabel;
    AssetHistoryCache& hist = historyCache[histKey];

    matrix->setTextColor(0xFFFF);
    matrix->setTextSize(1);
    matrix->setCursor(2, 1);
    matrix->printf("%s %s", activeSymbol.c_str(), tfLabel);

    matrix->setTextColor(matrix->color565(255, 215, 0));
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

void CryptoEngine::renderQuote(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();
    
    const uint16_t* icon = ICON_BTC_8x8;
    if (activeSymbol == "ETH") icon = ICON_ETH_8x8;
    else if (activeSymbol == "SOL") icon = ICON_SOL_8x8;
    
    const char* curSym = getCurrencyPrefix(config_currency);
    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.0f", curSym, currentPrice);
    } else if (currentPrice >= 1.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.2f", curSym, currentPrice);
    } else if (currentPrice >= 0.001f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.4f", curSym, currentPrice);
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.6f", curSym, currentPrice);
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

        matrix->setTextColor(matrix->color565(255, 215, 0));
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
        
        matrix->setTextColor(matrix->color565(255, 215, 0));
        matrix->setCursor(20 + activeSymbol.length() * 6 + 6, 4);
        matrix->print(priceBuf);
        
        matrix->setTextColor(badgeColor);
        matrix->setCursor(20, 18);
        matrix->print(pctBuf);
    }
}

void CryptoEngine::renderFullScreenQuote(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    const char* curSym = getCurrencyPrefix(config_currency);
    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.0f", curSym, currentPrice);
    } else if (currentPrice >= 1.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.2f", curSym, currentPrice);
    } else if (currentPrice >= 0.001f) {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.4f", curSym, currentPrice);
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "%s%.6f", curSym, currentPrice);
    }

    char pctBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(pctBuf, sizeof(pctBuf), "--");
    } else {
        snprintf(pctBuf, sizeof(pctBuf), "%s%.2f%%", changePercent24h >= 0 ? "+" : "", changePercent24h);
    }
    uint16_t badgeColor = (!fetchSuccess || currentPrice <= 0.0f) ? matrix->color565(150, 150, 150) : (changePercent24h >= 0 ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60));

    const uint16_t* icon = ICON_BTC_8x8;
    if (activeSymbol == "ETH") icon = ICON_ETH_8x8;
    else if (activeSymbol == "SOL") icon = ICON_SOL_8x8;

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

        // Price centered
        matrix->setTextColor(matrix->color565(255, 215, 0));
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

        matrix->setTextColor(matrix->color565(255, 215, 0));
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

EngineDescriptor CryptoEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_crypto;
    desc_crypto.metadata = {"crypto", "Crypto Tracker", "finance", FIRMWARE_VERSION};
    desc_crypto.capabilities.realtime = false;
    desc_crypto.requirements.needsPsram = true;
    desc_crypto.requirements.needsNetwork = true;
    desc_crypto.schema.fields = {
        ConfigField("symbols", ConfigType::STRING, "Symbols", "Comma-separated crypto symbols", "BTC,ETH,SOL", true, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("show_chart", ConfigType::BOOLEAN, "Show Chart", "Display historical price sparkline chart", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("chart_timeframe", ConfigType::ENUM, "Chart Timeframe", "Historical chart timeframe", "daily", false, "", "", "", "hourly,daily,weekly,monthly", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("duration_sec", ConfigType::INTEGER, "Page Duration (s)", "Seconds to dwell on each view", "5", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("currency", ConfigType::ENUM, "Fiat Currency", "Target currency for quotes", "USD", false, "", "", "", "USD,EUR,GBP,JPY", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("provider", ConfigType::ENUM, "Provider", "Market data provider", "coingecko", false, "", "", "", "coingecko,binance", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("cache_ttl_min", ConfigType::INTEGER, "Cache TTL (min)", "Minutes between fresh API requests", "5", false, "1", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("crypto_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("crypto_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_crypto.factory = []() { return std::unique_ptr<IEngine>(new CryptoEngine()); };
    return desc_crypto;
}

