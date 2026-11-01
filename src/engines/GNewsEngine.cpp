#include "GNewsEngine.h"
#include "GNewsTickerUtils.h"
#include "../core/drawing/IDrawingSurface.h"
#include "../core/Logger.h"
#include "../core/I18n.h"
#include "../core/net/SecureHttpClient.h"
#include <cmath>
#include <cstdlib>

namespace {
uint32_t gnewsRandomU32() {
#if defined(ESP32)
    return esp_random();
#else
    return static_cast<uint32_t>(rand());
#endif
}

void drawClippedSingleLineText(IDrawingSurface* matrix, const char* text,
                               int startX, int y, int clipMinX, int clipMaxX,
                               uint16_t color, uint8_t textSize) {
    if (!matrix || !text || *text == '\0' || clipMaxX <= clipMinX) return;
    const int scale = (textSize > 1U) ? static_cast<int>(textSize) : 1;
    const int charW = 6 * scale;
    int firstIdx = 0;
    if (startX < (clipMinX - charW)) {
        firstIdx = (clipMinX - charW - startX) / charW;
        if (firstIdx < 0) firstIdx = 0;
    }
    const int len = static_cast<int>(strlen(text));
    for (int i = firstIdx; i < len; ++i) {
        const int cx = startX + i * charW;
        if (cx >= clipMaxX) break;
        if (cx + charW <= clipMinX) continue;
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= ' ') continue;
        matrix->drawChar(static_cast<int16_t>(cx), static_cast<int16_t>(y), c, color, color, textSize);
    }
}
} // namespace

GNewsEngine::GNewsEngine() {
    _geometry.width = 64;
    _geometry.height = 32;
    _geometry.layoutClass = LayoutClass::WIDE;
}

GNewsEngine::~GNewsEngine() {
    if (m_fetchTask && !m_fetchExited.load(std::memory_order_acquire)) {
        m_stopFetch.store(true, std::memory_order_release);
        net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_GNEWS);
        while (!m_fetchExited.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        m_fetchTask = nullptr;
    }
}

void GNewsEngine::applyConfig(const EngineConfig* config) {
    if (!config) return;
    config_api_key = config->getString("api_key", "");
    config_category = config->getString("category", "technology");
    config_keywords = config->getString("keywords", "");
    config_lang = config->getString("lang", "auto");
    config_country = config->getString("country", "auto");
    config_max_articles = std::max(3, std::min(15, config->getInt("max_articles", 5)));
    config_articles_per_rotation = std::max(1, std::min(15, config->getInt("articles_per_rotation", 2)));
    config_cache_ttl_min = config->getInt("cache_ttl_min", 30);
    config_requests_per_day = config->getInt("requests_per_day", 10);
    config_force_refresh = config->getBool("force_refresh", false);

    String prevMode = config_display_mode;
    bool prevTitle = config_show_title;
    bool prevDesc = config_show_description;
    bool prevContent = config_show_content;

    config_display_mode = config->getString("display_mode", "smooth_scroll");
    config_show_title = config->getBool("show_title", false);
    config_show_description = config->getBool("show_description", false);
    config_show_content = config->getBool("show_content", true);

    const bool contentTogglesChanged =
        (config_show_title != prevTitle ||
         config_show_description != prevDesc ||
         config_show_content != prevContent);

    if (config_display_mode != prevMode || contentTogglesChanged) {
        uint32_t now = millis();
        scrollPixelOffset = 0;
        m_scrollRemUs = 0;
        scrollState = ScrollState::PauseStart;
        stateStartTime = now;
        lastScrollTick = micros();
        lastArticleSwitchTime = now;
        currentPageIndex = 0;
        totalPages = 1;
        lastPageSwitchTime = now;
        cachedArticleIndex = -1;
        if (contentTogglesChanged) {
            m_needsArticleReload = true;
        }
    }

    config_scroll_speed = std::max(1, std::min(10, config->getInt("scroll_speed", 3)));
    config_scroll_pause_start_ms = std::max(0, std::min(4000, config->getInt("scroll_pause_start_ms", 1200)));
    config_scroll_pause_end_ms = std::max(0, std::min(4000, config->getInt("scroll_pause_end_ms", 1000)));
    config_article_duration_sec = std::max(5, std::min(60, config->getInt("article_duration_sec", 12)));
    config_theme = config->getString("theme", "category_dynamic");
    config_show_category_badge = config->getBool("show_category_badge", true);
    config_show_source = config->getBool("show_source", true);
    config_show_time_ago = config->getBool("show_time_ago", true);
    config_show_beacon = config->getBool("show_beacon", true);
    config_show_progress_dots = config->getBool("show_progress_dots", true);
}

EngineError GNewsEngine::initialize(EngineContext* context, const EngineConfig* config) {
    if (context) {
        _geometry = context->getGeometry();
        if (context->getSurface()) {
            lastMatrixW = context->getSurface()->width();
            lastMatrixH = context->getSurface()->height();
        }
    }
    if (config) applyConfig(config);
    return EngineError::OK;
}

const char* GNewsEngine::getCategoryShort(const char* category) {
    return I18n::getGNewsCategoryShort(category);
}

void GNewsEngine::pickNextRandomArticle(size_t count) {
    if (count > GNEWS_MAX_ARTICLES) {
        count = GNEWS_MAX_ARTICLES;
    }
    if (count == 0) {
        currentArticleIndex = 0;
        m_shuffleCount = 0;
        m_shufflePos = 0;
        return;
    }
    if (count == 1) {
        currentArticleIndex = 0;
        m_shuffleCount = 1;
        m_shufflePos = 1;
        return;
    }
    if (m_shuffleCount != count || m_shufflePos >= m_shuffleCount) {
        size_t prevIdx = currentArticleIndex;
        for (size_t i = 0; i < count; ++i) {
            m_shuffledOrder[i] = static_cast<uint8_t>(i);
        }
        for (size_t i = count - 1; i > 0; --i) {
            size_t j = gnewsRandomU32() % (i + 1);
            uint8_t tmp = m_shuffledOrder[i];
            m_shuffledOrder[i] = m_shuffledOrder[j];
            m_shuffledOrder[j] = tmp;
        }
        if (m_shuffledOrder[0] == prevIdx && count > 1) {
            uint8_t tmp = m_shuffledOrder[0];
            m_shuffledOrder[0] = m_shuffledOrder[count - 1];
            m_shuffledOrder[count - 1] = tmp;
        }
        m_shuffleCount = count;
        m_shufflePos = 0;
    }
    currentArticleIndex = m_shuffledOrder[m_shufflePos++];
}

