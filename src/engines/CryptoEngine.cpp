#include "CryptoEngine.h"
#include "../hal/HardwareHAL.h"
#include "../core/Logger.h"
#include "../core/SDUtils.h"
#include "../core/SdLockGuard.h"
#include "../api/CoinGeckoProvider.h"
#include "../api/BinanceProvider.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <esp_task_wdt.h>

CryptoEngine* CryptoEngine::instance = nullptr;

CryptoEngine::CryptoEngine() 
    : currentSymbolIndex(0), lastItemSwitchTime(0), lastFetchTime(0),
      currentPrice(0.0f), changePercent24h(0.0f), fetchSuccess(false), currentDecodeBuffer(nullptr) {
    instance = this;
    m_binance = new BinanceProvider();
    addProvider(m_binance);
    addProvider(new CoinGeckoProvider());
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
    m_renderedFirstFrame = false;
    if (!symbolList.empty()) {
        activeSymbol = symbolList[currentSymbolIndex % symbolList.size()];
        AssetQuoteCache& cache = quoteCache[activeSymbol];
        if (cache.hasData) {
            currentPrice = cache.price;
            changePercent24h = cache.changePercent24h;
            fetchSuccess = true;
        } else {
            currentPrice = 0.0f;
            changePercent24h = 0.0f;
            fetchSuccess = false;
        }
    }
    
    // Preload icons for all active symbols while heap is clean (before TLS handshakes fragment contiguous DRAM)
    if (WiFi.isConnected()) {
        for (const auto& sym : symbolList) {
            AssetQuoteCache& c = quoteCache[sym];
            if (!c.hasIcon && !c.iconAttempted) {
                loadOrDownloadIcon(sym, c.imageUrl, c);
            }
        }
    }
    
    requestRedraw();
}

