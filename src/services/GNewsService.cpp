#include "GNewsService.h"
#include "../core/Logger.h"
#include "../core/I18n.h"
#include "../core/NetworkBudget.h"
#include "../core/net/SecureHttpClient.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "../core/SDUtils.h"
#include "../core/SdLockGuard.h"
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

GNewsService gnewsService;

GNewsService::GNewsService() {
    for (int i = 0; i < 2; i++) {
        _snapshots[i].count = 0;
        _snapshots[i].hasData = false;
        _snapshots[i].fetchSuccess = false;
        _snapshots[i].lastFetchTime = 0;
        _snapshots[i].lastFetchEpoch = 0;
        _snapshots[i].status = 1; // EMPTY_KEY
    }
}

GNewsService::~GNewsService() {
    releaseArticleStorage();
}

bool GNewsService::ensureArticleStorage(GNewsSnapshot& snap) {
    if (snap.articles) return true;

    const size_t bytes = sizeof(GNewsArticle) * GNEWS_MAX_ARTICLES;
    // Prefer PSRAM: this block is only read by the render path, never from an ISR or DMA.
    void* block = nullptr;
    if (psramFound()) {
        block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    }
    if (!block) {
        block = malloc(bytes);
    }
    if (!block) {
        LOGE("GNewsService", "Failed to allocate %u bytes of article storage", (unsigned)bytes);
        return false;
    }

    memset(block, 0, bytes);
    snap.articles = static_cast<GNewsArticle*>(block);
    return true;
}

void GNewsService::setStatusLocked(uint8_t status) {
    _snapshots[0].status = status;
    _snapshots[1].status = status;
}

void GNewsService::releaseArticleStorage() {
    std::lock_guard<std::mutex> lock(_mutex);
    for (int i = 0; i < 2; i++) {
        if (_snapshots[i].articles) {
            heap_caps_free(_snapshots[i].articles);
            _snapshots[i].articles = nullptr;
        }
        _snapshots[i].count = 0;
        _snapshots[i].hasData = false;
    }
}

const GNewsSnapshot& GNewsService::getSnapshot() const {
    return _snapshots[_activeIdx.load(std::memory_order_acquire)];
}

bool GNewsService::hasData() const {
    const GNewsSnapshot& snap = getSnapshot();
    return snap.articles && snap.hasData && snap.count > 0;
}

void GNewsService::purgeArticles() {
    std::lock_guard<std::mutex> lock(_mutex);
    uint8_t curIdx = _activeIdx.load(std::memory_order_relaxed);
    uint8_t nextIdx = 1 - curIdx;
    GNewsSnapshot& next = _snapshots[nextIdx];
    next.count = 0;
    next.hasData = false;
    next.fetchSuccess = false;
    next.lastFetchTime = 0;
    next.lastFetchEpoch = 0;
    next.status = _snapshots[curIdx].status;
    _activeIdx.store(nextIdx, std::memory_order_release);

    GNewsSnapshot& prev = _snapshots[curIdx];
    prev.count = 0;
    prev.hasData = false;
    prev.fetchSuccess = false;
    prev.lastFetchTime = 0;
    prev.lastFetchEpoch = 0;

    _lastAttemptTime = 0;
    _lastAttemptEpoch = 0;
    _consecutiveFailures = 0;
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
    s.replace("&laquo;", "«");
    s.replace("&raquo;", "»");
    s.replace("&#171;", "«");
    s.replace("&#187;", "»");
    s.replace("&eacute;", "\xC3\xA9");
    s.replace("&egrave;", "\xC3\xA8");
    s.replace("&agrave;", "\xC3\xA0");
    s.replace("&ccedil;", "\xC3\xA7");
    s.replace("&ecirc;", "\xC3\xAA");
    s.replace("&euml;", "\xC3\xAB");
    s.replace("&ocirc;", "\xC3\xB4");
    s.replace("&icirc;", "\xC3\xAE");
    s.replace("&iuml;", "\xC3\xAF");
    s.replace("&ucirc;", "\xC3\xBB");
    s.replace("&ugrave;", "\xC3\xB9");
    s.replace("&Eacute;", "\xC3\x89");
    s.replace("&Egrave;", "\xC3\x88");
    s.replace("&Agrave;", "\xC3\x80");
    s.replace("&Ccedil;", "\xC3\x87");
    s.replace("&#233;", "\xC3\xA9");
    s.replace("&#232;", "\xC3\xA8");
    s.replace("&#224;", "\xC3\xA0");
    s.replace("&#231;", "\xC3\xA7");
    s.replace("&#234;", "\xC3\xAA");
    s.trim();
    return s;
}