void GNewsEngine::rebuildLayoutCache(int mW, int mH, uint32_t now) {
    if (mW <= 0) mW = (_geometry.width > 0) ? _geometry.width : 64;
    if (mH <= 0) mH = (_geometry.height > 0) ? _geometry.height : 32;

    const bool isTate = (mH > mW);
    int bodyY = 13;
    int lineSpacing = 9;
    int numRows = 2;
    int linesPerPage = 2;

    if (isTate) {
        bodyY = 24;
        lineSpacing = 9;
        numRows = max(2, (mH - bodyY) / lineSpacing);
        linesPerPage = numRows;
    } else if (mH >= 64) {
        bodyY = 19;
        lineSpacing = 10;
        numRows = 4;
        linesPerPage = 4;
    }

    if (config_display_mode == "static_paged") {
        const int maxW = isTate ? (mW - 4) : (mW - 8);
        wrapTextToLines(formattedHeadline, maxW);
        cachedMaxScroll = 0;
        totalPages = (cachedLineCount == 0) ? 1 : ((cachedLineCount + static_cast<size_t>(linesPerPage) - 1) / static_cast<size_t>(linesPerPage));
        if (totalPages < 1) totalPages = 1;
        currentPageIndex = 0;
        lastPageSwitchTime = now;
    } else if (config_display_mode == "vertical_crawl") {
        const int maxW = isTate ? (mW - 4) : (mW - 8);
        wrapTextToLines(formattedHeadline, maxW);
        const int totalH = static_cast<int>(cachedLineCount) * lineSpacing;
        cachedMaxScroll = (cachedLineCount > 0) ? (totalH + 4) : 0;
    } else if (config_display_mode == "serpentine") {
        const int charPx = (!isTate && mH >= 64 && mW >= 256) ? 12 : 6;
        const int tLen = static_cast<int>(strlen(formattedHeadline));
        cachedMaxScroll = (tLen > 0) ? max(charPx, tLen * charPx + 8) : 0;
        cachedLineCount = 0;
    } else {
        // "smooth_scroll"
        if (isTate) {
            distributeTextToRows(formattedHeadline, numRows);
            int maxLineW = 0;
            for (size_t i = 0; i < cachedLineCount; i++) {
                int w = static_cast<int>(strlen(cachedDisplayLines[i])) * 6;
                if (w > maxLineW) maxLineW = w;
            }
            cachedMaxScroll = (maxLineW > 0) ? (mW + maxLineW + 12) : 0;
        } else {
            const int charPx = (mH >= 64 && mW >= 256) ? 12 : 6;
            const int textW = static_cast<int>(strlen(formattedHeadline)) * charPx;
            cachedMaxScroll = (textW > 0) ? (textW + 16) : 0;
            cachedLineCount = 0;
        }
    }

    cachedArticleIndex = static_cast<int>(currentArticleIndex);
}

bool GNewsEngine::latchArticleAtIndex(size_t index, int mW, int mH, uint32_t now) {
    GNewsArticle art{};
    size_t count = 0;
    bool hasData = false;
    uint8_t status = 1;
    uint32_t seq = 0;

    if (!gnewsService.tryReadArticleLockFree(index, art, count, hasData, status, seq)) {
        return false;
    }
    m_cachedStatus = status;
    m_lastSeenSeq = seq;
    if (!hasData || count == 0) {
        m_hasActiveArticle = false;
        m_activeTotalCount = 0;
        return false;
    }

    if (index >= count) {
        index = 0;
    }
    currentArticleIndex = index;
    strncpy(m_activeCategory, art.category[0] != '\0' ? art.category : "News", sizeof(m_activeCategory) - 1);
    m_activeCategory[sizeof(m_activeCategory) - 1] = '\0';
    m_activeBadgeColor = (art.badgeColor != 0) ? art.badgeColor : 0xDEFB;
    m_activeTotalCount = count;
    m_hasActiveArticle = true;
    m_needsArticleReload = false;

    prepareHeadlineText(art);
    rebuildLayoutCache(mW, mH, now);
    return true;
}

bool GNewsEngine::latchNextRandomArticle(int mW, int mH, uint32_t now) {
    size_t count = 0;
    bool hasData = false;
    uint8_t status = 1;
    uint32_t seq = 0;

    if (!gnewsService.tryReadMetaLockFree(count, hasData, status, seq)) {
        return false;
    }
    m_cachedStatus = status;
    if (!hasData || count == 0) {
        m_lastSeenSeq = seq;
        m_hasActiveArticle = false;
        m_activeTotalCount = 0;
        return false;
    }

    if (seq != m_lastSeenSeq) {
        m_shuffleCount = 0;
        m_shufflePos = 0;
    }
    pickNextRandomArticle(count);
    return latchArticleAtIndex(currentArticleIndex, mW, mH, now);
}

void GNewsEngine::advanceToNextArticle(int mW, int mH, uint32_t now) {
    m_articlesShownThisCycle++;
    const size_t target = static_cast<size_t>(std::max(1, config_articles_per_rotation));
    if (m_articlesShownThisCycle >= target) {
        // Cycle complete: preserve the final completed frame of the last article so render()
        // on this tick and RotationManager's outgoing transition never flash a phantom extra article.
        return;
    }

    if (!latchNextRandomArticle(mW, mH, now)) {
        m_needsInitialPick = true;
        cachedArticleIndex = -1;
    }
    currentPageIndex = 0;
    lastPageSwitchTime = now;
    scrollPixelOffset = 0;
    m_scrollRemUs = 0;
    scrollState = ScrollState::PauseStart;
    stateStartTime = now;
    lastScrollTick = micros();
    lastArticleSwitchTime = now;
}

