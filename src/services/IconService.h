/**
 * @file IconService.h
 * @brief Centralized service for fetching, SD caching, and decoding asset icons (Crypto, Stock, Media, etc.).
 */
#pragma once

#include <Arduino.h>
#include <vector>
#include <map>

/**
 * @class IconService
 * @brief Memory-conscious, non-blocking icon downloader and decoder.
 *
 * Responsibilities:
 * - Directs icon downloads through plain HTTP weserv proxy (0 TLS overhead, ~1.5 KB RAM).
 * - Persists icons on SD storage under /<category>_icons/<symbol>.png.
 * - Guards PNGdec instantiations against DRAM fragmentation (sizeof(PNG) ~45.6 KB).
 * - Decodes directly into caller-owned fixed RGB565 buffers (e.g. 16x16 uint16_t arrays).
 * - Implements resilient fallback candidate URLs and negative caching (1-hour cooldown).
 */
class IconService {
public:
    IconService() = default;
    ~IconService() = default;

    /**
     * @brief Normalizes a symbol into a filesystem-safe identifier (e.g. "BTC" -> "btc", "^GSPC" -> "gspc").
     */
    static String sanitizeSymbol(const String& symbol);

    /**
     * @brief Constructs the SD path for a cached icon (e.g. "/crypto_icons/btc.png").
     */
    static String getSdPath(const char* category, const String& symbol);

    /**
     * @brief Checks if the icon already exists on the SD card.
     */
    bool hasIconOnSd(const char* category, const String& symbol) const;

    /**
     * @brief Downloads an icon via plain HTTP weserv proxy directly into the SD card cache file.
     * Uses ~1.5 KB RAM (plain HTTP, no TLS).
     * @return true if file now exists on SD.
     */
    bool downloadIconToSd(const char* category, const String& symbol, const String& rawUrl, int width = 16, int height = 16);

    /**
     * @brief Decodes a PNG icon from SD into a destination RGB565 buffer.
     * @param category Category folder name (e.g. "crypto", "stock")
     * @param symbol Symbol name
     * @param outPixels Caller-allocated RGB565 pixel buffer (e.g. width * height uint16_t array)
     * @param width Desired width
     * @param height Desired height
     * @return true if successfully decoded into outPixels
     */
    bool decodeIconFromSd(const char* category, const String& symbol, uint16_t* outPixels, int width = 16, int height = 16);

    /**
     * @brief Unified convenience method: checks SD, downloads if missing, and decodes into outPixels.
     * @param category Category folder name (e.g. "crypto", "stock", "media")
     * @param symbol Symbol name
     * @param rawUrl Image URL (if empty, default provider URL candidates are used)
     * @param outPixels Caller-allocated RGB565 pixel buffer
     * @param width Desired width
     * @param height Desired height
     * @return true if outPixels has been populated with valid icon data.
     */
    bool loadOrFetchIcon(const char* category, const String& symbol, const String& rawUrl,
                         uint16_t* outPixels, int width = 16, int height = 16);

    /**
     * @brief Resolves and loads an icon for a market ticker which might be either crypto or stock.
     * Checks both /crypto_icons/ and /stock_icons/ on SD, and falls back to candidate URLs.
     */
    bool loadOrFetchMarketIcon(const String& symbol, const String& rawUrl,
                               uint16_t* outPixels, int width = 16, int height = 16);

    /**
     * @brief Clears negative cache entries (failed attempts).
     */
    void clearNegativeCache();

private:
    std::map<String, uint32_t> m_failedAttempts;
};

extern IconService iconService;
