#include "StockEngine.h"
#include "../core/Logger.h"

StockEngine::StockEngine() 
    : matrix(nullptr), currentSymbolIndex(0), lastItemSwitchTime(0),
      currentPrice(0.0f), changePercent24h(0.0f), fetchSuccess(false) {}

void StockEngine::begin(MatrixPanel_I2S_DMA* display) {
    matrix = display;
}

void StockEngine::updateConfig(const StockConfig& cfg) {
    config = cfg;
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

void StockEngine::onDisplayStart() {
    lastItemSwitchTime = millis();
    if (!symbolList.empty()) {
        fetchQuote(symbolList[currentSymbolIndex % symbolList.size()]);
    }
}

void StockEngine::fetchQuote(const String& symbol) {
    activeSymbol = symbol;
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
    bool fetched = false;
    
    // Yahoo Finance v8 Chart API (does not require crumb/cookie, bypasses 401 error)
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
    LOGI("StockEngine", "Fetching live stock quote for %s...", symbol.c_str());
    
    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    int code = -1;
    if (http.begin(url)) {
        code = http.GET();
        if (code != 200) {
            http.end();
            // Fallback to query2 if query1 failed
            String fallbackUrl = "https://query2.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
            if (http.begin(fallbackUrl)) {
                code = http.GET();
            }
        }
        
        if (code == 200) {
            String payload = http.getString();
            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, payload);
            if (!err) {
                JsonObject meta = doc["chart"]["result"][0]["meta"];
                newPrice = meta["regularMarketPrice"] | 0.0f;
                float prevClose = meta["previousClose"] | meta["chartPreviousClose"] | newPrice;
                if (prevClose > 0.0f && newPrice > 0.0f) {
                    newChange = ((newPrice - prevClose) / prevClose) * 100.0f;
                } else {
                    newChange = 0.0f;
                }
                if (newPrice > 0.0f) fetched = true;
            }
        }
        http.end();
    }
    
    if (fetched && newPrice > 0.0f) {
        cache.price = newPrice;
        cache.changePercent24h = newChange;
        cache.lastFetchTime = now;
        cache.hasData = true;
        
        currentPrice = newPrice;
        changePercent24h = newChange;
        fetchSuccess = true;
        LOGI("StockEngine", "[Fetch Success] Updated stock cache for %s: $%.2f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
    } else if (cache.hasData) {
        // Fallback to last known cached price for THIS symbol if HTTP failed
        currentPrice = cache.price;
        changePercent24h = cache.changePercent24h;
        fetchSuccess = true;
        LOGW("StockEngine", "[HTTP Failed] Reusing last known cached price for %s: $%.2f", symbol.c_str(), currentPrice);
    } else {
        currentPrice = 0.0f;
        changePercent24h = 0.0f;
        fetchSuccess = false;
        LOGW("StockEngine", "No stock quote available for %s", symbol.c_str());
    }
}

bool StockEngine::loop() {
    if (!matrix || symbolList.empty()) return false;
    
    uint32_t durationMs = (config.duration_sec > 0 ? config.duration_sec : 5) * 1000;
    if (millis() - lastItemSwitchTime > durationMs) {
        lastItemSwitchTime = millis();
        currentSymbolIndex = (currentSymbolIndex + 1) % symbolList.size();
        fetchQuote(symbolList[currentSymbolIndex]);
    }
    
    renderQuote();
    return true;
}

void StockEngine::renderQuote() {
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
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint16_t color = icon[y * 8 + x];
                if (color != 0) {
                    matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
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
        
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint16_t color = icon[y * 8 + x];
                if (color != 0) {
                    matrix->fillRect(iconX + (x * 2), iconY + (y * 2), 2, 2, color);
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
