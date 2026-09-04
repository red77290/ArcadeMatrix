#include "GNewsService.h"
#include "../core/Logger.h"
#include "../core/I18n.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../core/SDUtils.h"

GNewsService gnewsService;

GNewsService::GNewsService() {
    _snapshot.count = 0;
    _snapshot.hasData = false;
    _snapshot.fetchSuccess = false;
    _snapshot.lastFetchTime = 0;
    _snapshot.status = 1; // EMPTY_KEY
}

GNewsService::~GNewsService() {}

GNewsSnapshot GNewsService::getSnapshot() const {
    return _snapshot;
}

bool GNewsService::hasData() const {
    return _snapshot.hasData && _snapshot.count > 0;
}

void GNewsService::purgeArticles() {
    _snapshot.count = 0;
    _snapshot.hasData = false;
    _snapshot.fetchSuccess = false;
    _snapshot.lastFetchTime = 0;
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

static String cleanNewsText(const char* raw) {
    if (!raw) return "";
    String s = String(raw);
    s.replace("&quot;", "\"");
    s.replace("&apos;", "'");
    s.replace("&#39;", "'");
    s.replace("&amp;", "&");
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&nbsp;", " ");
    s.replace("&#8217;", "'");
    s.replace("&#8216;", "'");
    s.replace("&#8220;", "\"");
    s.replace("&#8221;", "\"");
    s.replace("&#8211;", "-");
    s.replace("&#8212;", "-");
    s.trim();
    return s;
}

void GNewsService::saveToSd() {
    if (!sd.exists("/")) return;
    FsFile f = sd.open("/gnews_cache.json", FILE_OPEN_WRITE);
    if (!f) return;

    DynamicJsonDocument doc(8192);
    doc["last_fetch_time"] = _snapshot.lastFetchTime;
    doc["last_fetch_day"] = _lastFetchDay;
    doc["active_key_idx"] = _activeKeyIdx;
    doc["cat_round_robin_idx"] = _catRoundRobinIdx;
    doc["status"] = _snapshot.status;

    JsonArray usagesArr = doc.createNestedArray("key_usages");
    for (uint32_t u : _keyUsages) {
        usagesArr.add(u);
    }

    JsonArray artArr = doc.createNestedArray("articles");
    for (size_t i = 0; i < _snapshot.count; i++) {
        JsonObject obj = artArr.createNestedObject();
        obj["title"] = _snapshot.articles[i].title;
        obj["source"] = _snapshot.articles[i].source;
        obj["category"] = _snapshot.articles[i].category;
        obj["published_epoch"] = _snapshot.articles[i].publishedEpoch;
    }

    serializeJson(doc, f);
    f.close();
    LOGI("GNewsService", "Persisted %d articles to SD /gnews_cache.json", (int)_snapshot.count);
}

void GNewsService::loadFromSd() {
    _loadedFromSd = true;
    if (!sd.exists("/gnews_cache.json")) return;

    FsFile f = sd.open("/gnews_cache.json", FILE_OPEN_READ);
    if (!f) return;

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, f);
    f.close();
    if (error) {
        LOGW("GNewsService", "Failed to parse SD cache: %s", error.c_str());
        return;
    }

    _snapshot.lastFetchTime = doc["last_fetch_time"] | 0;
    _lastFetchDay = doc["last_fetch_day"] | -1;
    _activeKeyIdx = doc["active_key_idx"] | 0;
    _catRoundRobinIdx = doc["cat_round_robin_idx"] | 0;
    _snapshot.status = doc["status"] | 0;

    JsonArray usagesArr = doc["key_usages"].as<JsonArray>();
    _keyUsages.clear();
    for (uint32_t u : usagesArr) {
        _keyUsages.push_back(u);
    }

    JsonArray artArr = doc["articles"].as<JsonArray>();
    _snapshot.count = 0;
    for (JsonObject obj : artArr) {
        if (_snapshot.count >= 10) break;
        const char* title = obj["title"] | "";
        const char* source = obj["source"] | "News";
        const char* category = obj["category"] | "News";
        uint32_t pubEpoch = obj["published_epoch"] | 0;

        if (strlen(title) == 0) continue;

        GNewsArticle& a = _snapshot.articles[_snapshot.count++];
        strncpy(a.title, title, sizeof(a.title) - 1);
        a.title[sizeof(a.title) - 1] = '\0';
        strncpy(a.source, source, sizeof(a.source) - 1);
        a.source[sizeof(a.source) - 1] = '\0';
        strncpy(a.category, category, sizeof(a.category) - 1);
        a.category[sizeof(a.category) - 1] = '\0';
        a.publishedEpoch = pubEpoch;
        a.badgeColor = getCategoryColor(category);
    }

    if (_snapshot.count > 0) {
        _snapshot.hasData = true;
        _snapshot.fetchSuccess = true;
        LOGI("GNewsService", "Restored %d articles from SD /gnews_cache.json", (int)_snapshot.count);
    }
}