void CryptoEngine::loadOrDownloadIcon(const String& symbol, const String& newImgUrl, AssetQuoteCache& cache) {
    if (cache.hasIcon || cache.iconAttempted) return;
    
    String safeName = symbol;
    safeName.toLowerCase();
    String sdPath = "/crypto_icons/" + safeName + ".png";
    
    bool onSd = false;
    {
        SdLockGuard guard(pdMS_TO_TICKS(1500));
        if (guard && sd.exists(sdPath.c_str())) {
            onSd = true;
        }
    }
    
    // Download via plain HTTP weserv proxy (no TLS) if not already cached on SD.
    // Plain HTTP uses ~1.5 KB RAM and does not require contiguous DRAM for PNGdec.
    if (!onSd && WiFi.isConnected()) {
        esp_task_wdt_reset();
        String imgUrl = newImgUrl;
        if (imgUrl.isEmpty()) {
            imgUrl = "assets.coincap.io/assets/icons/" + safeName + "@2x.png";
        }
        String proxyUrl = "http://images.weserv.nl/?url=" + imgUrl + "&w=16&h=16&output=png";
        LOGI("CryptoEngine", "Downloading crypto logo for %s via proxy: %s", symbol.c_str(), proxyUrl.c_str());
        
        HTTPClient httpImg;
        WiFiClient imgClient;
        httpImg.setTimeout(3000);
        httpImg.setUserAgent("Mozilla/5.0 ArcadeMatrix/3.4");
        
        if (httpImg.begin(imgClient, proxyUrl)) {
            int code = httpImg.GET();
            if (code == 200) {
                int len = httpImg.getSize();
                if (len > 0 && len < 16384) {
                    SdLockGuard guard(pdMS_TO_TICKS(1500));
                    if (guard) {
                        if (!sd.exists("/crypto_icons")) sd.mkdir("/crypto_icons");
                        FsFile f = sd.open(sdPath.c_str(), FILE_OPEN_WRITE);
                        if (f) {
                            httpImg.writeToStream(&f);
                            f.close();
                            onSd = true;
                        }
                    }
                }
            }
            httpImg.end();
            imgClient.stop();
        }
        esp_task_wdt_reset();
    }
    
    // Mark icon as attempted to prevent retrying on every 16ms frame if decode or download fails
    cache.iconAttempted = true;

    // Decode from SD card into cache.iconPixels (16x16 RGB565)
    // Invariant: PNGdec allocates ~45KB internally; check contiguous DRAM before instantiation
    if (onSd) {
        if (ESP.getMaxAllocHeap() < sizeof(PNG)) {
            LOGW("CryptoEngine", "Skipping icon decode for %s: insufficient contiguous heap (largest=%u, need ~%u bytes)",
                 symbol.c_str(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)sizeof(PNG));
            return;
        }
        PNG* png = new (std::nothrow) PNG();
        if (!png) {
            LOGW("CryptoEngine", "Skipping icon decode for %s: out of memory for PNGdec", symbol.c_str());
            return;
        }

        size_t size = 0;
        uint8_t* buf = nullptr;
        {
            SdLockGuard guard(pdMS_TO_TICKS(1500));
            if (guard && sd.exists(sdPath.c_str())) {
                FsFile f = sd.open(sdPath.c_str(), FILE_OPEN_READ);
                if (f) {
                    size = f.size();
                    if (size > 0 && size <= 16384) {
                        buf = (uint8_t*)malloc(size);
                        if (buf) {
                            f.read(buf, size);
                        }
                    }
                    f.close();
                }
            }
        }
        
        if (buf && size > 0) {
            memset(cache.iconPixels, 0, sizeof(cache.iconPixels));
            currentDecodeBuffer = cache.iconPixels;
            
            pngPtr = png;
            int rc = png->openRAM(buf, size, pngDraw);
            if (rc == PNG_SUCCESS) {
                png->decode((void*)this, 0);
                cache.hasIcon = true;
                LOGI("CryptoEngine", "Successfully loaded 16x16 icon for %s", symbol.c_str());
            } else {
                LOGW("CryptoEngine", "Failed to decode PNG for %s (rc=%d)", symbol.c_str(), rc);
            }
            png->close();
            pngPtr = nullptr;
            free(buf);
        } else if (buf) {
            free(buf);
        }
        delete png;
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
        if (!cache.hasIcon) {
            loadOrDownloadIcon(symbol, cache.imageUrl, cache);
        }
        requestRedraw();
        LOGI("CryptoEngine", "[Cache Hit] Using cached quote for %s: $%.4f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
        return;
    }
    
    esp_task_wdt_reset();
    float newPrice = 0.0f;
    float newChange = 0.0f;
    String newImgUrl = "";
    bool fetched = false;
    
    for (size_t i = 0; i < providers.size(); i++) {
        size_t idx = (config_provider == "binance") ? i : (providers.size() - 1 - i);
        ICryptoProvider* provider = providers[idx];
        provider->setCurrency(config_currency);
        if (provider->fetchQuote(symbol, newPrice, newChange, newImgUrl)) {
            fetched = true;
            break;
        }
    }
    esp_task_wdt_reset();
    
    if (fetched && newPrice > 0.0f) {
        cache.price = newPrice;
        cache.changePercent24h = newChange;
        cache.imageUrl = newImgUrl;
        cache.lastFetchTime = now;
        cache.hasData = true;
        
        currentPrice = newPrice;
        changePercent24h = newChange;
        fetchSuccess = true;
        if (!cache.hasIcon) {
            loadOrDownloadIcon(symbol, newImgUrl, cache);
        }
        requestRedraw();
        LOGI("CryptoEngine", "[Fetch Success] Updated cache for %s: $%.4f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
    } else if (cache.hasData) {
        currentPrice = cache.price;
        changePercent24h = cache.changePercent24h;
        fetchSuccess = true;
        requestRedraw();
        LOGW("CryptoEngine", "[HTTP Failed/429] Reusing last known cached price for %s: $%.4f", symbol.c_str(), currentPrice);
    } else {
        currentPrice = 0.0f;
        changePercent24h = 0.0f;
        fetchSuccess = false;
        cache.lastFetchTime = now; // Record attempt time to prevent continuous frame re-fetch
        requestRedraw();
        LOGW("CryptoEngine", "No quote available for %s", symbol.c_str());
    }
}

int CryptoEngine::pngDraw(PNGDRAW *pDraw) {
    CryptoEngine* self = static_cast<CryptoEngine*>(pDraw->pUser);
    if (!self) self = instance;
    if (!self || !self->currentDecodeBuffer || !self->pngPtr) return 0;
    
    int iWidth = pDraw->iWidth;
    if (iWidth > 16) iWidth = 16;
    
    int y = pDraw->y;
    if (y >= 16) return 0;
    
    uint16_t lineBuffer[16];
    self->pngPtr->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
    
    for (int x = 0; x < iWidth; x++) {
        uint16_t color = lineBuffer[x];
        if (color != 0) {
            self->currentDecodeBuffer[y * 16 + x] = color;
        } else {
            self->currentDecodeBuffer[y * 16 + x] = 0x0000;
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
        requestRedraw();
        LOGI("CryptoEngine", "[Cache Hit] Using cached history for %s (%s)", symbol.c_str(), timeframeLabel(tf));
        return;
    }
    
    esp_task_wdt_reset();
    float points[64];
    size_t count = 0;
    float minP = 0.0f;
    float maxP = 0.0f;
    
    for (size_t i = 0; i < providers.size(); i++) {
        size_t idx = (config_provider == "binance") ? i : (providers.size() - 1 - i);
        ICryptoProvider* provider = providers[idx];
        provider->setCurrency(config_currency);
        if (provider->fetchHistory(symbol, tf, points, 64, count, minP, maxP)) {
            memcpy(cache.points, points, count * sizeof(float));
            cache.count = count;
            cache.minPrice = minP;
            cache.maxPrice = maxP;
            cache.lastFetchTime = now;
            cache.hasData = true;
            requestRedraw();
            LOGI("CryptoEngine", "[History Success] Fetched %d points for %s (%s)", (int)count, symbol.c_str(), timeframeLabel(tf));
            break;
        }
    }
    if (!cache.hasData) {
        cache.lastFetchTime = now; // Guard against instant re-fetch loop
    }
    esp_task_wdt_reset();
}

bool CryptoEngine::fetchCombined(const String& symbol) {
    if (!m_binance) return false;
    uint32_t now = millis();
    AssetQuoteCache& qCache = quoteCache[symbol];
    String histKey = symbol + "_" + timeframeLabel(config_chart_timeframe);
    AssetHistoryCache& hCache = historyCache[histKey];

    m_binance->setCurrency(config_currency);
    float newPrice = 0.0f;
    float newChange = 0.0f;
    String newImgUrl = "";
    float points[64];
    size_t count = 0;
    float minP = 0.0f;
    float maxP = 0.0f;

    bool ok = m_binance->fetchQuoteAndHistory(symbol, newPrice, newChange, newImgUrl,
                                              config_chart_timeframe, points, 64, count, minP, maxP);
    if (ok && newPrice > 0.0f) {
        qCache.price = newPrice;
        qCache.changePercent24h = newChange;
        qCache.imageUrl = newImgUrl;
        qCache.lastFetchTime = now;
        qCache.hasData = true;

        currentPrice = newPrice;
        changePercent24h = newChange;
        fetchSuccess = true;

        if (!qCache.hasIcon) {
            loadOrDownloadIcon(symbol, newImgUrl, qCache);
        }

        if (count > 0) {
            memcpy(hCache.points, points, count * sizeof(float));
            hCache.count = count;
            hCache.minPrice = minP;
            hCache.maxPrice = maxP;
            hCache.lastFetchTime = now;
            hCache.hasData = true;
            LOGI("CryptoEngine", "[Combined Success] Fetched quote + %d history points for %s", (int)count, symbol.c_str());
        } else {
            hCache.lastFetchTime = now;
        }
        requestRedraw();
        return true;
    }
    return false;
}

void CryptoEngine::update(EngineContext* context) {
    if (symbolList.empty() || !config_enabled) return;
    
    // Invariant: Do not perform any blocking network operation before the first frame is rendered and presented!
    if (!m_renderedFirstFrame) return;

    auto* matrix = context ? context->getSurface() : nullptr;
    int mH = matrix ? matrix->height() : 32;
    
    uint32_t now = millis();

    // Check if initial quote for active symbol is missing or expired
    AssetQuoteCache& cache = quoteCache[activeSymbol];
    if (!cache.hasIcon && !cache.iconAttempted) {
        loadOrDownloadIcon(activeSymbol, cache.imageUrl, cache);
    }
    uint32_t ttlMs = (config_cache_ttl_min > 0 ? config_cache_ttl_min : 1) * 60 * 1000;
    bool needsFetch = false;
    if (!cache.hasData) {
        if (cache.lastFetchTime == 0 || (now - cache.lastFetchTime >= 30000UL)) {
            needsFetch = true;
        }
    } else if (now - cache.lastFetchTime >= ttlMs) {
        needsFetch = true;
    }

    if (needsFetch) {
        bool combined = false;
        if (config_provider == "binance" && config_show_chart) {
            combined = fetchCombined(activeSymbol);
        }
        if (!combined) {
            fetchQuote(activeSymbol);
            if (fetchSuccess && config_show_chart) {
                fetchHistory(activeSymbol, config_chart_timeframe);
            }
        }
        requestRedraw();
    }

    uint32_t durationMs = (config_duration_sec > 0 ? config_duration_sec : 5) * 1000;
    if (now - lastItemSwitchTime > durationMs) {
        lastItemSwitchTime = now;
        if (mH >= 64 || !config_show_chart) {
            currentPage = DisplayPage::Info;
            symbolsShownThisCycle++;
            currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
            activeSymbol = symbolList[currentSymbolIndex];
            AssetQuoteCache& nextCache = quoteCache[activeSymbol];
            if (nextCache.hasData) {
                currentPrice = nextCache.price;
                changePercent24h = nextCache.changePercent24h;
                fetchSuccess = true;
            } else {
                currentPrice = 0.0f;
                changePercent24h = 0.0f;
                fetchSuccess = false;
            }
            if (!nextCache.hasIcon && !nextCache.iconAttempted) {
                loadOrDownloadIcon(activeSymbol, nextCache.imageUrl, nextCache);
            }
            bool needFetch = !nextCache.hasData || (now - nextCache.lastFetchTime >= ttlMs);
            if (needFetch) {
                bool combined = false;
                if (config_provider == "binance" && config_show_chart) {
                    combined = fetchCombined(activeSymbol);
                }
                if (!combined) {
                    fetchQuote(activeSymbol);
                    if (fetchSuccess && config_show_chart) {
                        fetchHistory(activeSymbol, config_chart_timeframe);
                    }
                }
            }
        } else {
            if (currentPage == DisplayPage::Info) {
                currentPage = DisplayPage::Chart;
                if (fetchSuccess) {
                    fetchHistory(activeSymbol, config_chart_timeframe);
                }
            } else {
                currentPage = DisplayPage::Info;
                symbolsShownThisCycle++;
                currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
                activeSymbol = symbolList[currentSymbolIndex];
                AssetQuoteCache& nextCache = quoteCache[activeSymbol];
                if (nextCache.hasData) {
                    currentPrice = nextCache.price;
                    changePercent24h = nextCache.changePercent24h;
                    fetchSuccess = true;
                } else {
                    currentPrice = 0.0f;
                    changePercent24h = 0.0f;
                    fetchSuccess = false;
                }
                if (!nextCache.hasIcon && !nextCache.iconAttempted) {
                    loadOrDownloadIcon(activeSymbol, nextCache.imageUrl, nextCache);
                }
                bool needFetch = !nextCache.hasData || (now - nextCache.lastFetchTime >= ttlMs);
                if (needFetch) {
                    bool combined = false;
                    if (config_provider == "binance" && config_show_chart) {
                        combined = fetchCombined(activeSymbol);
                    }
                    if (!combined) {
                        fetchQuote(activeSymbol);
                    }
                }
            }
        }
        requestRedraw();
    }
}

bool CryptoEngine::isFinished() const {
    if (symbolList.empty()) return true;
    return (symbolsShownThisCycle >= symbolList.size());
}

void CryptoEngine::render(EngineContext* context) {
    if (symbolList.empty() || !config_enabled) return;
    if (m_redrawFrames == 0) return;
    m_redrawFrames--;

    m_renderedFirstFrame = true;

    auto* matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return;
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
    // Invariant 15 (Allocation-Free Deactivation): reclaim all heap held by caches
    // using swap idiom (zero new allocation, immediate deallocation).
    std::map<String, AssetQuoteCache>().swap(quoteCache);
    std::map<String, AssetHistoryCache>().swap(historyCache);
    currentPrice = 0.0f;
    changePercent24h = 0.0f;
    fetchSuccess = false;
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
    config_provider = engineConfig->getString("provider", "binance");
    config_provider.toLowerCase();
    if (config_provider.isEmpty()) config_provider = "binance";

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
    requestRedraw();
}

void CryptoEngine::renderUnifiedVertical(EngineContext* context) {
    auto* matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return;
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
    auto* matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return;
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
    auto* matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return;
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
    auto* matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return;
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
    auto* matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return;
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
    desc_crypto.requirements.needsPsram = false;
    desc_crypto.requirements.needsNetwork = true;
    desc_crypto.requirements.needsTls = true;
    desc_crypto.requirements.targetFps = 30;
    desc_crypto.requirements.supportsSingleBuffer = true;
    desc_crypto.requirements.internalPersistentBytes = 12000;
    desc_crypto.requirements.internalContiguousBytes = 16000;
    desc_crypto.requirements.psramBytes = 0;
    desc_crypto.schema.fields = {
        ConfigField("symbols", ConfigType::STRING, "Symbols", "Comma-separated crypto symbols", "BTC,ETH,SOL", true, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("show_chart", ConfigType::BOOLEAN, "Show Chart", "Display historical price sparkline chart", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("chart_timeframe", ConfigType::ENUM, "Chart Timeframe", "Historical chart timeframe", "daily", false, "", "", "", "hourly,daily,weekly,monthly", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("duration_sec", ConfigType::INTEGER, "Page Duration (s)", "Seconds to dwell on each view", "5", false, "3", "30", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("currency", ConfigType::ENUM, "Fiat Currency", "Target currency for quotes", "USD", false, "", "", "", "USD,EUR,GBP,JPY", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("provider", ConfigType::ENUM, "Provider", "Market data provider", "binance", false, "", "", "", "binance,coingecko", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("cache_ttl_min", ConfigType::INTEGER, "Cache TTL (min)", "Minutes between fresh API requests", "5", false, "1", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("crypto_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("crypto_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_crypto.factory = []() { return std::unique_ptr<IEngine>(new CryptoEngine()); };
    return desc_crypto;
}

