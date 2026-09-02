#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../include/core/EngineContract.h"
#include "../services/GNewsService.h"

/**
 * @class GNewsEngine
 * @brief Live breaking news ticker engine rendering headlines from GNews API across all matrix resolutions.
 */
class GNewsEngine : public IEngine {
public:
    enum class ScrollState {
        PauseStart,
        Scrolling,
        PauseEnd
    };

    GNewsEngine();
    ~GNewsEngine() override = default;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    bool isFinished() const override;
    bool isRealtime() const override { return true; }

    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override {
        _geometry = geometry;
    }

private:
    // Configuration properties
    String config_api_key = "";
    String config_category = "technology";
    String config_keywords = "";
    String config_lang = "auto";
    String config_country = "auto";
    int config_max_articles = 5;
    int config_cache_ttl_min = 30;
    int config_requests_per_day = 10;
    bool config_force_refresh = false;
    String config_display_mode = "smooth_scroll";
    int config_scroll_speed = 3;
    int config_scroll_pause_start_ms = 1200;
    int config_scroll_pause_end_ms = 1000;
    int config_article_duration_sec = 12;
    String config_theme = "category_dynamic";
    bool config_show_category_badge = true;
    bool config_show_source = true;
    bool config_show_time_ago = true;
    bool config_show_beacon = true;
    bool config_show_progress_dots = true;

    // Runtime state
    DisplayGeometry _geometry;
    size_t currentArticleIndex = 0;
    int scrollPixelOffset = 0;
    uint32_t sourceMarqueeOffset = 0;
    ScrollState scrollState = ScrollState::PauseStart;
    uint32_t stateStartTime = 0;
    uint32_t lastUpdateTime = 0;
    uint32_t lastScrollTick = 0;
    uint32_t lastSourceTick = 0;
    uint32_t lastArticleSwitchTime = 0;
    float beaconPulse = 0.0f;

    // Multi-line display pre-allocated static cache (Zero allocation on Core 1 hot path)
    static constexpr size_t MAX_DISPLAY_ROWS = 6;
    static constexpr size_t MAX_ROW_CHARS = 48;
    char cachedDisplayLines[MAX_DISPLAY_ROWS][MAX_ROW_CHARS];
    size_t cachedLineCount = 0;
    int cachedMaxScroll = 0;
    int cachedArticleIndex = -1;

    static const char* getCategoryShort(const char* category);
    void applyConfig(const EngineConfig* config);
    void advanceToNextArticle(const GNewsSnapshot& snap);
    void wrapTextToLines(const char* text, int maxW);
    void distributeTextToRows(const char* text, int numRows);
    void renderSerpentine(EngineContext* context, const GNewsArticle& article, int bodyY, int clipMinX, int clipMaxX, int clipMinY, int clipMaxY, int lineSpacing);
    void renderWide(EngineContext* context, const GNewsArticle& article, size_t totalCount);
    void renderCompact(EngineContext* context, const GNewsArticle& article, size_t totalCount);
    void renderVertical(EngineContext* context, const GNewsArticle& article, size_t totalCount);
};

class GNewsEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
