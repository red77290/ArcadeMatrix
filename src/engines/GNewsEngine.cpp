#include "GNewsEngine.h"
#include "../core/Logger.h"
#include "../core/I18n.h"
#include <cmath>

GNewsEngine::GNewsEngine() {
    _geometry.width = 64;
    _geometry.height = 32;
    _geometry.layoutClass = LayoutClass::WIDE;
}

void GNewsEngine::applyConfig(const EngineConfig* config) {
    if (!config) return;
    config_api_key = config->getString("api_key", "");
    config_category = config->getString("category", "technology");
    config_keywords = config->getString("keywords", "");
    config_lang = config->getString("lang", "auto");
    config_country = config->getString("country", "auto");
    config_max_articles = config->getInt("max_articles", 5);
    config_cache_ttl_min = config->getInt("cache_ttl_min", 30);
    config_requests_per_day = config->getInt("requests_per_day", 10);
    config_force_refresh = config->getBool("force_refresh", false);
    config_display_mode = config->getString("display_mode", "smooth_scroll");
    config_scroll_speed = config->getInt("scroll_speed", 3);
    config_scroll_pause_start_ms = config->getInt("scroll_pause_start_ms", 1200);
    config_scroll_pause_end_ms = config->getInt("scroll_pause_end_ms", 1000);
    config_article_duration_sec = config->getInt("article_duration_sec", 12);
    config_theme = config->getString("theme", "category_dynamic");
    config_show_category_badge = config->getBool("show_category_badge", true);
    config_show_source = config->getBool("show_source", true);
    config_show_time_ago = config->getBool("show_time_ago", true);
    config_show_beacon = config->getBool("show_beacon", true);
    config_show_progress_dots = config->getBool("show_progress_dots", true);
}

EngineError GNewsEngine::initialize(EngineContext* context, const EngineConfig* config) {
    if (config) applyConfig(config);
    return EngineError::OK;
}

const char* GNewsEngine::getCategoryShort(const char* category) {
    if (!category) return "NEWS";
    String cat = String(category);
    cat.toLowerCase();
    if (cat.indexOf("tech") >= 0) return "TECH";
    if (cat.indexOf("sci") >= 0) return "SCI";
    if (cat.indexOf("sport") >= 0) return "SPORT";
    if (cat.indexOf("bus") >= 0 || cat.indexOf("fin") >= 0 || cat.indexOf("econ") >= 0) return "BIZ";
    if (cat.indexOf("world") >= 0 || cat.indexOf("nation") >= 0) return "WORLD";
    if (cat.indexOf("ent") >= 0 || cat.indexOf("art") >= 0) return "CULT";
    if (cat.indexOf("heal") >= 0) return "SANTE";
    return "NEWS";
}

