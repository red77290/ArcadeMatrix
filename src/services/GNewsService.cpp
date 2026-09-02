#include "GNewsService.h"
#include "../core/Logger.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

GNewsService gnewsService;

GNewsService::GNewsService() {
    _snapshot.count = 0;
    _snapshot.hasData = false;
    _snapshot.fetchSuccess = false;
    _snapshot.lastFetchTime = 0;
}

GNewsService::~GNewsService() {}

GNewsSnapshot GNewsService::getSnapshot() const {
    return _snapshot;
}

bool GNewsService::hasData() const {
    return _snapshot.hasData && _snapshot.count > 0;
}

uint16_t GNewsService::getCategoryColor(const char* category) {
    if (!category) return 0xDEFB; // Cool white
    String cat = String(category);
    cat.toLowerCase();

    if (cat.indexOf("world") >= 0 || cat.indexOf("nation") >= 0 || cat.indexOf("break") >= 0) {
        return 0xF949; // Crimson Red
    } else if (cat.indexOf("tech") >= 0) {
        return 0x073F; // Electric Cyan
    } else if (cat.indexOf("bus") >= 0 || cat.indexOf("fin") >= 0 || cat.indexOf("econ") >= 0) {
        return 0x072E; // Emerald Green
    } else if (cat.indexOf("sport") >= 0) {
        return 0xFC80; // Amber Orange
    } else if (cat.indexOf("sci") >= 0) {
        return 0xD01F; // Cosmic Purple
    } else if (cat.indexOf("ent") >= 0 || cat.indexOf("art") >= 0) {
        return 0xFA10; // Hot Pink
    } else if (cat.indexOf("heal") >= 0) {
        return 0x1F56; // Seafoam Teal
    }
    return 0xDEFB; // Crisp Cool White
}

void GNewsService::populateDemoArticles(const char* category) {
    _snapshot.count = 0;
    const char* cat = (category && strlen(category) > 0) ? category : "General";
    uint16_t col = getCategoryColor(cat);

    auto addDemo = [&](const char* title, const char* source) {
        if (_snapshot.count < 10) {
            GNewsArticle& a = _snapshot.articles[_snapshot.count++];
            strncpy(a.title, title, sizeof(a.title) - 1);
            a.title[sizeof(a.title) - 1] = '\0';
            strncpy(a.source, source, sizeof(a.source) - 1);
            a.source[sizeof(a.source) - 1] = '\0';
            strncpy(a.category, cat, sizeof(a.category) - 1);
            a.category[sizeof(a.category) - 1] = '\0';
            a.publishedEpoch = 0;
            a.badgeColor = col;
        }
    };

    String c = String(cat);
    c.toLowerCase();
    if (c.indexOf("tech") >= 0) {
        addDemo("Quantum computing milestone achieved with 1,000-qubit coherence", "TechCrunch");
        addDemo("Next-generation neural architecture boosts edge efficiency by 40%", "The Verge");
        addDemo("Retro arcade preservation initiative restores classic raster titles", "Ars Technica");
    } else if (c.indexOf("sci") >= 0) {
        addDemo("James Webb Space Telescope detects organic molecules in distant galaxy", "Nature");
        addDemo("New fusion containment record sets path for clean grid power", "Science Daily");
    } else if (c.indexOf("world") >= 0) {
        addDemo("International summit reaches landmark agreement on clean energy standards", "Reuters");
        addDemo("Global maritime corridor introduces automated zero-emission transit", "BBC News");
    } else {
        addDemo("Global technology summit unveils revolutionary advancements in AI & robotics", "BBC News");
        addDemo("Autonomous exploration vessel reaches uncharted deep-sea ecosystem", "Reuters");
        addDemo("Historic vintage gaming tournament attracts worldwide championship players", "IGN");
    }

    _snapshot.hasData = true;
    _snapshot.fetchSuccess = true;
    _snapshot.lastFetchTime = millis();
}