String GNewsService::getQuotaStatusString() const {
    if (_apiKeys.empty()) return "No API keys configured";
    String res = "";
    int budget = _lastRequestsPerDay > 0 ? _lastRequestsPerDay : 10;
    for (size_t i = 0; i < _apiKeys.size(); i++) {
        if (i > 0) res += " | ";
        uint32_t used = (i < _keyUsages.size()) ? _keyUsages[i] : 0;
        String keySuffix = _apiKeys[i].length() > 4 ? _apiKeys[i].substring(_apiKeys[i].length() - 4) : "****";
        res += "Key " + String(i + 1) + " (.." + keySuffix + "): " + String(used) + "/" + String(budget) + " reqs";
        if (i == _activeKeyIdx) {
            res += " [Active]";
        }
    }
    return res;
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

    const char* defCat = (defaultCategory && strlen(defaultCategory) > 0) ? defaultCategory : "News";
    uint16_t catColor = getCategoryColor(defCat);

    std::vector<GNewsArticle> incoming;
    for (JsonObject obj : articles) {
        const char* rawTitle = obj["title"] | "";
        if (!rawTitle || strlen(rawTitle) == 0) continue;

        String cleanTitle = cleanNewsText(rawTitle);
        if (cleanTitle.length() == 0) continue;

        GNewsArticle art;
        strncpy(art.title, cleanTitle.c_str(), sizeof(art.title) - 1);
        art.title[sizeof(art.title) - 1] = '\0';

        const char* sourceName = obj["source"]["name"] | "News";
        String cleanSource = cleanNewsText(sourceName);
        strncpy(art.source, cleanSource.c_str(), sizeof(art.source) - 1);
        art.source[sizeof(art.source) - 1] = '\0';

        strncpy(art.category, defCat, sizeof(art.category) - 1);
        art.category[sizeof(art.category) - 1] = '\0';

        art.publishedEpoch = 0;
        art.badgeColor = catColor;
        incoming.push_back(art);
    }

    if (incoming.empty()) return false;

    // Merge incoming into _snapshot.articles with title deduplication
    std::vector<GNewsArticle> merged;
    for (const auto& inc : incoming) {
        merged.push_back(inc);
    }
    for (size_t i = 0; i < _snapshot.count; i++) {
        bool dup = false;
        for (const auto& m : merged) {
            if (strcmp(m.title, _snapshot.articles[i].title) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup && merged.size() < 10) {
            merged.push_back(_snapshot.articles[i]);
        }
    }

    _snapshot.count = min((size_t)10, merged.size());
    for (size_t i = 0; i < _snapshot.count; i++) {
        _snapshot.articles[i] = merged[i];
    }

    _snapshot.hasData = (_snapshot.count > 0);
    _snapshot.fetchSuccess = true;
    _snapshot.lastFetchTime = millis();
    return true;
}

void GNewsService::fetchNews(const String& apiKey, const String& category, const String& keywords,
                            const String& lang, const String& country, int maxArticles, int cacheTtlMin,
                            int requestsPerDay, bool forceRefresh) {
    if (!_loadedFromSd) {
        loadFromSd();
    }

    uint32_t now = millis();
    _lastRequestsPerDay = requestsPerDay > 0 ? requestsPerDay : 10;
    uint32_t intervalMs = (86400000UL) / (uint32_t)_lastRequestsPerDay;

    String reqLang = lang;
    if (reqLang.length() == 0 || reqLang == "auto" || reqLang == "system") {
        reqLang = String(I18n::getLangCode(I18n::getLang()));
        if (reqLang.length() == 0) reqLang = "fr";
    }

    // UTC Midnight rollover check (GNews daily quota resets precisely at 00:00 UTC / 12:00 AM UTC)
    time_t epochTime = 0;
    time(&epochTime);
    int curUtcDay = 0;
    if (epochTime > 1600000000) {
        curUtcDay = (int)(epochTime / 86400);
    } else {
        curUtcDay = (int)(now / 86400000UL); // Fallback before NTP sync
    }

    if (_lastFetchDay != -1 && curUtcDay != _lastFetchDay) {
        LOGI("GNewsService", "UTC Midnight reached (day %d -> %d). Resetting daily quota counters.", _lastFetchDay, curUtcDay);
        for (size_t i = 0; i < _keyUsages.size(); i++) {
            _keyUsages[i] = 0;
        }
        if (_snapshot.status == 3) { // RATE_LIMITED
            _snapshot.status = 0; // OK
        }
    }
    _lastFetchDay = curUtcDay;

    if (!forceRefresh && _snapshot.hasData && (now - _snapshot.lastFetchTime < intervalMs)) {
        return; // Scheduled interval not elapsed
    }

    // Parse comma-separated keys
    _apiKeys.clear();
    int kStart = 0;
    int kComma = apiKey.indexOf(',');
    while (kComma != -1) {
        String token = apiKey.substring(kStart, kComma);
        token.trim();
        if (token.length() > 0) _apiKeys.push_back(token);
        kStart = kComma + 1;
        kComma = apiKey.indexOf(',', kStart);
    }
    String lastKey = apiKey.substring(kStart);
    lastKey.trim();
    if (lastKey.length() > 0) _apiKeys.push_back(lastKey);

    while (_keyUsages.size() < _apiKeys.size()) {
        _keyUsages.push_back(0);
    }

    if (_apiKeys.empty()) {
        _snapshot.status = 1; // EMPTY_KEY
        LOGW("GNewsService", "No API key configured. Please configure api_key in settings to fetch live news.");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        _snapshot.status = 4; // NETWORK_ERROR
        return;
    }

    if (!_snapshot.hasData) {
        _snapshot.status = 5; // LOADING
    }

    // Split category list if comma-separated
    std::vector<String> cats;
    if (keywords.length() == 0 && category.length() > 0) {
        int start = 0;
        int comma = category.indexOf(',');
        while (comma != -1) {
            String token = category.substring(start, comma);
            token.trim();
            if (token.length() > 0) cats.push_back(token);
            start = comma + 1;
            comma = category.indexOf(',', start);
        }
        String lastToken = category.substring(start);
        lastToken.trim();
        if (lastToken.length() > 0) cats.push_back(lastToken);
    }
    if (cats.empty()) {
        cats.push_back(category.length() > 0 ? category : "general");
    }

    String targetCat = cats[_catRoundRobinIdx % cats.size()];
    _catRoundRobinIdx = (_catRoundRobinIdx + 1) % cats.size();

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);

    size_t startKeyIdx = _activeKeyIdx % _apiKeys.size();
    bool querySucceeded = false;

    for (size_t attempt = 0; attempt < _apiKeys.size(); attempt++) {
        size_t curKeyIdx = (startKeyIdx + attempt) % _apiKeys.size();
        String currentKey = _apiKeys[curKeyIdx];

        String url = "https://gnews.io/api/v4/";
        if (keywords.length() > 0) {
            url += "search?q=" + keywords;
        } else {
            url += "top-headlines?category=" + targetCat;
        }
        url += "&lang=" + reqLang;
        if (country.length() > 0 && country != "auto") {
            url += "&country=" + country;
        }
        int count = (maxArticles >= 1 && maxArticles <= 10) ? maxArticles : 5;
        url += "&max=" + String(count);
        url += "&apikey=" + currentKey;

        LOGI("GNewsService", "Fetching live news with key %d/%d for '%s'", (int)curKeyIdx + 1, (int)_apiKeys.size(), targetCat.c_str());

        if (http.begin(client, url)) {
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                if (parseGNewsJson(payload, targetCat.c_str())) {
                    _activeKeyIdx = curKeyIdx;
                    if (curKeyIdx < _keyUsages.size()) _keyUsages[curKeyIdx]++;
                    _snapshot.status = 0; // OK
                    saveToSd();
                    querySucceeded = true;
                    http.end();
                    break;
                }
            } else {
                String errBody = http.getString();
                errBody.toLowerCase();
                if (httpCode == 429 || (httpCode == 403 && (errBody.indexOf("consumed") >= 0 || errBody.indexOf("quota") >= 0 || errBody.indexOf("limit") >= 0 || errBody.indexOf("plan") >= 0))) {
                    LOGW("GNewsService", "Key %d/%d rate limited / daily quota reached (HTTP %d). Failing over...", (int)curKeyIdx + 1, (int)_apiKeys.size(), httpCode);
                    _snapshot.status = 3; // RATE_LIMITED
                } else if (httpCode == 401 || errBody.indexOf("invalid") >= 0 || errBody.indexOf("forbidden") >= 0) {
                    LOGW("GNewsService", "Key %d/%d invalid (HTTP %d). Failing over...", (int)curKeyIdx + 1, (int)_apiKeys.size(), httpCode);
                    _snapshot.status = 2; // INVALID_KEY
                } else {
                    LOGW("GNewsService", "HTTP GET failed with code: %d", httpCode);
                    _snapshot.status = 4; // NETWORK_ERROR
                }
            }
            http.end();
        } else {
            LOGE("GNewsService", "Unable to connect to GNews endpoint");
            _snapshot.status = 4; // NETWORK_ERROR
        }
    }

    if (!querySucceeded) {
        saveToSd(); // Persist error state without erasing existing cached articles
    }
}
