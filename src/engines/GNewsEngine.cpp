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

void GNewsEngine::activate() {
    gnewsService.fetchNews(config_api_key, config_category, config_keywords,
                           config_lang, config_country, config_max_articles, config_cache_ttl_min);
    currentArticleIndex = 0;
    scrollOffset = 0.0f;
    scrollState = ScrollState::PauseStart;
    stateStartTime = millis();
    lastUpdateTime = millis();
    lastArticleSwitchTime = millis();
}

void GNewsEngine::onConfigChanged(const EngineConfig* config) {
    applyConfig(config);
    gnewsService.fetchNews(config_api_key, config_category, config_keywords,
                           config_lang, config_country, config_max_articles, config_cache_ttl_min);
}

void GNewsEngine::deactivate() {}

bool GNewsEngine::isFinished() const {
    return false;
}

void GNewsEngine::advanceToNextArticle(const GNewsSnapshot& snap) {
    if (snap.count > 0) {
        currentArticleIndex = (currentArticleIndex + 1) % snap.count;
    } else {
        currentArticleIndex = 0;
    }
    scrollOffset = 0.0f;
    scrollState = ScrollState::PauseStart;
    stateStartTime = millis();
    lastArticleSwitchTime = millis();
}

void GNewsEngine::update(EngineContext* context) {
    uint32_t now = millis();
    float dt = (now - lastUpdateTime) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    if (dt <= 0.0f) dt = 0.016f;
    lastUpdateTime = now;

    // Smooth sinusoidal pulsing beacon (0.0 to 1.0)
    beaconPulse = (sinf(now * 0.007f) + 1.0f) * 0.5f;

    GNewsSnapshot snap = gnewsService.getSnapshot();
    if (!snap.hasData || snap.count == 0) return;

    if (currentArticleIndex >= snap.count) {
        currentArticleIndex = 0;
    }
    const GNewsArticle& curArt = snap.articles[currentArticleIndex];

    if (config_display_mode == "static_paged") {
        uint32_t durationMs = (uint32_t)config_article_duration_sec * 1000UL;
        if (now - lastArticleSwitchTime >= durationMs) {
            advanceToNextArticle(snap);
        }
    } else {
        // Smooth horizontal ticker
        float speedPps = config_scroll_speed * 12.0f + 6.0f;
        int textWidth = strlen(curArt.title) * 6;
        int displayW = (context && context->getMatrix()) ? context->getMatrix()->width() : 64;

        switch (scrollState) {
            case ScrollState::PauseStart:
                if (now - stateStartTime >= (uint32_t)config_scroll_pause_start_ms) {
                    scrollState = ScrollState::Scrolling;
                    stateStartTime = now;
                }
                break;
            case ScrollState::Scrolling:
                scrollOffset += speedPps * dt;
                if (scrollOffset >= (textWidth + 8)) {
                    scrollState = ScrollState::PauseEnd;
                    stateStartTime = now;
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
        // Sleek placeholder
        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(0, 229, 255));
        matrix->setCursor((mW > 64) ? 8 : 2, mH / 2 - 4);
        matrix->print("GNEWS LIVE");
        if (config_show_beacon) {
            uint8_t br = (uint8_t)(beaconPulse * 255.0f);
            uint16_t bCol = matrix->color565(br, 30, 30);
            matrix->fillCircle(mW - 6, mH / 2, 2, bCol);
        }
        return;
    }

    if (currentArticleIndex >= snap.count) {
        currentArticleIndex = 0;
    }
    const GNewsArticle& article = snap.articles[currentArticleIndex];

    if (_geometry.layoutClass == LayoutClass::TALL || _geometry.layoutClass == LayoutClass::PORTRAIT || mH > mW) {
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
        curX += 8;
    }

    // Category Pill Badge
    if (config_show_category_badge) {
        String catName = String(article.category);
        catName.toUpperCase();
        int catW = catName.length() * 6 + 4;
        matrix->fillRoundRect(curX, headerY, catW, 9, 2, matrix->color565(20, 25, 35));
        matrix->drawRoundRect(curX, headerY, catW, 9, 2, catColor);
        matrix->setTextSize(1);
        matrix->setTextColor(catColor);
        matrix->setCursor(curX + 2, headerY + 1);
        matrix->print(catName);
        curX += catW + 6;
    }

    // News Source Name
    if (config_show_source) {
        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(200, 210, 225));
        matrix->setCursor(curX, headerY + 1);
        matrix->print(article.source);
        curX += strlen(article.source) * 6 + 6;
    }

    // Progress Dots (e.g. ● ○ ○ ○ ○)
    if (config_show_progress_dots && totalCount > 1) {
        int dotsStartX = mW - (totalCount * 6 + 4);
        if (dotsStartX > curX) {
            for (size_t i = 0; i < totalCount && i < 8; i++) {
                int dx = dotsStartX + (int)i * 6;
                if (i == currentArticleIndex) {
                    matrix->fillCircle(dx, headerY + 4, 2, catColor);
                } else {
                    matrix->drawCircle(dx, headerY + 4, 1, matrix->color565(70, 75, 85));
                }
            }
        }
    }

    // Divider Line
    int divY = (mH >= 64) ? 16 : 12;
    matrix->drawFastHLine(2, divY, mW - 4, matrix->color565(40, 45, 55));

    // 2. Headline Content Area
    int bodyY = divY + ((mH >= 64) ? 8 : 4);
    matrix->setTextSize((mH >= 64 && mW >= 256) ? 2 : 1);
    matrix->setTextColor(0xFFFF);

    if (config_display_mode == "static_paged" && mH >= 64) {
        // Multi-line word wrapped
        matrix->setCursor(4, bodyY);
        matrix->setTextWrap(true);
        matrix->print(article.title);
        matrix->setTextWrap(false);
    } else {
        // Smooth scrolling ticker
        int startX = (mH >= 64 && mW >= 256) ? (4 - (int)(scrollOffset * 1.5f)) : (4 - (int)scrollOffset);
        matrix->setCursor(startX, bodyY);
        matrix->setTextWrap(false);
        matrix->print(article.title);
    }
}