bool GNewsService::parseGNewsJson(const String& payload, const char* defaultCategory) {
    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        LOGE("GNewsService", "JSON deserialize error: %s", error.c_str());
        return false;
    }

    JsonArray articles = doc["articles"].as<JsonArray>();
    if (articles.isNull() || articles.size() == 0) {
        LOGW("GNewsService", "No articles returned in JSON payload");
        return false;
    }

    _snapshot.count = 0;
    const char* defCat = (defaultCategory && strlen(defaultCategory) > 0) ? defaultCategory : "News";
    uint16_t catColor = getCategoryColor(defCat);

    for (JsonObject obj : articles) {
        if (_snapshot.count >= 10) break;
        const char* title = obj["title"] | "";
        if (!title || strlen(title) == 0) continue;

        GNewsArticle& art = _snapshot.articles[_snapshot.count++];
        strncpy(art.title, title, sizeof(art.title) - 1);
        art.title[sizeof(art.title) - 1] = '\0';

        const char* sourceName = obj["source"]["name"] | "News";
        strncpy(art.source, sourceName, sizeof(art.source) - 1);
        art.source[sizeof(art.source) - 1] = '\0';

        strncpy(art.category, defCat, sizeof(art.category) - 1);
        art.category[sizeof(art.category) - 1] = '\0';

        art.publishedEpoch = 0; // Relative timestamp calculated dynamically
        art.badgeColor = catColor;
    }

    if (_snapshot.count > 0) {
        _snapshot.hasData = true;
        _snapshot.fetchSuccess = true;
        _snapshot.lastFetchTime = millis();
        LOGI("GNewsService", "Successfully parsed %d live articles for category '%s'", (int)_snapshot.count, defCat);
        return true;
    }
    return false;
}

void GNewsService::fetchNews(const String& apiKey, const String& category, const String& keywords,
                            const String& lang, const String& country, int maxArticles, int cacheTtlMin) {
    uint32_t now = millis();
    uint32_t ttlMs = (cacheTtlMin > 0 ? cacheTtlMin : 30) * 60 * 1000UL;

    bool configChanged = (apiKey != _lastApiKey || category != _lastCategory || keywords != _lastKeywords ||
                          lang != _lastLang || country != _lastCountry || maxArticles != _lastMaxArticles);

    if (!configChanged && _snapshot.hasData && (now - _snapshot.lastFetchTime < ttlMs)) {
        return; // Cache valid
    }

    _lastApiKey = apiKey;
    _lastCategory = category;
    _lastKeywords = keywords;
    _lastLang = lang;
    _lastCountry = country;
    _lastMaxArticles = maxArticles;

    if (WiFi.status() != WL_CONNECTED) {
        if (!_snapshot.hasData) {
            populateDemoArticles(category.c_str());
        }
        return;
    }

    if (apiKey.length() == 0) {
        // No API key provided -> use clean demo articles for immediate out-of-the-box enjoyment
        populateDemoArticles(category.c_str());
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);

    String url = "https://gnews.io/api/v4/";
    if (keywords.length() > 0) {
        url += "search?q=" + keywords;
    } else {
        url += "top-headlines?category=" + (category.length() > 0 ? category : "general");
    }

    if (lang.length() > 0 && lang != "auto") {
        url += "&lang=" + lang;
    }
    if (country.length() > 0 && country != "auto") {
        url += "&country=" + country;
    }
    int count = (maxArticles >= 1 && maxArticles <= 10) ? maxArticles : 5;
    url += "&max=" + String(count);
    url += "&apikey=" + apiKey;

    LOGI("GNewsService", "Fetching live news: %s (key hidden)", keywords.length() > 0 ? keywords.c_str() : category.c_str());

    if (http.begin(client, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            if (!parseGNewsJson(payload, category.c_str())) {
                if (!_snapshot.hasData) populateDemoArticles(category.c_str());
            }
        } else {
            LOGW("GNewsService", "HTTP GET failed with code: %d", httpCode);
            if (!_snapshot.hasData) populateDemoArticles(category.c_str());
        }
        http.end();
    } else {
        LOGE("GNewsService", "Unable to connect to GNews endpoint");
        if (!_snapshot.hasData) populateDemoArticles(category.c_str());
    }
}
