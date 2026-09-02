#pragma once
#include <Arduino.h>
#include <vector>
#include <cstring>

#ifndef GNEWS_ARTICLE_H
#define GNEWS_ARTICLE_H
struct GNewsArticle {
    char title[256];
    char source[48];
    char category[24];
    uint32_t publishedEpoch;
    uint16_t badgeColor;
};

struct GNewsSnapshot {
    GNewsArticle articles[10];
    size_t count = 0;
    uint32_t lastFetchTime = 0;
    bool hasData = false;
    bool fetchSuccess = false;
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
                   const String& lang, const String& country, int maxArticles, int cacheTtlMin);

    GNewsSnapshot getSnapshot() const;
    bool hasData() const;

    static uint16_t getCategoryColor(const char* category);

private:
    GNewsSnapshot _snapshot;
    String _lastApiKey;
    String _lastCategory;
    String _lastKeywords;
    String _lastLang;
    String _lastCountry;
    int _lastMaxArticles = 5;

    bool parseGNewsJson(const String& payload, const char* defaultCategory);
    void populateDemoArticles(const char* category);
};

extern GNewsService gnewsService;