void GNewsService::saveToSd() {
    SdLockGuard guard(pdMS_TO_TICKS(2000));
    if (!guard) return;
    if (!sd.exists("/")) return;

    FsFile f = sd.open("/gnews_cache.json", FILE_OPEN_WRITE);
    if (!f) return;

    const GNewsSnapshot& activeSnap = _snapshots[_activeIdx.load(std::memory_order_acquire)];
    DynamicJsonDocument doc(12288);
    doc["last_fetch_time"] = activeSnap.lastFetchTime;
    doc["last_fetch_epoch"] = activeSnap.lastFetchEpoch;
    doc["last_fetch_day"] = _lastFetchDay;
    doc["last_attempt_epoch"] = _lastAttemptEpoch;
    doc["consecutive_failures"] = _consecutiveFailures;
    doc["active_key_idx"] = _activeKeyIdx;
    doc["last_cat_idx"] = _catRoundRobinIdx;
    doc["cat_round_robin_idx"] = _catRoundRobinIdx;
    doc["status"] = activeSnap.status;

    JsonArray usagesArr = doc.createNestedArray("key_usages");
    for (uint32_t u : _keyUsages) {
        usagesArr.add(u);
    }

    JsonArray artArr = doc.createNestedArray("articles");
    for (size_t i = 0; i < activeSnap.count && activeSnap.articles; i++) {
        JsonObject obj = artArr.createNestedObject();
        obj["title"] = activeSnap.articles[i].title;
        obj["description"] = activeSnap.articles[i].description;
        obj["source"] = activeSnap.articles[i].source;
        obj["category"] = activeSnap.articles[i].category;
        obj["published_epoch"] = activeSnap.articles[i].publishedEpoch;
    }

    serializeJson(doc, f);
    f.close();
    guard.unlock();
    LOGI("GNewsService", "Persisted %d articles to SD /gnews_cache.json (epoch: %u)", (int)activeSnap.count, activeSnap.lastFetchEpoch);
}

void GNewsService::loadFromSd() {
    _loadedFromSd = true;
    SdLockGuard guard(pdMS_TO_TICKS(2000));
    if (!guard) return;
    if (!sd.exists("/gnews_cache.json")) return;

    FsFile f = sd.open("/gnews_cache.json", FILE_OPEN_READ);
    if (!f) return;

    DynamicJsonDocument doc(12288);
    DeserializationError error = deserializeJson(doc, f);
    f.close();
    guard.unlock();
    if (error) {
        LOGW("GNewsService", "Failed to parse SD cache: %s", error.c_str());
        return;
    }

    uint8_t curIdx = _activeIdx.load(std::memory_order_relaxed);
    uint8_t nextIdx = 1 - curIdx;
    GNewsSnapshot& next = _snapshots[nextIdx];

    next.lastFetchTime = millis();
    next.lastFetchEpoch = doc["last_fetch_epoch"] | doc["last_fetch_time"] | 0;
    _lastFetchDay = doc["last_fetch_day"] | -1;
    _lastAttemptEpoch = doc["last_attempt_epoch"] | 0;
    _consecutiveFailures = doc["consecutive_failures"] | 0;
    _activeKeyIdx = doc["active_key_idx"] | 0;
    _catRoundRobinIdx = doc["last_cat_idx"] | doc["cat_round_robin_idx"] | 0;
    next.status = doc["status"] | 0;

    JsonArray usagesArr = doc["key_usages"].as<JsonArray>();
    _keyUsages.clear();
    for (uint32_t u : usagesArr) {
        _keyUsages.push_back(u);
    }

    JsonArray artArr = doc["articles"].as<JsonArray>();
    next.count = 0;
    next.hasData = false;
    next.fetchSuccess = false;
    if (!artArr.isNull() && artArr.size() > 0 && !ensureArticleStorage(next)) {
        return;
    }
    for (JsonObject obj : artArr) {
        if (next.count >= GNEWS_MAX_ARTICLES) break;
        const char* title = obj["title"] | "";
        const char* desc = obj["description"] | "";
        const char* source = obj["source"] | "News";
        const char* category = obj["category"] | "News";
        uint32_t pubEpoch = obj["published_epoch"] | 0;

        if (strlen(title) == 0) continue;

        GNewsArticle& a = next.articles[next.count++];
        strncpy(a.title, title, sizeof(a.title) - 1);
        a.title[sizeof(a.title) - 1] = '\0';
        strncpy(a.description, desc, sizeof(a.description) - 1);
        a.description[sizeof(a.description) - 1] = '\0';
        strncpy(a.source, source, sizeof(a.source) - 1);
        a.source[sizeof(a.source) - 1] = '\0';
        strncpy(a.category, category, sizeof(a.category) - 1);
        a.category[sizeof(a.category) - 1] = '\0';
        a.publishedEpoch = pubEpoch;
        a.badgeColor = getCategoryColor(category);
    }

    if (next.count > 0) {
        next.hasData = true;
        next.fetchSuccess = true;
        LOGI("GNewsService", "Restored %d articles from SD /gnews_cache.json", (int)next.count);
    }
    _snapshots[curIdx].status = next.status;
    _activeIdx.store(nextIdx, std::memory_order_release);
}

