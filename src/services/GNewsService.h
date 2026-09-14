#pragma once
#include <Arduino.h>
#include <vector>
#include <cstring>

#ifndef GNEWS_ARTICLE_H
#define GNEWS_ARTICLE_H
struct GNewsArticle {
    char title[256];
    char description[384];
    char source[48];
    char category[24];
    uint32_t publishedEpoch;
    uint16_t badgeColor;
};

static constexpr size_t GNEWS_MAX_ARTICLES = 10;

struct GNewsSnapshot {
    // Lazily allocated (PSRAM first). Keeping the 10 fixed-size articles inline made the global
    // GNewsService object a 7.3 KB .bss block permanently reserved in internal DRAM, even when the
    // GNews engine was never instantiated. Internal DRAM is the scarce resource on this board:
    // mbedTLS forces every allocation of 4 KB or less into it, so that static cost was directly
    // eating the budget TLS handshakes need. Indexing syntax at call sites is unchanged.
    GNewsArticle* articles = nullptr;
    size_t count = 0;
    uint32_t lastFetchTime = 0;
    uint32_t lastFetchEpoch = 0;
    bool hasData = false;
    bool fetchSuccess = false;
    uint8_t status = 1; // 0=OK, 1=EMPTY_KEY, 2=INVALID_KEY, 3=RATE_LIMITED, 4=NETWORK_ERROR, 5=LOADING
};
#endif

/**
 * @class GNewsService
 * @brief Autonomous background service fetching top headlines and search articles from GNews API.
 */
class GNewsService {
public:
    GNewsService();
    ~GNewsService();

    void fetchNews(const String& apiKey, const String& category, const String& keywords,
                   const String& lang, const String& country, int maxArticles, int cacheTtlMin,
                   int requestsPerDay = 10, bool forceRefresh = false);

    const GNewsSnapshot& getSnapshot() const;
    bool hasData() const;
    void purgeArticles();
    void loadFromSd();
    void saveToSd();
    String getQuotaStatusString() const;

    static uint16_t getCategoryColor(const char* category);

private:
    GNewsSnapshot _snapshot;
    String _lastApiKey;
    String _lastCategory;
    String _lastKeywords;
    String _lastLang;
    String _lastCountry;
    int _lastMaxArticles = 5;
    int _lastRequestsPerDay = 10;
    size_t _catRoundRobinIdx = 0;
    size_t _activeKeyIdx = 0;
    std::vector<String> _apiKeys;
    std::vector<uint32_t> _keyUsages;
    int _lastFetchDay = -1;
    bool _loadedFromSd = false;

    bool parseGNewsJson(const String& payload, const char* defaultCategory);

    /**
     * @brief Allocate the article storage on first use, preferring PSRAM.
     * @return true when snapshot.articles is usable, false when allocation failed.
     */
    bool ensureArticleStorage();

    /** @brief Release the article storage and reset the snapshot counters. */
    void releaseArticleStorage();
};

extern GNewsService gnewsService;