void GNewsEngine::renderCompact(EngineContext* context, const GNewsArticle& article, size_t totalCount) {
    if (!context || !context->getMatrix()) return;
    auto* matrix = context->getMatrix();
    int mW = matrix->width();
    int mH = matrix->height();

    // Compact Header (64x32 or small)
    uint16_t catColor = article.badgeColor;
    if (config_theme == "breaking_crimson") catColor = 0xF949;
    else if (config_theme == "cyberpunk") catColor = 0x073F;

    if (config_show_beacon) {
        uint8_t br = 100 + (uint8_t)(beaconPulse * 155.0f);
        matrix->fillCircle(3, 4, 2, matrix->color565(br, 20, 20));
    }

    matrix->setTextSize(1);
    matrix->setTextColor(catColor);
    matrix->setCursor(8, 1);
    String cat = String(article.category);
    cat.toUpperCase();
    if (cat.length() > 6) cat = cat.substring(0, 6);
    matrix->print(cat);

    // Source pill or article index
    matrix->setTextColor(matrix->color565(140, 150, 160));
    matrix->setCursor(mW - 16, 1);
    matrix->printf("%d/%d", (int)currentArticleIndex + 1, (int)totalCount);

    matrix->drawFastHLine(0, 10, mW, matrix->color565(35, 40, 50));

    // Headline ticker
    matrix->setTextColor(0xFFFF);
    int startX = 2 - (int)scrollOffset;
    matrix->setCursor(startX, 15);
    matrix->setTextWrap(false);
    matrix->print(article.title);
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
    matrix->setTextSize(1);
    matrix->setTextColor(catColor);
    matrix->setCursor(2, 2);
    matrix->print("NEWS");

    if (config_show_beacon) {
        uint8_t br = (uint8_t)(beaconPulse * 255.0f);
        matrix->fillCircle(mW - 4, 5, 2, matrix->color565(br, 20, 20));
    }

    matrix->drawFastHLine(2, 11, mW - 4, matrix->color565(40, 45, 55));

    // Source
    matrix->setTextColor(matrix->color565(160, 175, 195));
    matrix->setCursor(2, 14);
    matrix->print(article.source);

    // Multi-line wrapped title
    matrix->setTextColor(0xFFFF);
    matrix->setCursor(2, 25);
    matrix->setTextWrap(true);
    matrix->print(article.title);
    matrix->setTextWrap(false);
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
        ConfigField("api_key", ConfigType::STRING, "API Key", "GNews.io API key (uses RSS/Demo if empty)", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("category", ConfigType::ENUM, "Category", "News topic category", "technology", false, "", "", "", "general,world,nation,business,technology,entertainment,sports,science,health", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("keywords", ConfigType::STRING, "Keywords", "Custom search query or filter tags", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("lang", ConfigType::ENUM, "Language", "Article language (auto matches system)", "auto", false, "", "", "", "auto,en,fr,es,de,it,pt,nl,ru,zh,ja", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("country", ConfigType::ENUM, "Country", "Country edition", "auto", false, "", "", "", "auto,us,fr,gb,es,de,ca,it,jp,au,br,in", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("max_articles", ConfigType::INTEGER, "Max Articles", "Headlines count per cycle", "5", false, "3", "15", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("cache_ttl_min", ConfigType::INTEGER, "Cache TTL (min)", "Minutes between fresh API requests", "30", false, "5", "120", "5", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("display_mode", ConfigType::ENUM, "Display Mode", "Animation style", "smooth_scroll", false, "", "", "", "smooth_scroll,static_paged", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("scroll_speed", ConfigType::INTEGER, "Scroll Speed", "Ticker speed (1=Slow, 5=Turbo)", "3", false, "1", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
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