void GNewsEngine::advanceToNextArticle(const GNewsSnapshot& snap) {
    if (snap.count > 0) {
        currentArticleIndex = (currentArticleIndex + 1) % snap.count;
    } else {
        currentArticleIndex = 0;
    }
    scrollPixelOffset = 0;
    cachedArticleIndex = -1;
    scrollState = ScrollState::PauseStart;
    stateStartTime = millis();
    lastScrollTick = millis();
    lastArticleSwitchTime = millis();
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

void GNewsEngine::renderSerpentine(EngineContext* context, const GNewsArticle& article, int bodyY, int clipMinX, int clipMaxX, int clipMinY, int clipMaxY, int lineSpacing) {
    if (!context || !context->getMatrix()) return;
    auto* matrix = context->getMatrix();
    int s = scrollPixelOffset;
    int charW = 6;
    int xLeft = clipMinX - charW;
    int xRight = clipMaxX;
    int w = max(1, xRight - xLeft);

    for (size_t r = 0; r < cachedLineCount; r++) {
        const char* line = cachedDisplayLines[r];
        int len = strlen(line);
        bool isOdd = (r % 2) != 0;

        for (int k = 0; k < len; k++) {
            char c = line[k];
            int x0 = clipMinX + 2 + (k * charW);
            int cx = 0;
            int cy = 0;
            bool visible = false;

            if (!isOdd) {
                // Even row: travels to the left towards xLeft
                int d0 = x0 - xLeft;
                if (s <= d0) {
                    cx = x0 - s;
                    cy = bodyY + ((int)r * lineSpacing);
                    visible = true;
                } else {
                    int rem = s - d0;
                    int levelsUp = 1 + (rem / w);
                    int curRow = (int)r - levelsUp;
                    if (curRow >= 0) {
                        int remInLevel = rem % w;
                        bool curIsOdd = (curRow % 2) != 0;
                        cx = curIsOdd ? (xLeft + remInLevel) : (xRight - remInLevel);
                        cy = bodyY + (curRow * lineSpacing);
                        visible = true;
                    }
                }
            } else {
                // Odd row: travels to the right towards xRight
                int d0 = xRight - x0;
                if (s <= d0) {
                    cx = x0 + s;
                    cy = bodyY + ((int)r * lineSpacing);
                    visible = true;
                } else {
                    int rem = s - d0;
                    int levelsUp = 1 + (rem / w);
                    int curRow = (int)r - levelsUp;
                    if (curRow >= 0) {
                        int remInLevel = rem % w;
                        bool curIsOdd = (curRow % 2) != 0;
                        cx = curIsOdd ? (xLeft + remInLevel) : (xRight - remInLevel);
                        cy = bodyY + (curRow * lineSpacing);
                        visible = true;
                    }
                }
            }

            if (visible && cy + 7 > clipMinY && cy < clipMaxY && cx + 5 > clipMinX && cx < clipMaxX) {
                matrix->drawChar(cx, cy, c, 0xFFFF, 0x0000, 1);
            }
        }
    }
}

void GNewsEngine::activate() {
    gnewsService.fetchNews(config_api_key, config_category, config_keywords,
                           config_lang, config_country, config_max_articles, config_cache_ttl_min,
                           config_requests_per_day, false);
    currentArticleIndex = 0;
    scrollPixelOffset = 0;
    sourceMarqueeOffset = 0;
    cachedArticleIndex = -1;
    scrollState = ScrollState::PauseStart;
    stateStartTime = millis();
    lastUpdateTime = millis();
    lastScrollTick = millis();
    lastSourceTick = millis();
    lastArticleSwitchTime = millis();
}

void GNewsEngine::onConfigChanged(const EngineConfig* config) {
    bool prevForce = config_force_refresh;
    applyConfig(config);
    cachedArticleIndex = -1;
    if (config_force_refresh && !prevForce) {
        gnewsService.purgeArticles();
        gnewsService.fetchNews(config_api_key, config_category, config_keywords,
                               config_lang, config_country, config_max_articles, config_cache_ttl_min,
                               config_requests_per_day, true);
    } else {
        gnewsService.fetchNews(config_api_key, config_category, config_keywords,
                               config_lang, config_country, config_max_articles, config_cache_ttl_min,
                               config_requests_per_day, false);
    }
}

void GNewsEngine::deactivate() {}

bool GNewsEngine::isFinished() const {
    return false;
}

void GNewsEngine::update(EngineContext* context) {
    uint32_t now = millis();
    lastUpdateTime = now;

    // Periodic scheduled check
    gnewsService.fetchNews(config_api_key, config_category, config_keywords,
                           config_lang, config_country, config_max_articles, config_cache_ttl_min,
                           config_requests_per_day, false);

    // Smooth sinusoidal pulsing beacon (0.0 to 1.0)
    beaconPulse = (sinf((float)sourceMarqueeOffset * 0.1f) + 1.0f) * 0.5f;

    GNewsSnapshot snap = gnewsService.getSnapshot();
    if (!snap.hasData || snap.count == 0) return;

    if (currentArticleIndex >= snap.count) {
        currentArticleIndex = 0;
    }
    const GNewsArticle& curArt = snap.articles[currentArticleIndex];

    // Smooth discrete source marquee advancement (every 35ms -> 1 pixel)
    if (now - lastSourceTick >= 35) {
        uint32_t steps = (now - lastSourceTick) / 35;
        sourceMarqueeOffset += steps;
        lastSourceTick += steps * 35;
    }

    // Refresh cached line buffers if article changed or lines uninitialized
    if (cachedArticleIndex != (int)currentArticleIndex) {
        cachedArticleIndex = (int)currentArticleIndex;
        int mW = _geometry.width > 0 ? _geometry.width : 64;
        int mH = _geometry.height > 0 ? _geometry.height : 32;
        bool isTate = (_geometry.layoutClass == LayoutClass::TALL || _geometry.layoutClass == LayoutClass::PORTRAIT || mH > (mW * 3) / 2 || mW < 48);

        int bodyY = 14;
        int lineSpacing = 9;
        if (isTate) {
            bodyY = 24;
        } else if (mW >= 128 || mH >= 64) {
            int divY = (mH >= 64) ? 16 : 12;
            bodyY = divY + ((mH >= 64) ? 8 : 4);
        }
        int numRows = max(1, (mH - bodyY) / lineSpacing);

        if (config_display_mode == "static_paged") {
            int maxW = isTate ? (mW - 4) : (mW - 8);
            wrapTextToLines(curArt.title, maxW);
            cachedMaxScroll = 0;
        } else if (config_display_mode == "vertical_crawl") {
            int maxW = isTate ? (mW - 4) : (mW - 8);
            wrapTextToLines(curArt.title, maxW);
            int totalH = (int)cachedLineCount * lineSpacing;
            int viewH = mH - bodyY;
            cachedMaxScroll = (totalH > viewH) ? (totalH - viewH + 12) : 0;
        } else if (config_display_mode == "serpentine") {
            distributeTextToRows(curArt.title, numRows);
            int maxLineChars = 0;
            for (size_t r = 0; r < cachedLineCount; r++) {
                int len = strlen(cachedDisplayLines[r]);
                if (len > maxLineChars) maxLineChars = len;
            }
            cachedMaxScroll = mW + (maxLineChars * 6) + 24;
        } else {
            // "smooth_scroll"
            int textW = strlen(curArt.title) * 6;
            cachedMaxScroll = textW + 16;
            wrapTextToLines(curArt.title, mW - 4);
        }
    }

    if (config_display_mode == "static_paged") {
        uint32_t durationMs = (uint32_t)config_article_duration_sec * 1000UL;
        if (now - lastArticleSwitchTime >= durationMs) {
            advanceToNextArticle(snap);
        }
    } else {
        uint32_t tickMs = 30;
        switch (config_scroll_speed) {
            case 1: tickMs = 60; break;
            case 2: tickMs = 45; break;
            case 3: tickMs = 35; break;
            case 4: tickMs = 28; break;
            case 5: tickMs = 22; break;
            case 6: tickMs = 18; break;
            case 7: tickMs = 14; break;
            case 8: tickMs = 10; break;
            case 9: tickMs = 7; break;
            case 10: default: tickMs = 4; break;
        }

        switch (scrollState) {
            case ScrollState::PauseStart:
                if (now - stateStartTime >= (uint32_t)config_scroll_pause_start_ms) {
                    scrollState = ScrollState::Scrolling;
                    stateStartTime = now;
                    lastScrollTick = now;
                }
                break;
            case ScrollState::Scrolling:
                if (now - lastScrollTick >= tickMs) {
                    uint32_t steps = (now - lastScrollTick) / tickMs;
                    scrollPixelOffset += (int)steps;
                    lastScrollTick += steps * tickMs;
                    if (scrollPixelOffset >= cachedMaxScroll) {
                        scrollState = ScrollState::PauseEnd;
                        stateStartTime = now;
                    }
                }
                break;
            case ScrollState::PauseEnd:
                if (now - stateStartTime >= (uint32_t)config_scroll_pause_end_ms) {
                    advanceToNextArticle(snap);
                }
                break;
        }
    }
}

void GNewsEngine::render(EngineContext* context) {
    if (!context || !context->getMatrix()) return;
    auto* matrix = context->getMatrix();
    int mW = matrix->width();
    int mH = matrix->height();

    matrix->fillScreen(0x0000);

    GNewsSnapshot snap = gnewsService.getSnapshot();
    if (!snap.hasData || snap.count == 0) {
        const char* statusMsg = I18n::getGNewsStatusLabel(snap.status);
        uint16_t msgColor = matrix->color565(0, 229, 255);
        if (snap.status == 2) { // INVALID_KEY
            msgColor = matrix->color565(255, 50, 50); // Red
        } else if (snap.status == 1) { // EMPTY_KEY
            msgColor = matrix->color565(255, 170, 0); // Amber
        } else if (snap.status == 3 || snap.status == 4) { // RATE_LIMITED or NETWORK_ERROR
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
            uint16_t bCol = (snap.status == 2) ? matrix->color565(br, 20, 20) : matrix->color565(br, 30, 30);
            matrix->fillCircle(mW - 6, mH / 2, 2, bCol);
        }
        return;
    }

    if (currentArticleIndex >= snap.count) {
        currentArticleIndex = 0;
    }
    const GNewsArticle& article = snap.articles[currentArticleIndex];

    bool isTate = (_geometry.layoutClass == LayoutClass::TALL || _geometry.layoutClass == LayoutClass::PORTRAIT || mH > (mW * 3) / 2 || mW < 48);
    if (isTate) {
        renderVertical(context, article, snap.count);
    } else if (mW >= 128) {
        renderWide(context, article, snap.count);
    } else {
        renderCompact(context, article, snap.count);
    }
}

void GNewsEngine::renderWide(EngineContext* context, const GNewsArticle& article, size_t totalCount) {
    if (!context || !context->getMatrix()) return;
    auto* matrix = context->getMatrix();
    int mW = matrix->width();
    int mH = matrix->height();

    // 1. Dynamic Header Bar
    uint16_t catColor = article.badgeColor;
    if (config_theme == "breaking_crimson") catColor = 0xF949;
    else if (config_theme == "cyberpunk") catColor = 0x073F;
    else if (config_theme == "monochrome_paper") catColor = 0xDEFB;

    int curX = 4;
    int headerY = (mH >= 64) ? 4 : 2;

    // Pulsing live beacon
    if (config_show_beacon) {
        uint8_t br = 120 + (uint8_t)(beaconPulse * 135.0f);
        uint16_t bCol = matrix->color565(br, 15, 25);
        matrix->fillCircle(curX + 2, headerY + 4, 2, bCol);
        curX += 7;
    }

    // Category Pill Badge (Compact: e.g. "TECH", "SCI", "NEWS")
    if (config_show_category_badge) {
        const char* catShort = getCategoryShort(article.category);
        int catW = strlen(catShort) * 6 + 5;
        matrix->fillRoundRect(curX, headerY, catW, 9, 2, matrix->color565(20, 25, 35));
        matrix->drawRoundRect(curX, headerY, catW, 9, 2, catColor);
        matrix->setTextSize(1);
        matrix->setTextColor(catColor);
        matrix->setCursor(curX + 3, headerY + 1);
        matrix->print(catShort);
        curX += catW + 5;
    }

    size_t dotsCount = (totalCount > 6) ? 6 : totalCount;
    int dotsStartX = (config_show_progress_dots && totalCount > 1) ? (mW - ((int)dotsCount * 5 + 3)) : (mW - 3);

    // News Source Name (with Marquee if long)
    if (config_show_source) {
        int maxSrcW = max(20, dotsStartX - curX - 3);
        int srcW = strlen(article.source) * 6;

        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(200, 210, 225));
        matrix->setTextWrap(false);

        if (srcW <= maxSrcW) {
            matrix->setCursor(curX, headerY + 1);
            matrix->print(article.source);
            curX += srcW + 5;
        } else {
            int gap = 16;
            int totalSrcW = srcW + gap;
            int dx = (int)(sourceMarqueeOffset % totalSrcW);
            int drawX1 = curX - dx;
            matrix->setCursor(drawX1, headerY + 1);
            matrix->print(article.source);
            int drawX2 = drawX1 + totalSrcW;
            if (drawX2 < dotsStartX) {
                matrix->setCursor(drawX2, headerY + 1);
                matrix->print(article.source);
            }
            // Clear side bounds outside the header slot
            matrix->fillRect(0, headerY, curX, 10, 0x0000);
            if (config_show_category_badge) {
                const char* catShort = getCategoryShort(article.category);
                int catW = strlen(catShort) * 6 + 5;
                int badgeX = config_show_beacon ? 11 : 4;
                matrix->fillRoundRect(badgeX, headerY, catW, 9, 2, matrix->color565(20, 25, 35));
                matrix->drawRoundRect(badgeX, headerY, catW, 9, 2, catColor);
                matrix->setTextColor(catColor);
                matrix->setCursor(badgeX + 3, headerY + 1);
                matrix->print(catShort);
            }
            if (config_show_beacon) {
                uint8_t br = 120 + (uint8_t)(beaconPulse * 135.0f);
                uint16_t bCol = matrix->color565(br, 15, 25);
                matrix->fillCircle(6, headerY + 4, 2, bCol);
            }
            matrix->fillRect(dotsStartX, headerY, mW - dotsStartX, 10, 0x0000);
            curX = dotsStartX;
        }
    }

    // Progress Dots (e.g. ● ○ ○ ○ ○)
    if (config_show_progress_dots && totalCount > 1) {
        if (dotsStartX > curX) {
            for (size_t i = 0; i < dotsCount; i++) {
                int dx = dotsStartX + (int)i * 5;
                if (i == currentArticleIndex) {
                    matrix->fillCircle(dx + 1, headerY + 4, 1, catColor);
                } else {
                    matrix->drawPixel(dx + 1, headerY + 4, matrix->color565(70, 75, 85));
                }
            }
        }
    }

    // Divider Line
    int divY = (mH >= 64) ? 16 : 12;
    matrix->drawFastHLine(2, divY, mW - 4, matrix->color565(40, 45, 55));

    // 2. Headline Content Area
    int bodyY = divY + ((mH >= 64) ? 8 : 4);
    int lineSpacing = 9;

    if (config_display_mode == "static_paged") {
        for (size_t i = 0; i < cachedLineCount; i++) {
            int y = bodyY + (int)i * lineSpacing;
            if (y + 8 > divY && y < mH) {
                matrix->setCursor(4, y);
                matrix->setTextColor(0xFFFF);
                matrix->setTextWrap(false);
                matrix->print(cachedDisplayLines[i]);
            }
        }
    } else if (config_display_mode == "vertical_crawl") {
        int baseY = bodyY - scrollPixelOffset;
        for (size_t i = 0; i < cachedLineCount; i++) {
            int y = baseY + (int)i * lineSpacing;
            if (y + 8 > divY && y < mH) {
                matrix->setCursor(4, y);
                matrix->setTextColor(0xFFFF);
                matrix->setTextWrap(false);
                matrix->print(cachedDisplayLines[i]);
            }
        }
    } else if (config_display_mode == "serpentine") {
        renderSerpentine(context, article, bodyY, 0, mW, divY + 1, mH, lineSpacing);
    } else {
        int startX = 4 - scrollPixelOffset;
        matrix->setTextWrap(false);
        if (mH >= 64 && mW >= 256) {
            matrix->setTextSize(2);
            matrix->setTextColor(0xFFFF);
            matrix->setCursor(startX, bodyY);
            matrix->print(article.title);
        } else {
            matrix->setTextSize(1);
            matrix->setTextColor(0xFFFF);
            matrix->setCursor(startX, bodyY);
            matrix->print(article.title);
        }
    }
}

