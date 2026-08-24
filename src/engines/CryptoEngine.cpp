#include "CryptoEngine.h"
#include "../hal/HardwareHAL.h"
#include "../core/Logger.h"
#include "../api/CoinGeckoProvider.h"
#include "../api/BinanceProvider.h"
#include <HTTPClient.h>

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
        fetchQuote(symbolList[currentSymbolIndex % symbolList.size()]);
    }
}

void CryptoEngine::fetchQuote(const String& symbol) {
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
        LOGI("CryptoEngine", "[Cache Hit] Using cached quote for %s: $%.4f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
        return;
    }
    
    float newPrice = 0.0f;
    float newChange = 0.0f;
    String newImgUrl = "";
    bool fetched = false;
    
    // Try each provider until one succeeds
    for (ICryptoProvider* provider : providers) {
        if (provider->fetchQuote(symbol, newPrice, newChange, newImgUrl)) {
            fetched = true;
            break;
        }
    }
    
    // Download and Cache Icon
    if (fetched && newImgUrl.length() > 0 && !cache.hasIcon) {
        String sdPath = "/crypto_icons/" + symbol + ".png";
        if (!sd.exists(sdPath)) {
            String proxyUrl = "https://wsrv.nl/?url=" + newImgUrl + "&w=16&h=16&output=png";
            HTTPClient httpImg;
            httpImg.setTimeout(5000);
            if (httpImg.begin(proxyUrl)) {
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
        LOGI("CryptoEngine", "[Fetch Success] Updated cache for %s: $%.4f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
    } else if (cache.hasData) {
        // Fallback to last known cached price for THIS symbol if HTTP failed (e.g. Rate Limit 429)
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

void CryptoEngine::fetchHistory(const String& symbol, Timeframe tf) {
    uint32_t now = millis();
    AssetHistoryCache& cache = historyCache[symbol];

    uint32_t ttlMs = 5 * 60 * 1000;
    switch (tf) {
        case Timeframe::Hourly: ttlMs = 60 * 1000; break;
        case Timeframe::Daily: ttlMs = 5 * 60 * 1000; break;
        case Timeframe::Weekly: ttlMs = 30 * 60 * 1000; break;
        case Timeframe::Monthly: ttlMs = 120 * 60 * 1000; break;
    }

    if (cache.hasData && (now - cache.lastFetchTime < ttlMs)) {
        return;
    }

    float points[64];
    size_t count = 0;
    float minP = 0.0f;
    float maxP = 0.0f;

    for (ICryptoProvider* provider : providers) {
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
    
    uint32_t now = millis();
    uint32_t durationMs = (config_duration_sec > 0 ? config_duration_sec : 5) * 1000;
    if (now - lastItemSwitchTime > durationMs) {
        lastItemSwitchTime = now;
        if (config_show_chart) {
            if (currentPage == DisplayPage::Info) {
                currentPage = DisplayPage::Chart;
                fetchHistory(symbolList[currentSymbolIndex % symbolList.size()], config_chart_timeframe);
            } else {
                currentPage = DisplayPage::Info;
                symbolsShownThisCycle++;
                currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
                fetchQuote(symbolList[currentSymbolIndex]);
            }
        } else {
            currentPage = DisplayPage::Info;
            symbolsShownThisCycle++;
            currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
            fetchQuote(symbolList[currentSymbolIndex]);
        }
    }
}

bool CryptoEngine::isFinished() const {
    if (symbolList.empty()) return true;
    return (symbolsShownThisCycle >= symbolList.size());
}

void CryptoEngine::render(EngineContext* context) {
    if (symbolList.empty() || !config_enabled) return;
    if (currentPage == DisplayPage::Info) {
        renderQuote(context);
    } else {
        renderChart(context);
    }
}

void CryptoEngine::deactivate() {
}

void CryptoEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;
    config_enabled = engineConfig->getBool("enabled", true);
    config_duration_sec = engineConfig->getInt("duration_sec", 5);
    config_cache_ttl_min = engineConfig->getInt("cache_ttl_min", 15);
    config_show_chart = engineConfig->getBool("show_chart", true);
    config_chart_timeframe = timeframeFromString(engineConfig->getString("chart_timeframe", "daily"));
    String syms = engineConfig->getString("symbols", "BTC,ETH,SOL");
    parseSymbols(syms);
}

void CryptoEngine::renderChart(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();

    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "$%.0f", currentPrice);
    } else if (currentPrice >= 1.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "$%.2f", currentPrice);
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "$%.4f", currentPrice);
    }

    char pctBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(pctBuf, sizeof(pctBuf), "--");
    } else {
        snprintf(pctBuf, sizeof(pctBuf), "%s%.2f%%", changePercent24h >= 0 ? "+" : "", changePercent24h);
    }
    uint16_t badgeColor = (!fetchSuccess || currentPrice <= 0.0f) ? matrix->color565(150, 150, 150) : (changePercent24h >= 0 ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60));

    const char* tfLabel = timeframeLabel(config_chart_timeframe);
    AssetHistoryCache& hist = historyCache[activeSymbol];

    if (mH >= 64) {
        // Header
        matrix->setTextColor(0xFFFF);
        matrix->setTextSize(1);
        matrix->setCursor(6, 4);
        matrix->printf("%s (%s)", activeSymbol.c_str(), tfLabel);

        matrix->setTextColor(matrix->color565(255, 215, 0));
        int priceX = mW - (strlen(priceBuf) * 6 + 6);
        if (priceX < 6 + (int)(activeSymbol.length() + 5) * 6) priceX = 6 + (activeSymbol.length() + 5) * 6;
        matrix->setCursor(priceX, 4);
        matrix->print(priceBuf);

        // Subheader % change
        matrix->setTextColor(badgeColor);
        matrix->setCursor(6, 14);
        matrix->print(pctBuf);

        // Sparkline
        int sparkX = 4;
        int sparkY = 25;
        int sparkW = mW - 8;
        int sparkH = 35;

        if (hist.hasData && hist.count > 1) {
            bool isUp = (hist.points[hist.count - 1] >= hist.points[0]);
            uint16_t lineColor = isUp ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60);
            uint16_t fillColor = isUp ? matrix->color565(0, 30, 10) : matrix->color565(35, 10, 10);
            SparklineRenderer::drawSparkline(matrix, hist.points, hist.count, hist.minPrice, hist.maxPrice, sparkX, sparkY, sparkW, sparkH, lineColor, fillColor);
        } else {
            matrix->setTextColor(matrix->color565(120, 120, 120));
            matrix->setCursor(6, sparkY + 10);
            matrix->print("Loading chart...");
        }
    } else {
        // 32px height panel
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
        int sparkY = 12;
        int sparkW = mW - 4;
        int sparkH = 19;

        if (hist.hasData && hist.count > 1) {
            bool isUp = (hist.points[hist.count - 1] >= hist.points[0]);
            uint16_t lineColor = isUp ? matrix->color565(0, 255, 120) : matrix->color565(255, 60, 60);
            uint16_t fillColor = isUp ? matrix->color565(0, 30, 10) : matrix->color565(35, 10, 10);
            SparklineRenderer::drawSparkline(matrix, hist.points, hist.count, hist.minPrice, hist.maxPrice, sparkX, sparkY, sparkW, sparkH, lineColor, fillColor);
        } else {
            matrix->setTextColor(matrix->color565(120, 120, 120));
            matrix->setCursor(4, sparkY + 4);
            matrix->print("Loading...");
        }
    }
}