void GNewsEngine::wrapTextToLines(const char* text, int maxW) {
    cachedLineCount = 0;
    if (!text || *text == '\0') return;

    int maxChars = maxW / 6;
    if (maxChars < 4) maxChars = 4;
    if (maxChars >= (int)MAX_ROW_CHARS) maxChars = (int)MAX_ROW_CHARS - 1;

    const char* ptr = text;
    while (*ptr != '\0' && cachedLineCount < MAX_DISPLAY_ROWS) {
        while (*ptr == ' ') ptr++;
        if (*ptr == '\0') break;

        const char* end = ptr;
        const char* lastSpace = nullptr;
        int count = 0;

        while (*end != '\0' && count < maxChars) {
            if (*end == ' ') lastSpace = end;
            end++;
            count++;
        }

        int lineLen = 0;
        if (*end == '\0') {
            lineLen = end - ptr;
        } else if (lastSpace != nullptr && lastSpace > ptr) {
            lineLen = lastSpace - ptr;
            end = lastSpace + 1;
        } else {
            lineLen = maxChars;
        }

        if (lineLen >= (int)MAX_ROW_CHARS) lineLen = (int)MAX_ROW_CHARS - 1;
        strncpy(cachedDisplayLines[cachedLineCount], ptr, lineLen);
        cachedDisplayLines[cachedLineCount][lineLen] = '\0';
        cachedLineCount++;
        ptr = end;
    }
}

void GNewsEngine::distributeTextToRows(const char* text, int numRows) {
    cachedLineCount = 0;
    if (!text || *text == '\0') return;
    if (numRows < 1) numRows = 1;
    if (numRows > (int)MAX_DISPLAY_ROWS) numRows = (int)MAX_DISPLAY_ROWS;

    int totalLen = strlen(text);
    int targetPerLine = (totalLen / numRows);
    if (targetPerLine < 4) targetPerLine = 4;
    if (targetPerLine >= (int)MAX_ROW_CHARS) targetPerLine = (int)MAX_ROW_CHARS - 1;

    const char* ptr = text;
    while (*ptr != '\0' && (int)cachedLineCount < numRows) {
        while (*ptr == ' ') ptr++;
        if (*ptr == '\0') break;

        if ((int)cachedLineCount == numRows - 1) {
            int lineLen = strlen(ptr);
            if (lineLen >= (int)MAX_ROW_CHARS) lineLen = (int)MAX_ROW_CHARS - 1;
            strncpy(cachedDisplayLines[cachedLineCount], ptr, lineLen);
            cachedDisplayLines[cachedLineCount][lineLen] = '\0';
            cachedLineCount++;
            break;
        }

        const char* end = ptr;
        const char* lastSpace = nullptr;
        int count = 0;

        while (*end != '\0' && count < targetPerLine) {
            if (*end == ' ') lastSpace = end;
            end++;
            count++;
        }

        int lineLen = 0;
        if (*end == '\0') {
            lineLen = end - ptr;
        } else if (lastSpace != nullptr && lastSpace > ptr) {
            lineLen = lastSpace - ptr;
            end = lastSpace + 1;
        } else {
            lineLen = count;
        }

        if (lineLen >= (int)MAX_ROW_CHARS) lineLen = (int)MAX_ROW_CHARS - 1;
        strncpy(cachedDisplayLines[cachedLineCount], ptr, lineLen);
        cachedDisplayLines[cachedLineCount][lineLen] = '\0';
        cachedLineCount++;
        ptr = end;
    }
}

void GNewsEngine::formatGNewsText(const char* utf8Input, char* output, size_t maxLen) {
    GNewsTickerUtils::formatSingleLineCp437(utf8Input, output, maxLen);
}

void GNewsEngine::prepareHeadlineText(const GNewsArticle& article) {
    formattedHeadline[0] = '\0';

    auto appendSegment = [this](const char* segment) {
        if (!segment || segment[0] == '\0') return;
        const size_t beforeLen = strlen(formattedHeadline);
        size_t curLen = beforeLen;
        if (curLen > 0) {
            if (curLen + 4 >= sizeof(formattedHeadline)) return;
            strcat(formattedHeadline, " - ");
            curLen += 3;
        }
        formatGNewsText(segment, formattedHeadline + curLen, sizeof(formattedHeadline) - curLen);
        if (formattedHeadline[curLen] == '\0') {
            formattedHeadline[beforeLen] = '\0';
        }
    };

    const char* lastAdded = nullptr;
    auto startsWithCi = [](const char* haystack, const char* needle) -> bool {
        if (!haystack || !needle || *needle == '\0') return false;
        while (static_cast<uint8_t>(*needle) <= 0x20U && *needle != '\0') needle++;
        while (static_cast<uint8_t>(*haystack) <= 0x20U && *haystack != '\0') haystack++;
        if (*needle == '\0') return false;
        while (*needle != '\0') {
            if (*haystack == '\0') return false;
            const bool ws1 = (static_cast<uint8_t>(*haystack) <= 0x20U);
            const bool ws2 = (static_cast<uint8_t>(*needle) <= 0x20U);
            if (ws1 || ws2) {
                if (ws1 != ws2) return false;
                while (static_cast<uint8_t>(*haystack) <= 0x20U && *haystack != '\0') haystack++;
                while (static_cast<uint8_t>(*needle) <= 0x20U && *needle != '\0') needle++;
                continue;
            }
            const char c1 = (*haystack >= 'A' && *haystack <= 'Z') ? (*haystack + 32) : *haystack;
            const char c2 = (*needle >= 'A' && *needle <= 'Z') ? (*needle + 32) : *needle;
            if (c1 != c2) return false;
            haystack++;
            needle++;
        }
        return true;
    };

    if (config_show_title && article.title[0] != '\0') {
        appendSegment(article.title);
        lastAdded = article.title;
    }
    if (config_show_description && article.description[0] != '\0') {
        if (!lastAdded || !startsWithCi(article.description, lastAdded)) {
            appendSegment(article.description);
            lastAdded = article.description;
        }
    }
    if (config_show_content && article.content[0] != '\0') {
        if (!lastAdded || !startsWithCi(article.content, lastAdded)) {
            appendSegment(article.content);
            lastAdded = article.content;
        }
    }

    if (formattedHeadline[0] == '\0') {
        if (article.content[0] != '\0') {
            appendSegment(article.content);
        } else if (article.description[0] != '\0') {
            appendSegment(article.description);
        } else if (article.title[0] != '\0') {
            appendSegment(article.title);
        }
    }

    formatGNewsText(article.source, formattedSource, sizeof(formattedSource));
}