void GNewsEngine::renderCompact(EngineContext* context, const GNewsArticle& article, size_t totalCount) {
    if (!context || !context->getMatrix()) return;
    auto* matrix = context->getMatrix();
    int mW = matrix->width();
    int mH = matrix->height();

    uint16_t catColor = article.badgeColor;
    if (config_theme == "breaking_crimson") catColor = 0xF949;
    else if (config_theme == "cyberpunk") catColor = 0x073F;

    if (config_show_beacon) {
        uint8_t br = (uint8_t)(beaconPulse * 255.0f);
        matrix->fillCircle(3, 3, 2, matrix->color565(br, 20, 20));
    }

    // Category
    const char* catShort = getCategoryShort(article.category);
    matrix->setTextSize(1);
    matrix->setTextColor(catColor);
    matrix->setCursor(8, 1);
    matrix->print(catShort);

    // Article index
    String idx = String(currentArticleIndex + 1) + "/" + String(totalCount);
    int idxX = mW - (idx.length() * 6 + 2);
    matrix->setTextColor(matrix->color565(140, 150, 160));
    matrix->setCursor(idxX, 1);
    matrix->print(idx);

    matrix->drawFastHLine(0, 10, mW, matrix->color565(35, 40, 50));

    // Headline area
    int bodyY = 14;
    int lineSpacing = 9;

    if (config_display_mode == "static_paged") {
        for (size_t i = 0; i < cachedLineCount; i++) {
            int y = bodyY + (int)i * lineSpacing;
            if (y + 8 > 10 && y < mH) {
                matrix->setCursor(2, y);
                matrix->setTextColor(0xFFFF);
                matrix->setTextWrap(false);
                matrix->print(cachedDisplayLines[i]);
            }
        }
    } else if (config_display_mode == "vertical_crawl") {
        int baseY = bodyY - scrollPixelOffset;
        for (size_t i = 0; i < cachedLineCount; i++) {
            int y = baseY + (int)i * lineSpacing;
            if (y + 8 > 10 && y < mH) {
                matrix->setCursor(2, y);
                matrix->setTextColor(0xFFFF);
                matrix->setTextWrap(false);
                matrix->print(cachedDisplayLines[i]);
            }
        }
    } else if (config_display_mode == "serpentine") {
        renderSerpentine(context, article, bodyY, 0, mW, 11, mH, lineSpacing);
    } else {
        matrix->setTextColor(0xFFFF);
        int startX = 2 - scrollPixelOffset;
        matrix->setCursor(startX, 15);
        matrix->setTextWrap(false);
        matrix->print(article.title);
    }
}