void CryptoEngine::renderQuote(EngineContext* context) {
    auto* matrix = context->getMatrix();
    matrix->fillScreen(0);
    int mW = matrix->width();
    int mH = matrix->height();
    
    // Select Icon
    const uint16_t* icon = ICON_BTC_8x8;
    if (activeSymbol == "ETH") icon = ICON_ETH_8x8;
    else if (activeSymbol == "SOL") icon = ICON_SOL_8x8;
    
    // Format Price & Change strings safely
    char priceBuf[32];
    if (!fetchSuccess || currentPrice <= 0.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "Loading...");
    } else if (currentPrice >= 1000.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "$%.0f", currentPrice);
    } else if (currentPrice >= 1.0f) {
        snprintf(priceBuf, sizeof(priceBuf), "$%.2f", currentPrice);
    } else {
        snprintf(priceBuf, sizeof(priceBuf), "$%.4f", currentPrice);
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
        matrix->setTextColor(matrix->color565(255, 215, 0)); // Bright Gold
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
        
        matrix->setTextColor(matrix->color565(255, 215, 0));
        matrix->setCursor(20 + activeSymbol.length() * 6 + 6, 4);
        matrix->print(priceBuf);
        
        // Change Badge bottom line (Size 1)
        matrix->setTextColor(badgeColor);
        matrix->setCursor(20, 18);
        matrix->print(pctBuf);
    }
}

EngineDescriptor CryptoEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_crypto;
    desc_crypto.metadata = {"crypto", "Crypto Tracker", "finance", FIRMWARE_VERSION};
    desc_crypto.capabilities.realtime = false;
    desc_crypto.capabilities.allowsOverlay = true;
    desc_crypto.requirements.needsPsram = true;
    desc_crypto.requirements.needsNetwork = true;
    desc_crypto.schema.fields = {
        ConfigField("symbols", ConfigType::STRING, "Symbols", "Comma-separated crypto symbols", "BTC,ETH,SOL", true, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
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