String GNewsService::getQuotaStatusString() const {
    std::lock_guard<std::mutex> lock(_mutex);
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

        const char* rawDesc = obj["description"] | obj["content"] | "";
        String cleanDesc = cleanNewsText(rawDesc);
        int bracketPos = cleanDesc.lastIndexOf("[+");
        if (bracketPos > 0) {
            cleanDesc = cleanDesc.substring(0, bracketPos);
            cleanDesc.trim();
        }
        strncpy(art.description, cleanDesc.c_str(), sizeof(art.description) - 1);
        art.description[sizeof(art.description) - 1] = '\0';

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

    uint8_t curIdx = _activeIdx.load(std::memory_order_relaxed);
    const GNewsSnapshot& curSnap = _snapshots[curIdx];

    // Merge incoming into the snapshot articles with title deduplication
    std::vector<GNewsArticle> merged;
    for (const auto& inc : incoming) {
        merged.push_back(inc);
    }
    for (size_t i = 0; i < curSnap.count && curSnap.articles; i++) {
        bool dup = false;
        for (const auto& m : merged) {
            if (strcmp(m.title, curSnap.articles[i].title) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup && merged.size() < GNEWS_MAX_ARTICLES) {
            merged.push_back(curSnap.articles[i]);
        }
    }

    uint8_t nextIdx = 1 - curIdx;
    GNewsSnapshot& next = _snapshots[nextIdx];
    if (!ensureArticleStorage(next)) {
        next.count = 0;
        next.hasData = false;
        return false;
    }

    next.count = min((size_t)GNEWS_MAX_ARTICLES, merged.size());
    for (size_t i = 0; i < next.count; i++) {
        next.articles[i] = merged[i];
    }

    next.hasData = (next.count > 0);
    next.fetchSuccess = true;
    next.lastFetchTime = millis();
    time_t curEp = 0;
    time(&curEp);
    next.lastFetchEpoch = (curEp > 1600000000) ? (uint32_t)curEp : 0;
    next.status = 0;
    _activeIdx.store(nextIdx, std::memory_order_release);
    return true;
}

bool GNewsService::parseGNewsJson(Stream& stream, const char* defaultCategory) {
    return parseGNewsJson(stream.readString(), defaultCategory);
}

void GNewsService::fetchNews(const String& apiKey, const String& category, const String& keywords,
                            const String& lang, const String& country, int maxArticles, int cacheTtlMin,
                            int requestsPerDay, bool forceRefresh) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_loadedFromSd) {
        loadFromSd();
    }

    GNewsSnapshot& activeSnap = _snapshots[_activeIdx.load(std::memory_order_relaxed)];

    uint32_t now = millis();
    _lastRequestsPerDay = (requestsPerDay > 0) ? std::min(requestsPerDay, 100) : 10;
    uint32_t intervalFromBudgetSec = 86400UL / (uint32_t)_lastRequestsPerDay;
    uint32_t intervalFromTtlSec = (cacheTtlMin > 0) ? ((uint32_t)cacheTtlMin * 60UL) : 1800UL;
    uint32_t intervalSec = std::max(intervalFromBudgetSec, intervalFromTtlSec);
    uint32_t intervalMs = intervalSec * 1000UL;

    if (_lastApiKey != apiKey) {
        _lastApiKey = apiKey;
        _consecutiveFailures = 0;
        if (activeSnap.status == 1 || activeSnap.status == 2 || activeSnap.status == 3) {
            setStatusLocked(0);
        }
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
        setStatusLocked(1); // EMPTY_KEY
        LOGW("GNewsService", "No API key configured. Please configure api_key in settings to fetch live news.");
        return;
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
        _consecutiveFailures = 0;
        if (activeSnap.status == 3) { // RATE_LIMITED
            setStatusLocked(0); // OK
        }
        saveToSd();
    }
    _lastFetchDay = curUtcDay;

    // Check if daily quota is reached for all keys (strict hard cap: requests_per_day per key)
    bool hasAvailableKey = false;
    for (size_t i = 0; i < _apiKeys.size(); i++) {
        if (_keyUsages[i] < (uint32_t)_lastRequestsPerDay) {
            hasAvailableKey = true;
            break;
        }
    }
    if (!hasAvailableKey) {
        setStatusLocked(3); // RATE_LIMITED / QUOTA_EXHAUSTED
        return; // Absolute hard stop: do NOT perform any network calls until midnight UTC!
    }

    // If currently rate limited (429 received from server), stay locked out until midnight UTC unless forced
    if (activeSnap.status == 3 && !forceRefresh) {
        return;
    }

    // Failure backoff guard (anti-hammering):
    // If a previous attempt failed (network error, timeout, 429), wait at least 5 minutes before retrying.
    // This applies REGARDLESS of whether activeSnap.hasData is true or false.
    uint32_t failureBackoffSec = 300; // 5 minutes minimum
    if (_consecutiveFailures > 1) {
        failureBackoffSec = std::min((uint32_t)300 * _consecutiveFailures, (uint32_t)1800); // Up to 30 min
    }
    uint32_t failureBackoffMs = failureBackoffSec * 1000UL;

    if (!forceRefresh && _lastAttemptTime != 0) {
        bool inFailureBackoff = false;
        if (epochTime > 1600000000 && _lastAttemptEpoch > 1600000000) {
            if ((uint32_t)epochTime >= _lastAttemptEpoch) {
                inFailureBackoff = (((uint32_t)epochTime - _lastAttemptEpoch) < failureBackoffSec);
            }
        } else {
            inFailureBackoff = ((now - _lastAttemptTime) < failureBackoffMs);
        }
        if (inFailureBackoff && (_consecutiveFailures > 0 || !activeSnap.hasData)) {
            return; // Waiting for failure backoff window to expire
        }
    }

    // Normal scheduled interval check (based on requests_per_day AND cache_ttl_min)
    bool intervalElapsed = false;
    if (epochTime > 1600000000 && activeSnap.lastFetchEpoch > 1600000000) {
        if ((uint32_t)epochTime >= activeSnap.lastFetchEpoch) {
            intervalElapsed = (((uint32_t)epochTime - activeSnap.lastFetchEpoch) >= intervalSec);
        } else {
            intervalElapsed = true; // Clock jumped backwards
        }
    } else {
        intervalElapsed = (now - activeSnap.lastFetchTime >= intervalMs);
    }

    if (!forceRefresh && activeSnap.hasData && activeSnap.articles != nullptr && !intervalElapsed) {
        return; // Scheduled interval not elapsed
    }

    if (WiFi.status() != WL_CONNECTED) {
        setStatusLocked(4); // NETWORK_ERROR
        return;
    }

    if (!activeSnap.hasData) {
        setStatusLocked(5); // LOADING
    }

    // Record attempt timestamp immediately to enforce backoff if network or parsing fails
    _lastAttemptTime = now;
    _lastAttemptEpoch = (epochTime > 1600000000) ? (uint32_t)epochTime : 0;

    String reqLang = lang;
    if (reqLang.length() == 0 || reqLang == "auto" || reqLang == "system") {
        reqLang = String(I18n::getLangCode(I18n::getLang()));
        if (reqLang.length() == 0) reqLang = "fr";
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

    net::SecureHttpOptions options;
    options.requestTimeoutMs = 4500;
    options.handshakeTimeoutSec = 4;
    options.ownerId = net::OWNER_GNEWS;
    auto session = net::SecureHttpClient::session("gnews.io", 443, options);

    size_t startKeyIdx = _activeKeyIdx % _apiKeys.size();
    bool querySucceeded = false;

    for (size_t attempt = 0; attempt < _apiKeys.size(); attempt++) {
        size_t curKeyIdx = (startKeyIdx + attempt) % _apiKeys.size();
        if (_keyUsages[curKeyIdx] >= (uint32_t)_lastRequestsPerDay) {
            LOGI("GNewsService", "Skipping key %d/%d (quota exhausted: %u/%d)",
                 (int)curKeyIdx + 1, (int)_apiKeys.size(), _keyUsages[curKeyIdx], _lastRequestsPerDay);
            continue;
        }

        String currentKey = _apiKeys[curKeyIdx];

        String path = "/api/v4/";
        if (keywords.length() > 0) {
            path += "search?q=" + keywords;
        } else {
            path += "top-headlines?category=" + targetCat;
        }
        path += "&lang=" + reqLang;
        if (country.length() > 0 && country != "auto") {
            path += "&country=" + country;
        }
        int count = (maxArticles >= 1 && maxArticles <= 10) ? maxArticles : 5;
        path += "&max=" + String(count);
        path += "&apikey=" + currentKey;

        LOGI("GNewsService", "Fetching live news with key %d/%d (usage %u/%d) for '%s'",
             (int)curKeyIdx + 1, (int)_apiKeys.size(), _keyUsages[curKeyIdx] + 1, _lastRequestsPerDay, targetCat.c_str());

        // Increment key usage because the HTTP request is dispatched and counted by GNews API
        _keyUsages[curKeyIdx]++;
        saveToSd();

        auto response = session.get(path);
        int httpCode = response.statusCode();

        if (response.ok()) {
            String payload = response.body();
            if (parseGNewsJson(payload, targetCat.c_str())) {
                _activeKeyIdx = curKeyIdx;
                setStatusLocked(0); // OK
                GNewsSnapshot& pubSnap = _snapshots[_activeIdx.load(std::memory_order_relaxed)];
                pubSnap.lastFetchTime = millis();
                pubSnap.lastFetchEpoch = (epochTime > 1600000000) ? (uint32_t)epochTime : 0;
                _consecutiveFailures = 0;
                saveToSd();
                querySucceeded = true;
                break;
            } else {
                _consecutiveFailures++;
            }
        } else {
            _consecutiveFailures++;
            String errBody = response.body();
            errBody.toLowerCase();
            if (httpCode == 429 || (httpCode == 403 && (errBody.indexOf("consumed") >= 0 || errBody.indexOf("quota") >= 0 || errBody.indexOf("limit") >= 0 || errBody.indexOf("plan") >= 0))) {
                LOGW("GNewsService", "Key %d/%d rate limited / daily quota reached (HTTP %d). Locking out until midnight UTC.", (int)curKeyIdx + 1, (int)_apiKeys.size(), httpCode);
                _keyUsages[curKeyIdx] = _lastRequestsPerDay; // Mark key as fully consumed
                setStatusLocked(3); // RATE_LIMITED
                saveToSd();
            } else if (httpCode == 401 || errBody.indexOf("invalid") >= 0 || errBody.indexOf("forbidden") >= 0) {
                LOGW("GNewsService", "Key %d/%d invalid (HTTP %d). Failing over...", (int)curKeyIdx + 1, (int)_apiKeys.size(), httpCode);
                setStatusLocked(2); // INVALID_KEY
            } else {
                LOGW("GNewsService", "HTTP GET failed with code: %d (error: %s)", httpCode, response.errorMessage());
                setStatusLocked(4); // NETWORK_ERROR
            }
        }
    }
    esp_task_wdt_reset();

    if (!querySucceeded) {
        if (_consecutiveFailures == 0) _consecutiveFailures = 1;
        saveToSd(); // Persist error state without erasing existing cached articles
    }
}