void GNewsEngine::renderVertical(EngineContext* context, const GNewsArticle& article, size_t totalCount) {
    if (!context || !context->getMatrix()) return;
    auto* matrix = context->getMatrix();
    int mW = matrix->width();
    int mH = matrix->height();

    uint16_t catColor = article.badgeColor;
    if (config_theme == "breaking_crimson") catColor = 0xF949;
    else if (config_theme == "cyberpunk") catColor = 0x073F;

    // Top indicator
    const char* catShort = getCategoryShort(article.category);
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
    int srcW = strlen(article.source) * 6;
    if (srcW <= (mW - 4)) {
        matrix->setCursor(2, 14);
        matrix->print(article.source);
    } else {
        int gap = 14;
        int totalSrcW = srcW + gap;
        int dx = (int)(sourceMarqueeOffset % totalSrcW);
        int drawX1 = 2 - dx;
        matrix->setCursor(drawX1, 14);
        matrix->print(article.source);
        int drawX2 = drawX1 + totalSrcW;
        if (drawX2 < mW - 2) {
            matrix->setCursor(drawX2, 14);
            matrix->print(article.source);
        }
        matrix->fillRect(0, 14, 2, 8, 0x0000);
        matrix->fillRect(mW - 2, 14, 2, 8, 0x0000);
    }

    int bodyY = 24;
    int lineSpacing = 9;

    if (config_display_mode == "static_paged") {
        for (size_t i = 0; i < cachedLineCount; i++) {
            int y = bodyY + (int)i * lineSpacing;
            if (y + 8 > 20 && y < mH) {
                matrix->setCursor(2, y);
                matrix->setTextColor(0xFFFF);
                matrix->setTextWrap(false);
                matrix->print(cachedDisplayLines[i]);
            }
        }
    } else if (config_display_mode == "vertical_crawl") {
        int baseY = bodyY - scrollPixelOffset;
        for (size_t i = 0; i < cachedLineCount; i++) {
            int y = baseY + (int)i * lineSpacing;
            if (y + 8 > 20 && y < mH) {
                matrix->setCursor(2, y);
                matrix->setTextColor(0xFFFF);
                matrix->setTextWrap(false);
                matrix->print(cachedDisplayLines[i]);
            }
        }
    } else if (config_display_mode == "serpentine") {
        renderSerpentine(context, article, bodyY, 0, mW, 20, mH, lineSpacing);
    } else {
        matrix->setTextColor(0xFFFF);
        matrix->setCursor(2, 24);
        matrix->setTextWrap(true);
        matrix->print(article.title);
        matrix->setTextWrap(false);
    }
}