void GNewsEngine::renderSerpentine(EngineContext* context, const char* title, int bodyY, int clipMinX, int clipMaxX, int clipMinY, int clipMaxY, int lineSpacing, int numRows) {
    (void)bodyY;
    (void)lineSpacing;
    (void)numRows;
    if (!context || !context->getSurface() || !title || *title == '\0') return;
    auto* matrix = context->getSurface();
    const int mW = matrix->width();
    const int mH = matrix->height();
    const bool isTate = (mH > mW);

    const int tLen = static_cast<int>(strlen(title));
    if (tLen <= 0) return;

    const uint8_t textSize = (!isTate && mW >= 256 && mH >= 64) ? 2U : 1U;
    const int charW = 6 * static_cast<int>(textSize);
    const int glyphH = 8 * static_cast<int>(textSize);

    const int minTopY = clipMinY + 1;
    int maxTopY = clipMaxY - glyphH - 1;
    if (maxTopY < minTopY) {
        maxTopY = minTopY;
    }

    const float centerY = 0.5f * static_cast<float>(minTopY + maxTopY);
    const float amp = 0.5f * static_cast<float>(maxTopY - minTopY);
    const float wavelength = (textSize == 2U)
        ? 128.0f
        : ((mW >= 128) ? 72.0f : 56.0f);
    const float freq = 6.2831853f / wavelength;

    const int startX = (clipMinX + 2) - scrollPixelOffset;
    int firstIdx = 0;
    if (startX < (clipMinX - charW)) {
        firstIdx = (clipMinX - charW - startX) / charW;
        if (firstIdx < 0) firstIdx = 0;
    }

    for (int i = firstIdx; i < tLen; ++i) {
        const int cx = startX + i * charW;
        if (cx >= clipMaxX) break;
        if (cx + charW <= clipMinX) continue;

        const unsigned char c = static_cast<unsigned char>(title[i]);
        if (c == ' ') continue;

        const float phase = static_cast<float>(cx - clipMinX) * freq;
        const int cy = static_cast<int>(centerY + amp * sinf(phase) + 0.5f);
        if (cy + glyphH > clipMinY && cy < clipMaxY) {
            matrix->drawChar(cx, cy, c, 0xFFFF, 0xFFFF, textSize);
        }
    }

    const int clipH = clipMaxY - clipMinY;
    if (clipH > 0) {
        if (clipMinX > 0) {
            matrix->fillRect(0, clipMinY, clipMinX, clipH, 0x0000);
        }
        if (clipMaxX < mW) {
            matrix->fillRect(clipMaxX, clipMinY, mW - clipMaxX, clipH, 0x0000);
        }
    }
}

void GNewsEngine::startFetchTask() {
    if (m_fetchTask) {
        m_stopFetch.store(false, std::memory_order_release);
        return;
    }
    m_stopFetch.store(false, std::memory_order_release);
    m_fetchExited.store(false, std::memory_order_release);
    if (xTaskCreatePinnedToCore(fetchTaskEntry, "gnews_fetch", 8192, this, 1, &m_fetchTask, 0) != pdPASS) {
        m_fetchTask = nullptr;
        m_fetchExited.store(true, std::memory_order_release);
        LOGW("GNewsEngine", "Failed to start gnews_fetch task on Core 0");
    }
}

