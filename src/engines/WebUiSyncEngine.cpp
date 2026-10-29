/**
 * @file WebUiSyncEngine.cpp
 * @brief Zero-heap localized WebUI loading notice for non-PSRAM ESP32 boards (`esp32dev`).
 */
#include "WebUiSyncEngine.h"
#include <cstring>
#include <algorithm>

std::atomic<bool> WebUiSyncEngine::s_requested{false};
std::atomic<bool> WebUiSyncEngine::s_readyForServing{false};

namespace {
void drawCenteredClippedLine(
    IDrawingSurface* s,
    const char* text,
    int16_t viewW,
    int16_t y,
    uint16_t color
) {
    if (!s || !text || text[0] == '\0' || viewW <= 4) return;
    size_t maxChars = static_cast<size_t>((viewW - 2) / 6);
    if (maxChars == 0) return;

    char clipped[32];
    size_t srcLen = strlen(text);
    size_t copyLen = std::min<size_t>(srcLen, std::min<size_t>(maxChars, sizeof(clipped) - 1));
    memcpy(clipped, text, copyLen);
    clipped[copyLen] = '\0';

    int16_t textPx = static_cast<int16_t>(copyLen * 6);
    int16_t x = std::max<int16_t>(1, static_cast<int16_t>((viewW - textPx) / 2));
    s->setTextColor(color);
    s->setCursor(x, y);
    s->print(clipped);
}
} // namespace

EngineError WebUiSyncEngine::initialize(EngineContext* context, const EngineConfig* config) {
    (void)config;
    m_surface = context ? context->getSurface() : nullptr;
    m_dirty = true;
    m_renderedNewFrame = false;
    return EngineError::OK;
}

void WebUiSyncEngine::activate() {
    m_lastAnimStepMs = millis();
    m_animStep = 0;
    m_lastLang = I18n::getLang();
    m_dirty = true;
    m_renderedNewFrame = false;
    s_readyForServing.store(true, std::memory_order_release);
}

void WebUiSyncEngine::deactivate() {
    m_dirty = false;
    m_renderedNewFrame = false;
    s_readyForServing.store(false, std::memory_order_release);
    s_requested.store(false, std::memory_order_release);
}

void WebUiSyncEngine::update(EngineContext* context) {
    (void)context;
    uint32_t now = millis();
    Lang curLang = I18n::getLang();
    if (curLang != m_lastLang) {
        m_lastLang = curLang;
        m_dirty = true;
    }
    if ((now - m_lastAnimStepMs) >= 180U) {
        m_lastAnimStepMs = now;
        m_animStep = static_cast<uint8_t>((m_animStep + 1U) & 0x07U);
        m_dirty = true;
    }
}

void WebUiSyncEngine::render(EngineContext* context) {
    if (!m_dirty) {
        m_renderedNewFrame = false;
        return;
    }
    IDrawingSurface* s = (context && context->getSurface()) ? context->getSurface() : m_surface;
    if (!s) {
        m_renderedNewFrame = false;
        return;
    }

    const int16_t w = s->width();
    const int16_t h = s->height();
    const char* line1 = I18n::getWebUiLoadingTitleLabel(m_lastLang);
    const char* line2 = I18n::getWebUiLoadingSubLabel(m_lastLang);

    const uint16_t borderCol = s->color565(0, 96, 160);
    const uint16_t titleCol  = s->color565(0, 229, 255);
    const uint16_t subCol    = s->color565(255, 255, 255);
    const uint16_t barBgCol  = s->color565(24, 32, 48);

    s->setFont(nullptr);
    s->setTextSize(1);
    s->setTextWrap(false);

    if (w >= 4 && h >= 16) {
        s->drawFastHLine(0, 0, w, borderCol);
        s->drawFastHLine(0, h - 1, w, borderCol);
    }

    int16_t titleY = (h >= 48) ? static_cast<int16_t>(h / 2 - 12) : 5;
    int16_t subY   = (h >= 48) ? static_cast<int16_t>(h / 2)      : 16;
    drawCenteredClippedLine(s, line1, w, titleY, titleCol);
    drawCenteredClippedLine(s, line2, w, subY, subCol);

    if (w >= 32 && h >= 28) {
        int16_t barW = std::min<int16_t>(48, static_cast<int16_t>(w - 12));
        int16_t barX = static_cast<int16_t>((w - barW) / 2);
        int16_t barY = (h >= 48) ? static_cast<int16_t>(h / 2 + 12) : static_cast<int16_t>(h - 5);
        s->drawFastHLine(barX, barY, barW, barBgCol);
        int16_t segW = std::max<int16_t>(6, static_cast<int16_t>(barW / 4));
        int16_t segX = static_cast<int16_t>(barX + ((static_cast<int32_t>(barW - segW) * m_animStep) / 7));
        s->drawFastHLine(segX, barY, segW, titleCol);
    }

    m_dirty = false;
    m_renderedNewFrame = true;
}