EngineDescriptor GNewsEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"gnews", "GNews Live Feed", "news", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.requirements.needsPsram = true;
    desc.requirements.needsNetwork = true;

    desc.schema.fields = {
        ConfigField("api_key", ConfigType::STRING, "API Key", "GNews.io API key (comma-separated for multi-key pool)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("category", ConfigType::ENUM, "Category", "News topic category", "technology", false, "", "", "", "general,world,nation,business,technology,entertainment,sports,science,health", "", true, "", ValidationPolicy::FallbackDefault),
        ConfigField("keywords", ConfigType::STRING, "Keywords", "Custom search query or filter tags", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("lang", ConfigType::ENUM, "Language", "Article language (auto matches system)", "auto", false, "", "", "", "auto,en,fr,es,de,it,pt,nl,ru,zh,ja", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("country", ConfigType::ENUM, "Country", "Country edition", "auto", false, "", "", "", "auto,us,fr,gb,es,de,ca,it,jp,au,br,in", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("max_articles", ConfigType::INTEGER, "Max Articles", "Headlines count per cycle", "5", false, "3", "15", "1", "", "", false, "", ValidationPolicy::Clamp),
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

    desc.factory = []() { return std::unique_ptr<IEngine>(new GNewsEngine()); };
    return desc;
}
