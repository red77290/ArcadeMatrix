#include "CryptoEngine.h"
#include "../core/Logger.h"

CryptoEngine::CryptoEngine() 
    : matrix(nullptr), currentSymbolIndex(0), lastItemSwitchTime(0), lastFetchTime(0),
      currentPrice(0.0f), changePercent24h(0.0f), fetchSuccess(false) {}

void CryptoEngine::begin(MatrixPanel_I2S_DMA* display) {
    matrix = display;
}

void CryptoEngine::updateConfig(const CryptoConfig& cfg) {
    config = cfg;
    parseSymbols();
}

void CryptoEngine::parseSymbols() {
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
        symbolList.push_back("BTC");
        symbolList.push_back("ETH");
        symbolList.push_back("SOL");
        symbolList.push_back("DOGE");
    }
}

void CryptoEngine::onDisplayStart() {
    lastItemSwitchTime = millis();
    if (!symbolList.empty()) {
        fetchQuote(symbolList[currentSymbolIndex % symbolList.size()]);
    }
}

void CryptoEngine::fetchQuote(const String& symbol) {
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
        LOGI("CryptoEngine", "[Cache Hit] Using cached quote for %s: $%.4f (%.2f%%)", symbol.c_str(), currentPrice, changePercent24h);
        return;
    }
    
    // Reset transient price for fresh fetch attempt
    float newPrice = 0.0f;
    float newChange = 0.0f;
    bool fetched = false;
    
    String lowerSymbol = symbol;
    lowerSymbol.toLowerCase();
    
    HTTPClient http;
    http.setTimeout(3000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    // 2a. PRIMARY API: CoinGecko Markets API by Symbol
    String cgUrl = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&symbols=" + lowerSymbol;
    LOGI("CryptoEngine", "[CoinGecko Primary] Fetching live crypto quote for %s...", symbol.c_str());
    
    int code = -1;
    if (http.begin(cgUrl)) {
        code = http.GET();
        if (code == 200) {
            String payload = http.getString();
            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, payload);
            if (!err && doc.is<JsonArray>() && doc.size() > 0) {
                JsonObject coin = doc[0];
                newPrice = coin["current_price"] | 0.0f;
                newChange = coin["price_change_percentage_24h"] | 0.0f;
                if (newPrice > 0.0f) fetched = true;
            }
        }
        http.end();
    }
    
    // 2b. CoinGecko Simple Price API by Coin ID (handles ERGO, FLUX, KASPA, etc.)
    if (!fetched) {
        String coinId = lowerSymbol;
        if (lowerSymbol == "erg") coinId = "ergo";
        
        String cgSimpleUrl = "https://api.coingecko.com/api/v3/simple/price?ids=" + coinId + "&vs_currencies=usd&include_24hr_change=true";
        LOGI("CryptoEngine", "[CoinGecko ID Fallback] Fetching quote for ID %s...", coinId.c_str());
        if (http.begin(cgSimpleUrl)) {
            code = http.GET();
            if (code == 200) {
                String payload = http.getString();
                DynamicJsonDocument doc(1024);
                DeserializationError err = deserializeJson(doc, payload);
                if (!err && doc.containsKey(coinId)) {
                    JsonObject item = doc[coinId];
                    newPrice = item["usd"] | 0.0f;
                    newChange = item["usd_24h_change"] | 0.0f;
                    if (newPrice > 0.0f) fetched = true;
                }
            }
            http.end();
        }
    }
    
    // 2c. FALLBACK: Binance API
    if (!fetched) {
        String apiSymbol = symbol;
        if (!apiSymbol.endsWith("USDT") && !apiSymbol.endsWith("USD")) {
            apiSymbol += "USDT";
        }
        String binanceUrl = "https://api.binance.com/api/v3/ticker/24hr?symbol=" + apiSymbol;
        LOGI("CryptoEngine", "[Binance Fallback] Fetching live quote for %s...", apiSymbol.c_str());
        
        if (http.begin(binanceUrl)) {
            code = http.GET();
            if (code == 200) {
                String payload = http.getString();
                StaticJsonDocument<512> doc;
                DeserializationError err = deserializeJson(doc, payload);
                if (!err) {
                    newPrice = doc["lastPrice"] | 0.0f;
                    newChange = doc["priceChangePercent"] | 0.0f;
                    if (newPrice > 0.0f) fetched = true;
                }
            }
            http.end();
        }
    }
    
    // Update cache if successful
    if (fetched && newPrice > 0.0f) {
        cache.price = newPrice;
        cache.changePercent24h = newChange;
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

bool CryptoEngine::loop() {
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

void CryptoEngine::renderQuote() {
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
        
        matrix->setTextColor(matrix->color565(255, 215, 0));
        matrix->setCursor(20 + activeSymbol.length() * 6 + 6, 4);
        matrix->print(priceBuf);
        
        // Change Badge bottom line (Size 1)
        matrix->setTextColor(badgeColor);
        matrix->setCursor(20, 18);
        matrix->print(pctBuf);
    }
}