void GNewsEngine::stopFetchTask() {
    if (m_fetchTask) {
        m_stopFetch.store(true, std::memory_order_release);
        net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_GNEWS);
        for (int i = 0; i < 30 && !m_fetchExited.load(std::memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (m_fetchExited.load(std::memory_order_acquire)) {
            m_fetchTask = nullptr;
        } else {
            LOGW("GNewsEngine", "gnews_fetch task did not exit within 300ms cooperative window");
        }
    }
}

void GNewsEngine::fetchTaskEntry(void* arg) {
    auto* self = static_cast<GNewsEngine*>(arg);
    self->fetchWorker();
    self->m_fetchTask = nullptr;
    self->m_fetchExited.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}

void GNewsEngine::fetchWorker() {
    while (!m_stopFetch.load(std::memory_order_acquire)) {
        if (m_purgeRequested.exchange(false, std::memory_order_acq_rel)) {
            gnewsService.purgeArticles();
        }
        bool force = config_force_refresh || m_fetchRequested.exchange(false);
        gnewsService.fetchNews(config_api_key, config_category, config_keywords,
                               config_lang, config_country, config_max_articles, config_cache_ttl_min,
                               config_requests_per_day, force);

        // Sleep in 100ms slices up to 10 seconds (or until stopped/requested)
        for (int i = 0; i < 100 && !m_stopFetch.load(std::memory_order_acquire); i++) {
            if (m_fetchRequested.load(std::memory_order_acquire)) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void GNewsEngine::activate() {
    const uint32_t now = millis();
    m_articlesShownThisCycle = 0;
    m_activateTimeMs = now;
    currentPageIndex = 0;
    totalPages = 1;
    lastPageSwitchTime = now;
    scrollPixelOffset = 0;
    m_scrollRemUs = 0;
    sourceMarqueeOffset = 0;
    cachedArticleIndex = -1;
    cachedLineCount = 0;
    cachedMaxScroll = 0;
    formattedHeadline[0] = '\0';
    formattedSource[0] = '\0';
    m_hasActiveArticle = false;
    m_needsArticleReload = false;
    scrollState = ScrollState::PauseStart;
    stateStartTime = now;
    lastScrollTick = micros();
    lastSourceTick = now;
    lastArticleSwitchTime = now;

    const int initW = (lastMatrixW > 0) ? lastMatrixW : ((_geometry.width > 0) ? _geometry.width : 64);
    const int initH = (lastMatrixH > 0) ? lastMatrixH : ((_geometry.height > 0) ? _geometry.height : 32);
    if (latchNextRandomArticle(initW, initH, now)) {
        m_needsInitialPick = false;
    } else {
        currentArticleIndex = 0;
        m_needsInitialPick = true;
    }
    startFetchTask();
}

void GNewsEngine::onConfigChanged(const EngineConfig* config) {
    bool prevForce = config_force_refresh;
    applyConfig(config);
    cachedArticleIndex = -1;
    if (config_force_refresh && !prevForce) {
        m_shuffleCount = 0;
        m_shufflePos = 0;
        m_hasActiveArticle = false;
        m_needsInitialPick = true;
        m_purgeRequested.store(true, std::memory_order_release);
    }
    m_fetchRequested.store(true, std::memory_order_release);
}

void GNewsEngine::deactivate() {
    stopFetchTask();
}

bool GNewsEngine::shutdownForDestruction() {
    if (!m_fetchTask && m_fetchExited.load(std::memory_order_acquire)) {
        return true;
    }
    stopFetchTask();
    return m_fetchExited.load(std::memory_order_acquire);
}

bool GNewsEngine::isFinished() const {
    if (!m_hasActiveArticle) {
        size_t count = 0;
        bool hasData = false;
        uint8_t status = 1;
        uint32_t seq = 0;
        if (gnewsService.tryReadMetaLockFree(count, hasData, status, seq) && hasData && count > 0) {
            return false;
        }
        uint32_t fallbackMs = static_cast<uint32_t>(std::max(5, config_article_duration_sec)) * 1000UL;
        return (millis() - m_activateTimeMs) >= fallbackMs;
    }
    size_t target = static_cast<size_t>(std::max(1, config_articles_per_rotation));
    return m_articlesShownThisCycle >= target;
}

void GNewsEngine::update(EngineContext* context) {
    startFetchTask();
    uint32_t now = millis();

    // Smooth sinusoidal pulsing beacon (0.0 to 1.0)
    beaconPulse = (sinf((float)sourceMarqueeOffset * 0.1f) + 1.0f) * 0.5f;

    // Smooth discrete source marquee advancement (every 35ms -> 1 pixel)
    if (now - lastSourceTick >= 35) {
        uint32_t steps = (now - lastSourceTick) / 35;
        sourceMarqueeOffset += steps;
        lastSourceTick += steps * 35;
    }

    auto* matrix = context ? context->getSurface() : nullptr;
    int mW = matrix ? matrix->width() : (_geometry.width > 0 ? _geometry.width : 64);
    int mH = matrix ? matrix->height() : (_geometry.height > 0 ? _geometry.height : 32);

    if (mW != lastMatrixW || mH != lastMatrixH) {
        lastMatrixW = mW;
        lastMatrixH = mH;
        cachedArticleIndex = -1;
        currentPageIndex = 0;
    }

    size_t metaCount = 0;
    bool metaHasData = false;
    uint8_t metaStatus = m_cachedStatus;
    uint32_t metaSeq = m_lastSeenSeq;
    if (gnewsService.tryReadMetaLockFree(metaCount, metaHasData, metaStatus, metaSeq)) {
        m_cachedStatus = metaStatus;
        if (!metaHasData || metaCount == 0) {
            m_hasActiveArticle = false;
            m_activeTotalCount = 0;
            m_needsInitialPick = true;
            return;
        }
        if (metaSeq != m_lastSeenSeq && m_hasActiveArticle) {
            // Background refresh completed: reshuffle on the next article boundary without
            // interrupting or corrupting the article currently scrolling on screen.
            m_lastSeenSeq = metaSeq;
            m_activeTotalCount = metaCount;
            m_shuffleCount = 0;
            m_shufflePos = 0;
        }
    }

    if (!m_hasActiveArticle || m_needsInitialPick) {
        if (!latchNextRandomArticle(mW, mH, now)) {
            return;
        }
        m_needsInitialPick = false;
        currentPageIndex = 0;
        lastPageSwitchTime = now;
        scrollPixelOffset = 0;
        m_scrollRemUs = 0;
        scrollState = ScrollState::PauseStart;
        stateStartTime = now;
        lastScrollTick = micros();
        lastArticleSwitchTime = now;
    } else if (m_needsArticleReload) {
        latchArticleAtIndex(currentArticleIndex, mW, mH, now);
    } else if (cachedArticleIndex != static_cast<int>(currentArticleIndex)) {
        rebuildLayoutCache(mW, mH, now);
    }

    const size_t target = static_cast<size_t>(std::max(1, config_articles_per_rotation));
    if (m_articlesShownThisCycle >= target) {
        // Hold final completed frame cleanly until RotationManager transitions or re-activates.
        return;
    }

    const bool isTate = (mH > mW);
    int linesPerPage = 2;
    if (isTate) {
        linesPerPage = max(2, (mH - 24) / 9);
    } else if (mH >= 64) {
        linesPerPage = 4;
    }

    const uint32_t basePxPerSec = GNewsTickerUtils::scrollSpeedPxPerSec(config_scroll_speed);

    if (config_display_mode == "static_paged") {
        totalPages = (cachedLineCount == 0) ? 1 : ((cachedLineCount + static_cast<size_t>(linesPerPage) - 1) / static_cast<size_t>(linesPerPage));
        if (totalPages < 1) totalPages = 1;
        if (currentPageIndex >= totalPages) currentPageIndex = totalPages - 1;

        const uint32_t totalArticleMs = static_cast<uint32_t>(std::max(5, config_article_duration_sec)) * 1000UL;
        const uint32_t pageDurationMs = std::max<uint32_t>(2000U, totalArticleMs / static_cast<uint32_t>(totalPages));

        if (now - lastPageSwitchTime >= pageDurationMs) {
            lastPageSwitchTime = now;
            if (currentPageIndex + 1 < totalPages) {
                currentPageIndex++;
            } else {
                advanceToNextArticle(mW, mH, now);
            }
        }
    } else {
        const uint32_t effPxPerSec = (config_display_mode == "vertical_crawl")
            ? std::max<uint32_t>(5U, basePxPerSec / 2U)
            : basePxPerSec;

        switch (scrollState) {
            case ScrollState::PauseStart:
                if (cachedMaxScroll == 0) {
                    const uint32_t durationMs = static_cast<uint32_t>(std::max(5, config_article_duration_sec)) * 1000UL;
                    if (now - lastArticleSwitchTime >= durationMs) {
                        advanceToNextArticle(mW, mH, now);
                    }
                } else if (now - stateStartTime >= static_cast<uint32_t>(std::max(0, config_scroll_pause_start_ms))) {
                    scrollState = ScrollState::Scrolling;
                    stateStartTime = now;
                    m_scrollRemUs = 0;
                    lastScrollTick = micros();
                }
                break;
            case ScrollState::Scrolling: {
                const uint32_t nowUs = micros();
                const uint32_t deltaUs = nowUs - lastScrollTick;
                lastScrollTick = nowUs;
                if (GNewsTickerUtils::advanceScrollSubpixels(scrollPixelOffset, m_scrollRemUs, deltaUs, effPxPerSec)) {
                    if (scrollPixelOffset >= cachedMaxScroll) {
                        scrollPixelOffset = cachedMaxScroll;
                        scrollState = ScrollState::PauseEnd;
                        stateStartTime = now;
                    }
                }
                break;
            }
            case ScrollState::PauseEnd:
                if (now - stateStartTime >= static_cast<uint32_t>(std::max(0, config_scroll_pause_end_ms))) {
                    advanceToNextArticle(mW, mH, now);
                }
                break;
        }
    }
}

void GNewsEngine::render(EngineContext* context) {
    if (!context || !context->getSurface()) return;
    auto* matrix = context->getSurface();
    int mW = matrix->width();
    int mH = matrix->height();

    matrix->cp437(true);
    matrix->fillScreen(0x0000);

    if (!m_hasActiveArticle) {
        size_t metaCount = 0;
        bool metaHasData = false;
        uint8_t metaStatus = m_cachedStatus;
        uint32_t metaSeq = 0;
        if (gnewsService.tryReadMetaLockFree(metaCount, metaHasData, metaStatus, metaSeq)) {
            m_cachedStatus = metaStatus;
        }
        const char* statusMsg = I18n::getGNewsStatusLabel(m_cachedStatus);
        uint16_t msgColor = matrix->color565(0, 229, 255);
        if (m_cachedStatus == 2) { // INVALID_KEY
            msgColor = matrix->color565(255, 50, 50); // Red
        } else if (m_cachedStatus == 1) { // EMPTY_KEY
            msgColor = matrix->color565(255, 170, 0); // Amber
        } else if (m_cachedStatus == 3 || m_cachedStatus == 4) { // RATE_LIMITED or NETWORK_ERROR
            msgColor = matrix->color565(255, 140, 0); // Orange
        }

        matrix->setTextSize(1);
        matrix->setTextColor(msgColor);
        matrix->setTextWrap(false);
        int textW = strlen(statusMsg) * 6;
        int curX = max(2, (mW - textW) / 2);
        matrix->setCursor(curX, mH / 2 - 4);
        matrix->print(statusMsg);

        if (config_show_beacon) {
            uint8_t br = (uint8_t)(beaconPulse * 255.0f);
            uint16_t bCol = (m_cachedStatus == 2) ? matrix->color565(br, 20, 20) : matrix->color565(br, 30, 30);
            matrix->fillCircle(mW - 6, mH / 2, 2, bCol);
        }
        matrix->cp437(false);
        return;
    }

    const size_t totalCount = (m_activeTotalCount > 0) ? m_activeTotalCount : 1;
    bool isTate = (mH > mW);
    if (isTate) {
        renderVertical(context, m_activeCategory, m_activeBadgeColor, totalCount);
    } else if (mW >= 128) {
        renderWide(context, m_activeCategory, m_activeBadgeColor, totalCount);
    } else {
        renderCompact(context, m_activeCategory, m_activeBadgeColor, totalCount);
    }
    matrix->cp437(false);
}

uint16_t GNewsEngine::resolveBadgeColor(uint16_t badgeColor) const {
    if (config_theme == "breaking_crimson") return 0xF949;
    if (config_theme == "cyberpunk") return 0x073F;
    if (config_theme == "monochrome_paper") return 0xDEFB;
    return badgeColor;
}

bool GNewsEngine::renderCommonBodyModes(EngineContext* context, int xPad, int bodyY, int clipTopY, int lineSpacing, int linesPerPage) {
    auto* matrix = context->getSurface();
    const int mW = matrix->width();
    const int mH = matrix->height();

    if (config_display_mode == "static_paged") {
        const size_t startLine = currentPageIndex * static_cast<size_t>(linesPerPage);
        const size_t endLine = min(startLine + static_cast<size_t>(linesPerPage), cachedLineCount);
        matrix->setTextColor(0xFFFF);
        matrix->setTextWrap(false);
        for (size_t i = startLine; i < endLine; i++) {
            const int y = bodyY + static_cast<int>(i - startLine) * lineSpacing;
            if (y < mH) {
                matrix->setCursor(xPad, y);
                matrix->print(cachedDisplayLines[i]);
            }
        }
        return true;
    }
    if (config_display_mode == "vertical_crawl") {
        const int baseY = bodyY - scrollPixelOffset;
        matrix->setTextColor(0xFFFF);
        matrix->setTextWrap(false);
        for (size_t i = 0; i < cachedLineCount; i++) {
            const int y = baseY + static_cast<int>(i) * lineSpacing;
            if (y + 8 > clipTopY && y < mH) {
                matrix->setCursor(xPad, y);
                matrix->print(cachedDisplayLines[i]);
            }
        }
        return true;
    }
    if (config_display_mode == "serpentine") {
        renderSerpentine(context, formattedHeadline, bodyY, 2, mW - 2, clipTopY + 1, mH, lineSpacing, linesPerPage);
        return true;
    }
    return false;
}

void GNewsEngine::renderWide(EngineContext* context, const char* category, uint16_t badgeColor, size_t totalCount) {
    if (!context || !context->getSurface()) return;
    auto* matrix = context->getSurface();
    int mW = matrix->width();
    int mH = matrix->height();

    int divY = (mH >= 64) ? 16 : 11;
    int bodyY = (mH >= 64) ? 19 : 13;
    int lineSpacing = (mH >= 64) ? 10 : 9;
    int linesPerPage = (mH >= 64) ? 4 : 2;

    matrix->setTextSize(1);

    // 1. Headline Content Area rendered FIRST (so header occludes it cleanly when scrolling)
    if (!renderCommonBodyModes(context, 4, bodyY, divY, lineSpacing, linesPerPage)) {
        const int startX = 4 - scrollPixelOffset;
        const int tickerY = (mH >= 64) ? (divY + 8) : bodyY;
        const uint8_t tickerScale = (mH >= 64 && mW >= 256) ? 2U : 1U;
        drawClippedSingleLineText(matrix, formattedHeadline, startX, tickerY, 0, mW, 0xFFFF, tickerScale);
    }

    // 2. Clear Header Bar area to occlude any scrolling text
    matrix->fillRect(0, 0, mW, divY + 1, 0x0000);

    // 3. Dynamic Header Bar
    uint16_t catColor = resolveBadgeColor(badgeColor);
    const int headerY = (mH >= 64) ? 4 : 2;
    const char* catShort = getCategoryShort(category);
    const int catW = static_cast<int>(strlen(catShort)) * 6 + 5;
    const int badgeX = config_show_beacon ? 11 : 4;
    const int srcSlotX = badgeX + (config_show_category_badge ? (catW + 5) : 0);
    const size_t dotsCount = (totalCount > 6) ? 6 : totalCount;
    const int dotsStartX = (config_show_progress_dots && totalCount > 1)
        ? (mW - (static_cast<int>(dotsCount) * 5 + 3))
        : (mW - 3);

    // News Source Name rendered first so left/right masks never erase badge, beacon, or dots
    int postSrcX = srcSlotX;
    if (config_show_source) {
        const int maxSrcW = max(20, dotsStartX - srcSlotX - 3);
        const int srcW = static_cast<int>(strlen(formattedSource)) * 6;
        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(200, 210, 225));
        matrix->setTextWrap(false);

        if (srcW <= maxSrcW) {
            matrix->setCursor(srcSlotX, headerY + 1);
            matrix->print(formattedSource);
            postSrcX = srcSlotX + srcW + 5;
        } else {
            const int totalSrcW = srcW + 16;
            const int drawX1 = srcSlotX - static_cast<int>(sourceMarqueeOffset % static_cast<uint32_t>(totalSrcW));
            matrix->setCursor(drawX1, headerY + 1);
            matrix->print(formattedSource);
            if (drawX1 + totalSrcW < dotsStartX) {
                matrix->setCursor(drawX1 + totalSrcW, headerY + 1);
                matrix->print(formattedSource);
            }
            matrix->fillRect(0, headerY, srcSlotX, 10, 0x0000);
            matrix->fillRect(dotsStartX, headerY, mW - dotsStartX, 10, 0x0000);
            postSrcX = dotsStartX - 1;
        }
    }

    if (config_show_beacon) {
        const uint8_t br = 120 + static_cast<uint8_t>(beaconPulse * 135.0f);
        matrix->fillCircle(6, headerY + 4, 2, matrix->color565(br, 15, 25));
    }

    if (config_show_category_badge) {
        matrix->fillRoundRect(badgeX, headerY, catW, 9, 2, matrix->color565(20, 25, 35));
        matrix->drawRoundRect(badgeX, headerY, catW, 9, 2, catColor);
        matrix->setTextSize(1);
        matrix->setTextColor(catColor);
        matrix->setCursor(badgeX + 3, headerY + 1);
        matrix->print(catShort);
    }

    if (config_show_progress_dots && totalCount > 1 && dotsStartX >= postSrcX) {
        const size_t activeDot = currentArticleIndex % dotsCount;
        for (size_t i = 0; i < dotsCount; i++) {
            const int dx = dotsStartX + static_cast<int>(i) * 5;
            if (i == activeDot) {
                matrix->fillCircle(dx + 1, headerY + 4, 1, catColor);
            } else {
                matrix->drawPixel(dx + 1, headerY + 4, matrix->color565(70, 75, 85));
            }
        }
    }

    // Divider Line
    matrix->drawFastHLine(2, divY, mW - 4, matrix->color565(40, 45, 55));
}

void GNewsEngine::renderCompact(EngineContext* context, const char* category, uint16_t badgeColor, size_t totalCount) {
    if (!context || !context->getSurface()) return;
    auto* matrix = context->getSurface();
    int mW = matrix->width();

    int divY = 10;
    int bodyY = 13;
    int lineSpacing = 9;
    int linesPerPage = 2;

    matrix->setTextSize(1);

    // 1. Headline content area rendered FIRST
    if (!renderCommonBodyModes(context, 2, bodyY, divY, lineSpacing, linesPerPage)) {
        const int startX = 2 - scrollPixelOffset;
        drawClippedSingleLineText(matrix, formattedHeadline, startX, 15, 0, mW, 0xFFFF, 1U);
    }

    // 2. Clear Header Bar area to occlude any scrolling text
    matrix->fillRect(0, 0, mW, divY + 1, 0x0000);

    // 3. Compact Header Bar
    uint16_t catColor = resolveBadgeColor(badgeColor);

    if (config_show_beacon) {
        uint8_t br = (uint8_t)(beaconPulse * 255.0f);
        matrix->fillCircle(3, 3, 2, matrix->color565(br, 20, 20));
    }

    // Category
    const char* catShort = getCategoryShort(category);
    matrix->setTextSize(1);
    matrix->setTextColor(catColor);
    matrix->setCursor(8, 1);
    matrix->print(catShort);

    // Article index
    char idxBuf[16];
    const unsigned dispIdx = static_cast<unsigned>((currentArticleIndex % totalCount) + 1);
    int idxLen = snprintf(idxBuf, sizeof(idxBuf), "%u/%u", dispIdx, (unsigned)totalCount);
    if (idxLen < 0) idxLen = 0;
    int idxX = mW - (idxLen * 6 + 2);
    matrix->setTextColor(matrix->color565(140, 150, 160));
    matrix->setCursor(idxX, 1);
    matrix->print(idxBuf);

    matrix->drawFastHLine(0, divY, mW, matrix->color565(35, 40, 50));
}

void GNewsEngine::renderVertical(EngineContext* context, const char* category, uint16_t badgeColor, size_t totalCount) {
    (void)totalCount;
    if (!context || !context->getSurface()) return;
    auto* matrix = context->getSurface();
    int mW = matrix->width();
    int mH = matrix->height();

    int bodyY = 24;
    int lineSpacing = 9;
    int linesPerPage = max(2, (mH - bodyY) / lineSpacing);

    matrix->setTextSize(1);

    // 1. Headline content area rendered FIRST
    if (!renderCommonBodyModes(context, 2, bodyY, 21, lineSpacing, linesPerPage)) {
        const int startX = mW - scrollPixelOffset;
        for (size_t i = 0; i < cachedLineCount; i++) {
            const int y = bodyY + static_cast<int>(i) * lineSpacing;
            if (y + 8 > 22 && y < mH) {
                drawClippedSingleLineText(matrix, cachedDisplayLines[i], startX, y, 0, mW, 0xFFFF, 1U);
            }
        }
    }

    // 2. Clear Header Bar area to occlude any scrolling text
    matrix->fillRect(0, 0, mW, 23, 0x0000);

    // 3. Top indicator
    uint16_t catColor = resolveBadgeColor(badgeColor);

    const char* catShort = getCategoryShort(category);
    matrix->setTextSize(1);
    matrix->setTextColor(catColor);
    matrix->setCursor(2, 2);
    matrix->print(catShort);

    if (config_show_beacon) {
        uint8_t br = (uint8_t)(beaconPulse * 255.0f);
        matrix->fillCircle(mW - 4, 5, 2, matrix->color565(br, 20, 20));
    }

    matrix->drawFastHLine(2, 11, mW - 4, matrix->color565(40, 45, 55));

    // Source (Marquee if long)
    matrix->setTextColor(matrix->color565(160, 175, 195));
    matrix->setTextWrap(false);
    int srcW = strlen(formattedSource) * 6;
    if (srcW <= (mW - 4)) {
        matrix->setCursor(2, 14);
        matrix->print(formattedSource);
    } else {
        int gap = 14;
        int totalSrcW = srcW + gap;
        int dx = (int)(sourceMarqueeOffset % totalSrcW);
        int drawX1 = 2 - dx;
        matrix->setCursor(drawX1, 14);
        matrix->print(formattedSource);
        int drawX2 = drawX1 + totalSrcW;
        if (drawX2 < mW - 2) {
            matrix->setCursor(drawX2, 14);
            matrix->print(formattedSource);
        }
        matrix->fillRect(0, 14, 2, 8, 0x0000);
        matrix->fillRect(mW - 2, 14, 2, 8, 0x0000);
    }

    matrix->drawFastHLine(2, 22, mW - 4, matrix->color565(40, 45, 55));
}

EngineDescriptor GNewsEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"gnews", "GNews Live Feed", "news", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = true;
    desc.requirements.targetFps = 30;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 12000;
    desc.requirements.internalContiguousBytes = 16000;
    desc.requirements.psramBytes = 0;

    static constexpr FieldTableEntry kFields[] = {
        ConfigField("api_key", ConfigType::STRING, "API Key", "GNews.io API key (comma-separated for multi-key pool)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("category", ConfigType::ENUM, "Category", "News topic category", "technology", false, "", "", "", "general,world,nation,business,technology,entertainment,sports,science,health", "", true, "", ValidationPolicy::FallbackDefault),
        ConfigField("keywords", ConfigType::STRING, "Keywords", "Custom search query or filter tags", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("lang", ConfigType::ENUM, "Language", "Article language (auto matches system)", "auto", false, "", "", "", "auto,en,fr,es,de,it,pt,nl,ru,zh,ja", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("country", ConfigType::ENUM, "Country", "Country edition", "auto", false, "", "", "", "auto,us,fr,gb,es,de,ca,it,jp,au,br,in", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("max_articles", ConfigType::INTEGER, "Max Articles", "Headlines count per cycle", "5", false, "3", "15", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("articles_per_rotation", ConfigType::INTEGER, "Articles per Rotation", "Number of random articles to scroll before rotating to the next screen (overrides slot timer)", "2", false, "1", "15", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("show_title", ConfigType::BOOLEAN, "Show Title", "Include article title in the scrolling feed", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_description", ConfigType::BOOLEAN, "Show Description", "Include article summary description in the scrolling feed", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_content", ConfigType::BOOLEAN, "Show Content", "Include full article body content in the scrolling feed", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("requests_per_day", ConfigType::INTEGER, "Daily Requests Budget", "Total API requests per 24 hours (Free tier: max 100)", "10", false, "1", "100", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("force_refresh", ConfigType::BOOLEAN, "Force Refresh Now", "Purge cached news of obsolete language and immediately query API", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("cache_ttl_min", ConfigType::INTEGER, "Cache TTL (min)", "Minutes between fresh API requests", "30", false, "5", "120", "5", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("display_mode", ConfigType::ENUM, "Display Mode", "Animation style", "smooth_scroll", false, "", "", "", "smooth_scroll,vertical_crawl,static_paged,serpentine", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("scroll_speed", ConfigType::INTEGER, "Scroll Speed", "Ticker speed (1=Slow, 10=Turbo)", "3", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("scroll_pause_start_ms", ConfigType::INTEGER, "Start Pause (ms)", "Initial dwell before scrolling", "1200", false, "0", "4000", "100", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("scroll_pause_end_ms", ConfigType::INTEGER, "End Pause (ms)", "End dwell before switching", "1000", false, "0", "4000", "100", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("article_duration_sec", ConfigType::INTEGER, "Article Duration (s)", "Seconds per article", "12", false, "5", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("theme", ConfigType::ENUM, "Theme", "Color palette scheme", "category_dynamic", false, "", "", "", "category_dynamic,breaking_crimson,cyberpunk,monochrome_paper", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_category_badge", ConfigType::BOOLEAN, "Show Category Badge", "Display colored topic badge", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_source", ConfigType::BOOLEAN, "Show Source", "Display news source name", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_time_ago", ConfigType::BOOLEAN, "Show Time Ago", "Display relative time badge", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_beacon", ConfigType::BOOLEAN, "Show Live Beacon", "Display live pulsing broadcast dot", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_progress_dots", ConfigType::BOOLEAN, "Show Progress Dots", "Display headline index dots", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.schema.fields.assign(kFields, kFields + (sizeof(kFields) / sizeof(kFields[0])));

    desc.factory = []() { return std::unique_ptr<IEngine>(new GNewsEngine()); };
    return desc;
}
