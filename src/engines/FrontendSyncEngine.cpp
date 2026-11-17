#include "FrontendSyncEngine.h"
#include "WebUiSyncEngine.h"
#include <ArduinoJson.h>
#include <cstring>
#include <cstdlib>
#include "../core/drawing/IDrawingSurface.h"
#include "../core/drawing/DmdzStreamDecoder.h"
#include "../core/I18n.h"
#include "../core/SDUtils.h"
#include "../core/Logger.h"
#include "../core/Globals.h"
#include "../core/SdLockGuard.h"
#include "../core/NetworkBudget.h"
#include "../core/net/DmdzStreamClient.h"
#include "../core/net/RawMqttSocket.h"
#include "../core/net/SecureHttpClient.h"
#include "../core/net/SecureHttpSession.h"
#include "../core/net/ShowcaseDiscovery.h"
#include "../hal/HardwareHAL.h"
#include <esp_task_wdt.h>
#if defined(ESP32)
#include <esp_heap_caps.h>
#if !defined(UNIT_TEST)
#include <ESPmDNS.h>
#endif
#endif

#if defined(HARDWARE_PROFILE_ESP32_DEV)
#pragma GCC optimize("Os")
#endif

extern ConfigLoader config;

namespace {

void copySafeCStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0U) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0U;
    while (i + 1U < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

bool isLoopbackOrEmptyHost(const char* h) {
    if (!h || h[0] == '\0') return true;
    return (strcmp(h, "127.0.0.1") == 0 ||
            strcasecmp(h, "localhost") == 0 ||
            strcmp(h, "0.0.0.0") == 0 ||
            strcmp(h, "::1") == 0);
}

bool resolveBaseServerUrl(const char* rawUrl, char* outBaseUrl, size_t outCap, char* outResolvedHost, size_t hostCap) {
    uint16_t port = 8890;
    bool online = false;
    return net::ShowcaseDiscovery::resolveUrl(
        rawUrl,
        outBaseUrl,
        outCap,
        outResolvedHost,
        hostCap,
        port,
        online,
        nullptr,
        0,
        true,
        false
    );
}

struct SharedBrokerTargetState {
    std::atomic<uint32_t> seq{0};
    char host[96] = "";
    uint16_t port = 1883;
    bool fromShowcase = false;
    bool probedOnce = false;
};

SharedBrokerTargetState s_brokerState;

void writeSharedBrokerState(bool fromShowcase, const char* host, uint16_t port, bool probedOnce) {
    s_brokerState.seq.fetch_add(1, std::memory_order_acq_rel);
    copySafeCStr(s_brokerState.host, sizeof(s_brokerState.host), host);
    s_brokerState.port = (port > 0) ? port : 1883;
    s_brokerState.fromShowcase = fromShowcase;
    s_brokerState.probedOnce = probedOnce;
    s_brokerState.seq.fetch_add(1, std::memory_order_release);
}

void readSharedBrokerState(bool& outFromShowcase, char* outHost, size_t hostCap, uint16_t& outPort, bool& outProbedOnce) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t s1 = s_brokerState.seq.load(std::memory_order_acquire);
        if ((s1 & 1U) != 0U) continue;
        bool fs = s_brokerState.fromShowcase;
        uint16_t p = s_brokerState.port;
        bool po = s_brokerState.probedOnce;
        char localH[96];
        copySafeCStr(localH, sizeof(localH), s_brokerState.host);
        uint32_t s2 = s_brokerState.seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            outFromShowcase = fs;
            copySafeCStr(outHost, hostCap, localH);
            outPort = p;
            outProbedOnce = po;
            return;
        }
    }
    outFromShowcase = false;
    if (outHost && hostCap > 0) outHost[0] = '\0';
    outPort = 1883;
    outProbedOnce = false;
}

void drawSyncText(IDrawingSurface* s, int16_t x, int16_t y, const char* text, uint16_t color, int16_t maxWidthPx) {
    if (!s || !text || text[0] == '\0' || maxWidthPx <= 0) return;
    const int maxChars = maxWidthPx / 6;
    if (maxChars <= 0) return;
    s->setTextSize(1);
    s->setTextColor(color);
    for (int i = 0; i < maxChars && text[i] != '\0'; ++i) {
        s->setCursor(static_cast<int16_t>(x + i * 6), y);
        char ch[2] = {text[i], '\0'};
        s->print(ch);
    }
}

void drawSyncScrollingText(
    IDrawingSurface* s,
    int16_t x,
    int16_t y,
    int16_t widthPx,
    const char* text,
    uint16_t color,
    uint32_t elapsedMs
) {
    if (!s || !text || text[0] == '\0' || widthPx <= 0) return;
    const int textLen = static_cast<int>(strlen(text));
    const int textPx = textLen * 6;
    if (textPx <= widthPx) {
        drawSyncText(s, x, y, text, color, widthPx);
        return;
    }
    const int scrollSpan = textPx - widthPx + 18;
    const int rawStep = static_cast<int>((elapsedMs / 45U) % static_cast<uint32_t>(scrollSpan + 24));
    const int offsetPx = (rawStep > 12) ? std::min(scrollSpan, rawStep - 12) : 0;
    const int firstChar = offsetPx / 6;
    const int maxChars = (widthPx / 6) + 1;
    s->setTextSize(1);
    s->setTextColor(color);
    for (int i = 0; i < maxChars && (firstChar + i) < textLen; ++i) {
        int16_t cx = static_cast<int16_t>(x + i * 6);
        if (cx + 5 <= x + widthPx) {
            s->setCursor(cx, y);
            char ch[2] = {text[firstChar + i], '\0'};
            s->print(ch);
        }
    }
}

void drawTrophyIcon8x8(IDrawingSurface* s, int16_t x, int16_t y, uint16_t color) {
    if (!s) return;
    s->drawFastHLine(x + 1, y, 6, color);
    s->drawPixel(x, y + 1, color);
    s->drawFastHLine(x + 1, y + 1, 6, 0xFFE0);
    s->drawPixel(x + 7, y + 1, color);
    s->drawFastHLine(x, y + 2, 8, color);
    s->drawFastHLine(x + 1, y + 3, 6, color);
    s->drawFastHLine(x + 2, y + 4, 4, color);
    s->drawFastVLine(x + 3, y + 5, 2, color);
    s->drawFastVLine(x + 4, y + 5, 2, color);
    s->drawFastHLine(x + 1, y + 7, 6, color);
}

uint16_t rankColor565(uint8_t rank) {
    if (rank == 1U) return 0xFFE0; // Gold
    if (rank == 2U) return 0xC618; // Silver
    if (rank == 3U) return 0xFD20; // Bronze
    return 0x07FF;                 // Cyan
}

#if defined(ESP32) && !defined(UNIT_TEST)
bool closeSdFileSafely(FsFile& file, const char* removePath = nullptr, bool flushBeforeClose = false) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        esp_task_wdt_reset();
        SdLockGuard guard(pdMS_TO_TICKS(500));
        if (guard) {
            if (file) {
                if (flushBeforeClose) {
                    file.flush();
                }
                file.close();
            }
            if (removePath && removePath[0] != '\0' && sd.exists(removePath)) {
                sd.remove(removePath);
            }
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}
#endif

} // namespace

// ============================================================================
// SyncCelebrationEngine (Core 1 lock-free 60 FPS celebration presenter)
// ============================================================================

EngineError SyncCelebrationEngine::initialize(EngineContext* context, const EngineConfig* cfg) {
    (void)cfg;
    if (context) {
        m_surface = context->getSurface();
        m_geometry = context->getGeometry();
    }
    if (m_geometry.width == 0) m_geometry.width = 128;
    if (m_geometry.height == 0) m_geometry.height = 32;
    m_fireworks.reset(m_geometry.width, m_geometry.height);
    m_dirty = false;
    m_renderedNewFrame = false;
    return EngineError::OK;
}

void SyncCelebrationEngine::triggerCelebration(const SyncCelebrationPayloadPod& payload) {
    m_seq.fetch_add(1U, std::memory_order_acq_rel);
    m_pendingPayload = payload;
    m_seq.fetch_add(1U, std::memory_order_release);
}

void SyncCelebrationEngine::latchPendingPayload() {
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t s1 = m_seq.load(std::memory_order_acquire);
        if ((s1 & 1U) != 0U) continue;
        SyncCelebrationPayloadPod tmp = m_pendingPayload;
        uint32_t s2 = m_seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            m_activePayload = tmp;
            return;
        }
    }
    m_activePayload = m_pendingPayload;
}

void SyncCelebrationEngine::activate() {
    latchPendingPayload();
    const uint16_t w = (m_surface && m_surface->width() > 0) ? static_cast<uint16_t>(m_surface->width()) : m_geometry.width;
    const uint16_t h = (m_surface && m_surface->height() > 0) ? static_cast<uint16_t>(m_surface->height()) : m_geometry.height;
    m_fireworks.reset(w, h);
    m_lastUpdateMs = millis();
    m_tickCount = 0;
    m_dirty = true;
    m_renderedNewFrame = false;
}

void SyncCelebrationEngine::update(EngineContext* context) {
    if (context && context->getSurface()) {
        m_surface = context->getSurface();
        m_geometry = context->getGeometry();
    }
    latchPendingPayload();
    const uint32_t now = millis();
    if (m_lastUpdateMs == 0U || (now - m_lastUpdateMs) >= 15U) {
        m_lastUpdateMs = (now == 0U) ? 1U : now;
        const uint16_t w = (m_surface && m_surface->width() > 0) ? static_cast<uint16_t>(m_surface->width()) : m_geometry.width;
        const uint16_t h = (m_surface && m_surface->height() > 0) ? static_cast<uint16_t>(m_surface->height()) : m_geometry.height;
        m_fireworks.step(w, h);
        ++m_tickCount;
        m_dirty = true;
    }
}

void SyncCelebrationEngine::render(EngineContext* context) {
    if (!m_dirty) {
        m_renderedNewFrame = false;
        return;
    }
    IDrawingSurface* s = (context && context->getSurface()) ? context->getSurface() : m_surface;
    if (!s) {
        m_renderedNewFrame = false;
        return;
    }

    const int16_t w = static_cast<int16_t>(s->width() > 0 ? s->width() : m_geometry.width);
    const int16_t h = static_cast<int16_t>(s->height() > 0 ? s->height() : m_geometry.height);
    const bool isTate = (h > w);

    // 1. Background Q8.8 fixed-point fireworks
    m_fireworks.render(s);

    const Lang lang = (m_activePayload.lang[0] != '\0')
        ? I18n::parseLang(m_activePayload.lang)
        : I18n::getLang();
    const bool isHighScore = (m_activePayload.kind == SyncCelebrationKind::HighScore);
    const char* bannerTitle = isHighScore
        ? I18n::getSyncNewRecordTitle(lang)
        : I18n::getSyncUnlockedTitle(lang);
    const int8_t bounceY = m_fireworks.currentHeaderBounceOffset();
    const ClockSpriteBank::HeroFamily heroFam = ClockSpriteBank::classifySystemHero(m_activePayload.system);

    if (isTate) {
        // Tate celebration: fireworks sky at top, bouncing header in middle, hero + record/trophy stacked
        const int16_t headerY = static_cast<int16_t>(16 + bounceY / 2);
        drawTrophyIcon8x8(s, 2, headerY, 0xFFE0);
        drawSyncText(s, 12, headerY, bannerTitle, 0xFFE0, w - 13);

        ClockSpriteBank::drawCelebrationHero(s, heroFam, h - 2, static_cast<uint16_t>(w), static_cast<uint16_t>(h), m_tickCount);

        if (isHighScore) {
            char rankLine[16];
            snprintf(rankLine, sizeof(rankLine), "#%u %s", static_cast<unsigned>(m_activePayload.rank), m_activePayload.tag[0] ? m_activePayload.tag : "AAA");
            drawSyncText(s, 2, 28, rankLine, rankColor565(m_activePayload.rank), w - 4);
            drawSyncText(s, 2, 37, m_activePayload.score, 0xFFFF, w - 4);
        } else {
            drawSyncScrollingText(s, 2, 28, w - 4, m_activePayload.title, 0xFFFF, static_cast<uint32_t>(m_tickCount) * 16U);
            char ptsLine[20];
            snprintf(ptsLine, sizeof(ptsLine), "+%u PTS%s", static_cast<unsigned>(m_activePayload.points), m_activePayload.hardcore ? " HC" : "");
            drawSyncText(s, 2, 37, ptsLine, m_activePayload.hardcore ? 0xFFE0 : 0x07FF, w - 4);
        }
    } else {
        // Yoko celebration: bouncing header at top, hero cameo along bottom, record/trophy details
        const int16_t headerY = static_cast<int16_t>(1 + (bounceY < -2 ? -1 : 0));
        drawTrophyIcon8x8(s, 2, headerY, 0xFFE0);
        drawSyncText(s, 12, headerY, bannerTitle, 0xFFE0, w - 14);

        ClockSpriteBank::drawCelebrationHero(s, heroFam, h - 1, static_cast<uint16_t>(w), static_cast<uint16_t>(h), m_tickCount);

        const int16_t textX = (w >= 128) ? 28 : 20;
        const int16_t availW = static_cast<int16_t>(w - textX - 2);
        if (isHighScore) {
            char line1[32];
            if (w >= 128 && m_activePayload.game[0] != '\0') {
                snprintf(line1, sizeof(line1), "#%u %s - %s", static_cast<unsigned>(m_activePayload.rank), m_activePayload.tag[0] ? m_activePayload.tag : "AAA", m_activePayload.game);
            } else {
                snprintf(line1, sizeof(line1), "#%u %s", static_cast<unsigned>(m_activePayload.rank), m_activePayload.tag[0] ? m_activePayload.tag : "AAA");
            }
            drawSyncText(s, textX, 12, line1, rankColor565(m_activePayload.rank), availW);
            drawSyncText(s, textX, 22, m_activePayload.score, 0xFFFF, availW);
        } else {
            drawSyncScrollingText(s, textX, 12, availW, m_activePayload.title, 0xFFFF, static_cast<uint32_t>(m_tickCount) * 16U);
            char ptsLine[28];
            if (m_activePayload.hardcore) {
                snprintf(ptsLine, sizeof(ptsLine), "+%u PTS [HARDCORE]", static_cast<unsigned>(m_activePayload.points));
            } else {
                snprintf(ptsLine, sizeof(ptsLine), "+%u PTS", static_cast<unsigned>(m_activePayload.points));
            }
            drawSyncText(s, textX, 22, ptsLine, m_activePayload.hardcore ? 0xFFE0 : 0x07FF, availW);
        }
    }

    m_dirty = false;
    m_renderedNewFrame = true;
}

void SyncCelebrationEngine::deactivate() {
    m_dirty = false;
    m_renderedNewFrame = false;
}

void SyncCelebrationEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_geometry = geometry;
    m_dirty = true;
}

// ============================================================================
// InGameShowcaseEngine (Core 1 lock-free RGB565 + Sync Cards presenter)
// ============================================================================

void InGameShowcaseEngine::publishSyncMeta(const SyncMetaPayloadPod& meta) {
    m_syncMetaSeq.fetch_add(1U, std::memory_order_acq_rel);
    m_pendingSyncMeta = meta;
    m_syncMetaSeq.fetch_add(1U, std::memory_order_release);
}

void InGameShowcaseEngine::clearSyncMeta() {
    m_activeCardMode.store(static_cast<uint8_t>(SyncCardMode::None), std::memory_order_release);
    m_syncMetaSeq.fetch_add(1U, std::memory_order_acq_rel);
    m_pendingSyncMeta = SyncMetaPayloadPod{};
    m_syncMetaSeq.fetch_add(1U, std::memory_order_release);
}

void InGameShowcaseEngine::setActiveCardMode(SyncCardMode mode) {
    const uint8_t prev = m_activeCardMode.exchange(static_cast<uint8_t>(mode), std::memory_order_acq_rel);
    if (prev != static_cast<uint8_t>(mode)) {
        m_cardActivatedMs = millis();
        m_lastCardFrameMs = 0;
        m_dirty = true;
    }
}

void InGameShowcaseEngine::latchSyncMetaIfNeeded() {
    const uint32_t curSeq = m_syncMetaSeq.load(std::memory_order_acquire);
    if ((curSeq & 1U) != 0U || curSeq == m_latchedSyncMetaSeq) {
        return;
    }
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t s1 = m_syncMetaSeq.load(std::memory_order_acquire);
        if ((s1 & 1U) != 0U) continue;
        SyncMetaPayloadPod tmp = m_pendingSyncMeta;
        uint32_t s2 = m_syncMetaSeq.load(std::memory_order_acquire);
        if (s1 == s2) {
            m_activeSyncMeta = tmp;
            m_latchedSyncMetaSeq = s1;
            m_dirty = true;
            return;
        }
    }
}

void InGameShowcaseEngine::renderHallOfFameCard(IDrawingSurface* s) {
    if (!s) return;
    s->clear(0);
    const int16_t w = static_cast<int16_t>(s->width() > 0 ? s->width() : m_width);
    const int16_t h = static_cast<int16_t>(s->height() > 0 ? s->height() : m_height);
    const bool isTate = (h > w);
    const Lang lang = (m_activeSyncMeta.lang[0] != '\0')
        ? I18n::parseLang(m_activeSyncMeta.lang)
        : I18n::getLang();
    const char* titleStr = I18n::getSyncHighScoresTitle(lang);
    const uint32_t elapsed = millis() - m_cardActivatedMs;

    drawTrophyIcon8x8(s, 2, 0, 0xFFE0);
    drawSyncText(s, 12, 0, titleStr, 0xFFE0, w - 14);
    s->drawFastHLine(0, 8, w, 0x3186);

    const uint8_t count = (m_activeSyncMeta.hiscoreCount <= 5U) ? m_activeSyncMeta.hiscoreCount : 5U;
    if (count == 0U) return;

    if (isTate) {
        int16_t y = 11;
        if (m_activeSyncMeta.title[0] != '\0' || m_activeSyncMeta.game[0] != '\0') {
            const char* gName = m_activeSyncMeta.title[0] ? m_activeSyncMeta.title : m_activeSyncMeta.game;
            drawSyncText(s, 2, y, gName, 0x07FF, w - 4);
            y = static_cast<int16_t>(y + 9);
        }
        const uint8_t maxRows = (h >= 128) ? 5U : 3U;
        for (uint8_t i = 0; i < count && i < maxRows; ++i) {
            const auto& e = m_activeSyncMeta.hiscores[i];
            const uint8_t rk = (e.rank > 0U) ? e.rank : static_cast<uint8_t>(i + 1U);
            char rowBuf[24];
            if (w <= 48) {
                snprintf(rowBuf, sizeof(rowBuf), "#%u %s", static_cast<unsigned>(rk), e.tag[0] ? e.tag : "AAA");
                drawSyncText(s, 1, y, rowBuf, rankColor565(rk), w - 2);
                drawSyncText(s, 4, static_cast<int16_t>(y + 8), e.score, 0xFFFF, w - 5);
                y = static_cast<int16_t>(y + 17);
            } else {
                snprintf(rowBuf, sizeof(rowBuf), "#%u %-3s %s", static_cast<unsigned>(rk), e.tag[0] ? e.tag : "AAA", e.score);
                drawSyncText(s, 2, y, rowBuf, rankColor565(rk), w - 4);
                y = static_cast<int16_t>(y + 10);
            }
        }
    } else if (h >= 64) {
        for (uint8_t i = 0; i < count; ++i) {
            const auto& e = m_activeSyncMeta.hiscores[i];
            const uint8_t rk = (e.rank > 0U) ? e.rank : static_cast<uint8_t>(i + 1U);
            const int16_t rowY = static_cast<int16_t>(11 + i * 10);
            char leftBuf[16];
            snprintf(leftBuf, sizeof(leftBuf), "#%u %-3s", static_cast<unsigned>(rk), e.tag[0] ? e.tag : "AAA");
            drawSyncText(s, 4, rowY, leftBuf, rankColor565(rk), 48);
            drawSyncText(s, 56, rowY, e.score, 0xFFFF, w - 80);
        }
        ClockSpriteBank::drawPacman(s, w - 18, 22, static_cast<uint8_t>((elapsed / 120U) & 0x03U), true);
    } else {
        // 32px Yoko: show 2 rows at a time, paging every 4s if > 2 entries
        const uint8_t pageOffset = (count > 2U && ((elapsed / 4000U) & 1U) != 0U) ? 2U : 0U;
        for (uint8_t slot = 0; slot < 2U && (pageOffset + slot) < count; ++slot) {
            const auto& e = m_activeSyncMeta.hiscores[pageOffset + slot];
            const uint8_t rk = (e.rank > 0U) ? e.rank : static_cast<uint8_t>(pageOffset + slot + 1U);
            const int16_t rowY = static_cast<int16_t>(11 + slot * 10);
            char rowBuf[32];
            snprintf(rowBuf, sizeof(rowBuf), "#%u %-3s %s", static_cast<unsigned>(rk), e.tag[0] ? e.tag : "AAA", e.score);
            drawSyncText(s, 2, rowY, rowBuf, (slot == 0U && pageOffset == 0U) ? 0xFFE0 : rankColor565(rk), w - 4);
        }
    }
}

void InGameShowcaseEngine::renderRetroAchievementsCard(IDrawingSurface* s) {
    if (!s) return;
    s->clear(0);
    const int16_t w = static_cast<int16_t>(s->width() > 0 ? s->width() : m_width);
    const int16_t h = static_cast<int16_t>(s->height() > 0 ? s->height() : m_height);
    const bool isTate = (h > w);
    const Lang lang = (m_activeSyncMeta.lang[0] != '\0')
        ? I18n::parseLang(m_activeSyncMeta.lang)
        : I18n::getLang();
    const char* titleStr = I18n::getSyncAchievementTitle(lang);
    const uint32_t elapsed = millis() - m_cardActivatedMs;

    drawTrophyIcon8x8(s, 2, 0, m_activeSyncMeta.ra.hardcore ? 0xFFE0 : 0x07FF);
    drawSyncText(s, 12, 0, titleStr, m_activeSyncMeta.ra.hardcore ? 0xFFE0 : 0x07FF, w - 14);
    s->drawFastHLine(0, 8, w, 0x3186);

    const uint16_t unlocked = m_activeSyncMeta.ra.unlocked;
    const uint16_t total = (m_activeSyncMeta.ra.total > 0U) ? m_activeSyncMeta.ra.total : 1U;
    char statBuf[28];
    snprintf(
        statBuf,
        sizeof(statBuf),
        "%u/%u %uP%s",
        static_cast<unsigned>(unlocked),
        static_cast<unsigned>(m_activeSyncMeta.ra.total),
        static_cast<unsigned>(m_activeSyncMeta.ra.points),
        m_activeSyncMeta.ra.hardcore ? " HC" : ""
    );

    if (isTate) {
        drawSyncText(s, 2, 12, statBuf, 0xFFFF, w - 4);
        const int16_t barW = static_cast<int16_t>(w - 4);
        s->drawRect(2, 22, barW, 5, 0x4208);
        const int16_t fillW = static_cast<int16_t>((static_cast<uint32_t>(unlocked) * static_cast<uint32_t>(barW - 2)) / total);
        if (fillW > 0) {
            s->fillRect(3, 23, std::min<int16_t>(fillW, barW - 2), 3, 0x07E0);
        }
        if (m_activeSyncMeta.ra.recentTitle[0] != '\0') {
            drawSyncScrollingText(s, 2, 32, w - 4, m_activeSyncMeta.ra.recentTitle, 0xFFE0, elapsed);
        }
        ClockSpriteBank::drawPacman(
            s,
            static_cast<int16_t>((w - 13) / 2),
            45,
            static_cast<uint8_t>((elapsed / 120U) & 0x03U),
            true
        );
    } else {
        drawSyncText(s, 2, 11, statBuf, 0xFFFF, w - 4);
        if (w >= 128) {
            const int16_t barX = 82;
            const int16_t barW = static_cast<int16_t>(w - barX - 4);
            if (barW >= 16) {
                s->drawRect(barX, 12, barW, 5, 0x4208);
                const int16_t fillW = static_cast<int16_t>((static_cast<uint32_t>(unlocked) * static_cast<uint32_t>(barW - 2)) / total);
                if (fillW > 0) {
                    s->fillRect(barX + 1, 13, std::min<int16_t>(fillW, barW - 2), 3, 0x07E0);
                }
            }
        }
        if (m_activeSyncMeta.ra.recentTitle[0] != '\0') {
            drawSyncScrollingText(s, 2, 22, w - 4, m_activeSyncMeta.ra.recentTitle, 0xFFE0, elapsed);
        } else if (m_activeSyncMeta.title[0] != '\0') {
            drawSyncText(s, 2, 22, m_activeSyncMeta.title, 0x07FF, w - 4);
        }
    }
}

EngineError InGameShowcaseEngine::initialize(EngineContext* context, const EngineConfig* cfg) {
    (void)cfg;
    if (!context || !context->getSurface()) {
        return EngineError::InitializationFailed;
    }
    m_surface = context->getSurface();
    DisplayGeometry geom = context->getGeometry();
    m_width = geom.width > 0 ? geom.width : 128;
    m_height = geom.height > 0 ? geom.height : 32;
    (void)m_streamPlayer.probeZeroCopyOnCore1(m_surface, m_width, m_height);
    m_dirty = false;
    m_renderedNewFrame = false;
    return EngineError::OK;
}

void InGameShowcaseEngine::activate() {
    (void)m_streamPlayer.refreshZeroCopyFromSurfaceReadOnly(m_surface, m_width, m_height);
    m_displayEngaged.store(true, std::memory_order_release);
    m_lastRenderedSeq = 0;
    m_dirty = true;
    m_renderedNewFrame = false;
}

uint32_t InGameShowcaseEngine::nextFrameDueInMs() const {
    if (!m_displayEngaged.load(std::memory_order_acquire)) {
        return 0xFFFFFFFFU;
    }
    if (m_streamPlayer.isCore1Streaming()) {
        return m_streamPlayer.computeCore1NextFrameDueInMs(millis());
    }
    if (m_activeFramePtr.load(std::memory_order_acquire) != nullptr) {
        const uint32_t fSeq = m_frameSeq.load(std::memory_order_acquire);
        if ((fSeq & 1U) == 0U && fSeq != m_lastRenderedSeq) {
            return 1U;
        }
        return engines::showcase::ShowcaseStreamPlayer::computeSelfPacedWakeupMs(
            m_nextStreamFrameDueMs.load(std::memory_order_relaxed),
            millis()
        );
    }
    if (activeCardMode() != SyncCardMode::None) {
        return 33U;
    }
    return 0xFFFFFFFFU;
}

void InGameShowcaseEngine::update(EngineContext* context) {
    if (context && context->getSurface()) {
        m_surface = context->getSurface();
    }
    latchSyncMetaIfNeeded();
    if (m_streamPlayer.isCore1Streaming()) {
        if (m_streamPlayer.shouldMarkCore1Dirty(millis())) {
            m_dirty = true;
        }
        return;
    }
    uint32_t seq = m_frameSeq.load(std::memory_order_acquire);
    if ((seq & 1U) == 0U && seq != m_lastRenderedSeq && m_activeFramePtr.load(std::memory_order_acquire) != nullptr) {
        m_dirty = true;
        return;
    }
    if (activeCardMode() != SyncCardMode::None) {
        const uint32_t now = millis();
        if (m_lastCardFrameMs == 0U || (now - m_lastCardFrameMs) >= 33U) {
            m_lastCardFrameMs = (now == 0U) ? 1U : now;
            m_dirty = true;
        }
    }
}

void InGameShowcaseEngine::waitRenderBlitQuiescent() const {
    std::atomic_thread_fence(std::memory_order_seq_cst);
#if defined(ESP32) && !defined(UNIT_TEST)
    uint32_t t0 = millis();
    while ((m_renderBlitSeq.load(std::memory_order_seq_cst) & 1U) != 0U &&
           (millis() - t0) < 50U) {
        vTaskDelay(1);
    }
#endif
}

void InGameShowcaseEngine::render(EngineContext* context) {
    if (!m_dirty) {
        m_renderedNewFrame = false;
        return;
    }
    IDrawingSurface* s = (context && context->getSurface()) ? context->getSurface() : m_surface;
    if (!s) {
        m_renderedNewFrame = false;
        return;
    }

    struct BlitSeqGuard {
        std::atomic<uint32_t>& seq;
        explicit BlitSeqGuard(std::atomic<uint32_t>& sRef) : seq(sRef) {
            seq.fetch_add(1U, std::memory_order_seq_cst);
        }
        ~BlitSeqGuard() {
            seq.fetch_add(1U, std::memory_order_release);
        }
    };

    if (m_streamPlayer.isCore1Streaming()) {
        BlitSeqGuard blitGuard{m_renderBlitSeq};
        const bool drew = m_streamPlayer.renderFrameCore1(s, m_width, m_height);
        m_dirty = false;
        m_renderedNewFrame = drew;
        return;
    }

    const uint32_t seq1 = m_frameSeq.load(std::memory_order_acquire);
    if ((seq1 & 1U) != 0U) {
        m_dirty = false;
        m_renderedNewFrame = false;
        return;
    }

    BlitSeqGuard blitGuard{m_renderBlitSeq};

    const uint32_t seq2 = m_frameSeq.load(std::memory_order_acquire);
    if ((seq2 & 1U) != 0U) {
        m_dirty = false;
        m_renderedNewFrame = false;
        return;
    }
    const uint16_t* ptr = m_activeFramePtr.load(std::memory_order_acquire);
    uint16_t fw = m_frameW.load(std::memory_order_relaxed);
    uint16_t fh = m_frameH.load(std::memory_order_relaxed);

    if (ptr && fw > 0 && fh > 0) {
        if (s->width() != static_cast<int>(fw) || s->height() != static_cast<int>(fh)) {
            s->clear(0);
        }
        s->blit565(ptr, 0, 0, fw, fh);
        m_lastRenderedSeq = seq2;
        m_consumedFrameSeq.store(seq2, std::memory_order_release);
        m_presentedFrames.fetch_add(1U, std::memory_order_relaxed);
        m_dirty = false;
        m_renderedNewFrame = true;
        return;
    }

    const SyncCardMode cardMode = activeCardMode();
    if (cardMode == SyncCardMode::HallOfFame) {
        renderHallOfFameCard(s);
        m_dirty = false;
        m_renderedNewFrame = true;
        return;
    }
    if (cardMode == SyncCardMode::RetroAchievements) {
        renderRetroAchievementsCard(s);
        m_dirty = false;
        m_renderedNewFrame = true;
        return;
    }

    m_dirty = false;
    m_renderedNewFrame = false;
}

void InGameShowcaseEngine::deactivate() {
    // Invariant 15: 100% allocation-free and wait-free on Core 1
    m_displayEngaged.store(false, std::memory_order_release);
    m_nextStreamFrameDueMs.store(0U, std::memory_order_relaxed);
    m_dirty = false;
    m_renderedNewFrame = false;
}

void InGameShowcaseEngine::pause() {
    m_displayEngaged.store(false, std::memory_order_release);
    m_renderedNewFrame = false;
}

void InGameShowcaseEngine::resume() {
    (void)m_streamPlayer.refreshZeroCopyFromSurfaceReadOnly(m_surface, m_width, m_height);
    m_displayEngaged.store(true, std::memory_order_release);
    m_lastRenderedSeq = 0;
    m_dirty = true;
    m_renderedNewFrame = false;
}

void InGameShowcaseEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_width = geometry.width > 0 ? geometry.width : 128;
    m_height = geometry.height > 0 ? geometry.height : 32;
    m_streamPlayer.onGeometryChanged(geometry.rotation);
    (void)m_streamPlayer.refreshZeroCopyFromSurfaceReadOnly(m_surface, m_width, m_height);
    m_lastRenderedSeq = 0;
    m_dirty = true;
}

// ============================================================================
// FrontendSyncEngine
// ============================================================================

FrontendSyncEngine* FrontendSyncEngine::instance = nullptr;

FrontendSyncEngine::FrontendSyncEngine(
    const MqttConfig& cfg,
    GifEngine* gifEngine,
    MessageEngine* messageEngine,
    InGameShowcaseEngine* inGameEngine,
    SyncCelebrationEngine* celebrationEngine
)
    : mqttConfig(cfg),
      gif(gifEngine),
      message(messageEngine),
      m_inGameEngine(inGameEngine),
      m_celebrationEngine(celebrationEngine),
      mqttClient(espClient) {
    instance = this;
    lastReconnectAttempt = 0;
    syncWorkerConfigSlot();
}

FrontendSyncEngine::~FrontendSyncEngine() {
    stop();
    if (instance == this) {
        instance = nullptr;
    }
}

void FrontendSyncEngine::syncWorkerConfigSlot() {
    m_cfgSeq.fetch_add(1, std::memory_order_acq_rel);
    copySafeCStr(m_workerCfg.showcaseUrl, sizeof(m_workerCfg.showcaseUrl), showcaseConfig.server_url.c_str());
    copySafeCStr(m_workerCfg.localBroker, sizeof(m_workerCfg.localBroker), mqttConfig.broker.c_str());
    copySafeCStr(m_workerCfg.lang, sizeof(m_workerCfg.lang), m_systemLang.length() > 0 ? m_systemLang.c_str() : "en");
    copySafeCStr(m_workerCfg.cycleMode, sizeof(m_workerCfg.cycleMode), mqttConfig.ingame_cycle_mode.c_str());
    copySafeCStr(m_workerCfg.cardStyleSource, sizeof(m_workerCfg.cardStyleSource), mqttConfig.card_style_source.c_str());
    copySafeCStr(m_workerCfg.cardLayout, sizeof(m_workerCfg.cardLayout), mqttConfig.card_layout.c_str());
    copySafeCStr(m_workerCfg.cardTheme, sizeof(m_workerCfg.cardTheme), mqttConfig.card_theme.c_str());
    copySafeCStr(m_workerCfg.textScrollSpeed, sizeof(m_workerCfg.textScrollSpeed), mqttConfig.text_scroll_speed.c_str());
    copySafeCStr(m_workerCfg.videoMode, sizeof(m_workerCfg.videoMode), mqttConfig.video_mode.c_str());
    copySafeCStr(m_workerCfg.colorBoost, sizeof(m_workerCfg.colorBoost), mqttConfig.color_boost.c_str());
    m_workerCfg.localPort = (mqttConfig.port > 0 && mqttConfig.port <= 65535) ? static_cast<uint16_t>(mqttConfig.port) : 1883;
    m_workerCfg.panelW = (m_panelW >= 32) ? m_panelW : 128;
    m_workerCfg.panelH = (m_panelH >= 16) ? m_panelH : 32;
    m_workerCfg.marqueeIntervalSec = static_cast<uint16_t>(constrain(mqttConfig.ingame_marquee_interval_sec, 5, 600));
    m_workerCfg.snapshotPanoramaCount = static_cast<uint8_t>(constrain(mqttConfig.snapshot_panorama_count, 0, 5));
    m_workerCfg.videoMaxDurationS = static_cast<uint8_t>(constrain(mqttConfig.video_max_duration_s, 2, 120));
    m_workerCfg.videoFps = static_cast<uint8_t>(constrain(mqttConfig.video_fps, 8, 60));
    m_workerCfg.mqttEnabled = mqttConfig.enabled;
    m_workerCfg.showcaseEnabled = showcaseConfig.enabled;
    m_workerCfg.useShowcaseBroker = mqttConfig.use_showcase_broker;
    m_workerCfg.ingameShowcaseEnabled = mqttConfig.ingame_showcase_enabled;
    m_workerCfg.includeCardIntro = mqttConfig.include_card_intro;
    m_workerCfg.showPoster = mqttConfig.show_poster;
    m_workerCfg.showSynopsis = mqttConfig.show_synopsis;
    m_workerCfg.jacketPanorama = mqttConfig.jacket_panorama;
    m_workerCfg.marqueePanorama = mqttConfig.marquee_panorama;
    m_workerCfg.showVideo = mqttConfig.show_video;
    m_cfgSeq.fetch_add(1, std::memory_order_release);
    m_cfgGen.fetch_add(1, std::memory_order_acq_rel);
}

void FrontendSyncEngine::updateRuntimeConfig(
    const MqttConfig& mqttCfg,
    const ShowcaseConfig& showcaseCfg,
    const String& systemLang,
    uint16_t panelW,
    uint16_t panelH
) {
    const bool brokerCfgChanged =
        (mqttCfg.enabled != mqttConfig.enabled) ||
        (mqttCfg.use_showcase_broker != mqttConfig.use_showcase_broker) ||
        (showcaseCfg.enabled != showcaseConfig.enabled) ||
        (showcaseCfg.server_url != showcaseConfig.server_url);

    mqttConfig = mqttCfg;
    showcaseConfig = showcaseCfg;
    m_systemLang = systemLang.length() > 0 ? systemLang : "en";
    m_panelW = panelW > 0 ? panelW : 128;
    m_panelH = panelH > 0 ? panelH : 32;
    syncWorkerConfigSlot();

    if (brokerCfgChanged && mqttConfig.enabled && showcaseConfig.enabled && mqttConfig.use_showcase_broker) {
        m_workerWakeLatch.store(true, std::memory_order_release);
        ensureShowcaseWorkerRunning();
    }

    if (!showcaseConfig.enabled || !mqttConfig.ingame_showcase_enabled) {
        cancelInGameShowcaseSession();
    }
    if (!mqttConfig.sync_show_hiscores && !mqttConfig.sync_show_ra) {
        resetSyncCardCycle();
    } else if (m_hasSessionSyncMeta && m_inGameEngine) {
        m_sessionSyncMeta.showHiscores = mqttConfig.sync_show_hiscores ? 1U : 0U;
        m_sessionSyncMeta.showRa = mqttConfig.sync_show_ra ? 1U : 0U;
        m_sessionSyncMeta.cardDurationSec = static_cast<uint8_t>(constrain(mqttConfig.sync_card_duration_sec, 4, 20));
        copySafeCStr(m_sessionSyncMeta.lang, sizeof(m_sessionSyncMeta.lang), m_systemLang.c_str());
        m_inGameEngine->publishSyncMeta(m_sessionSyncMeta);
    }
}

void FrontendSyncEngine::getResolvedBrokerTarget(
    String& outHost,
    uint16_t& outPort,
    String& outSource,
    bool& outShowcaseActive
) {
    bool fromShowcase = false;
    char brokerHost[96] = "";
    uint16_t brokerPort = 1883;
    bool probedOnce = false;
    readSharedBrokerState(fromShowcase, brokerHost, sizeof(brokerHost), brokerPort, probedOnce);

    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const auto& snap = guard.get();

    if (fromShowcase && snap.showcase.enabled && snap.mqtt.use_showcase_broker && brokerHost[0] != '\0') {
        outHost = String(brokerHost);
        outPort = brokerPort > 0 ? brokerPort : 1883;
        outSource = "showcase";
        outShowcaseActive = true;
        return;
    }

    uint16_t cfgPort = (snap.mqtt.port > 0 && snap.mqtt.port <= 65535)
        ? static_cast<uint16_t>(snap.mqtt.port)
        : 1883;
    if (!isLoopbackOrEmptyHost(snap.mqtt.broker.c_str())) {
        outHost = snap.mqtt.broker;
        outPort = cfgPort;
        outSource = "external";
        outShowcaseActive = false;
        return;
    }

    outHost = "";
    outPort = cfgPort;
    outSource = "local";
    outShowcaseActive = false;
}

bool FrontendSyncEngine::parseShowcaseHealthBroker(
    const char* healthJson,
    const char* fallbackHost,
    String& outHost,
    uint16_t& outPort
) {
    if (!healthJson || healthJson[0] == '\0') return false;
    StaticJsonDocument<1024> filter;
    filter["status"] = true;
    filter["mqtt_mode"] = true;
    filter["mqtt_host"] = true;
    filter["mqtt_port"] = true;
    filter["mqtt"]["mode"] = true;
    filter["mqtt"]["broker_host"] = true;
    filter["mqtt"]["broker_port"] = true;

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, healthJson, DeserializationOption::Filter(filter));
    if (err) return false;

    const char* status = doc["status"] | "";
    if (strcmp(status, "ok") != 0 && !doc.containsKey("mqtt_port") && !doc.containsKey("mqtt")) {
        return false;
    }

    const char* rawHost = doc["mqtt_host"] | "";
    if (rawHost[0] == '\0' && doc["mqtt"].is<JsonObjectConst>()) {
        rawHost = doc["mqtt"]["broker_host"] | "";
    }

    int rawPort = doc["mqtt_port"] | 0;
    if (rawPort <= 0 && doc["mqtt"].is<JsonObjectConst>()) {
        rawPort = doc["mqtt"]["broker_port"] | 0;
    }
    outPort = (rawPort > 0 && rawPort <= 65535) ? static_cast<uint16_t>(rawPort) : 1883;

    char hostBuf[96] = "";
    if (!isLoopbackOrEmptyHost(rawHost)) {
        copySafeCStr(hostBuf, sizeof(hostBuf), rawHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(hostBuf, sizeof(hostBuf), 8890)) {
            return false;
        }
    } else if (fallbackHost && !isLoopbackOrEmptyHost(fallbackHost)) {
        copySafeCStr(hostBuf, sizeof(hostBuf), fallbackHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(hostBuf, sizeof(hostBuf), 8890)) {
            return false;
        }
    } else {
        return false;
    }

    outHost = String(hostBuf);
    return outHost.length() > 0;
}

String FrontendSyncEngine::buildShowcaseQueueRequestJson(
    const MqttConfig& mqttCfg,
    const String& systemId,
    const String& gameQuery,
    uint16_t width,
    uint16_t height,
    const String& lang,
    const String& fallbackQuery
) {
    StaticJsonDocument<1024> doc;
    doc["domain"] = "games";
    JsonArray sysArr = doc.createNestedArray("systems");
    String cleanSys = systemId;
    cleanSys.trim();
    if (cleanSys.length() > 0) {
        sysArr.add(cleanSys);
    }
    String trimmedGame = gameQuery;
    trimmedGame.trim();
    doc["query"] = trimmedGame;
    String trimmedFallback = fallbackQuery;
    trimmedFallback.trim();
    if (trimmedFallback.length() > 0 && !trimmedFallback.equalsIgnoreCase(trimmedGame)) {
        doc["fallback_query"] = trimmedFallback;
    }
    doc["mode"] = "play_now";
    doc["mark_seen"] = false;
    doc["async_prepare"] = true;
    doc["render_card_in_stream"] = true;
    doc["width"] = (width >= 32) ? width : 128;
    doc["height"] = (height >= 16) ? height : 32;
    doc["lang"] = (lang.length() > 0) ? lang : "en";

    if (mqttCfg.card_style_source.equalsIgnoreCase("custom")) {
        doc["include_card_intro"] = mqttCfg.include_card_intro;
        doc["card_layout"] = mqttCfg.card_layout.length() > 0 ? mqttCfg.card_layout : "classic_split";
        doc["card_theme"] = mqttCfg.card_theme.length() > 0 ? mqttCfg.card_theme : "tinted";
        doc["include_poster"] = mqttCfg.show_poster;
        doc["include_synopsis"] = mqttCfg.show_synopsis;
        doc["text_scroll_speed"] = mqttCfg.text_scroll_speed.length() > 0 ? mqttCfg.text_scroll_speed : "4";
        doc["jacket_panorama"] = mqttCfg.jacket_panorama;
        doc["marquee_panorama"] = mqttCfg.marquee_panorama;
        doc["snapshot_panorama_count"] = constrain(mqttCfg.snapshot_panorama_count, 0, 5);
        doc["include_video"] = mqttCfg.show_video;
        doc["video_max_duration_s"] = constrain(mqttCfg.video_max_duration_s, 2, 120);
        doc["video_fps"] = constrain(mqttCfg.video_fps, 8, 60);
        doc["video_mode"] = mqttCfg.video_mode.length() > 0 ? mqttCfg.video_mode : "auto_action";
        doc["color_boost"] = mqttCfg.color_boost.length() > 0 ? mqttCfg.color_boost : "auto";
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void FrontendSyncEngine::stopReconnectTask() {
    stopReconnectRequested.store(true, std::memory_order_release);
    if (reconnectTaskHandle || !reconnectTaskExited.load(std::memory_order_acquire)) {
        espClient.stop();
        for (int i = 0; i < 120 && !reconnectTaskExited.load(std::memory_order_acquire); ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        reconnectTaskHandle = nullptr;
    }
    isReconnecting.store(false, std::memory_order_release);
}

namespace {
// Hardened PicoMQTT::Server subclass (0 bytes extra RAM):
// 1. Enforces MQTT v3.1.1 §3.1.4 session takeover: when a console daemon reconnects with the same
//    ClientId after a half-open Wi-Fi drop or reboot, immediately evicts its stale socket via
//    `clients.erase()` (~NetworkClientSocketHandle -> ::close(fd) in O(1), avoiding `it->disconnect()`
//    which would attempt a blocking NetworkClient::write() of a server-side DISCONNECT packet).
// 2. Caps concurrent embedded clients to 4 (PicoMQTT v0.3.8's Client copy-ctor resets keep_alive_millis
//    to 0, which could otherwise retain half-open sockets indefinitely and exhaust ESP32 LwIP sockets).
class EmbeddedMqttBroker final : public PicoMQTT::Server {
public:
    using PicoMQTT::Server::Server;
    static constexpr size_t MAX_EMBEDDED_CLIENTS = 4;

protected:
    void on_connected(const char* clientId) override {
        if (clients.size() > 1) {
            auto newest = std::prev(clients.end());
            if (clientId && clientId[0] != '\0' && strcmp(clientId, "<unknown>") != 0) {
                for (auto it = clients.begin(); it != newest;) {
                    if (strcmp(it->get_client_id(), clientId) == 0) {
                        LOGW("RetroFrontend", "Evicting stale MQTT session for ClientId '%s'", clientId);
                        it = clients.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            while (clients.size() > MAX_EMBEDDED_CLIENTS) {
                auto oldest = clients.begin();
                if (oldest == newest) {
                    break;
                }
                LOGW("RetroFrontend", "Evicting oldest MQTT client '%s' (cap=%u)",
                     oldest->get_client_id(), (unsigned)MAX_EMBEDDED_CLIENTS);
                clients.erase(oldest);
            }
        }
        LOGI("RetroFrontend", "Embedded MQTT client connected: id='%s' (active=%u)",
             clientId ? clientId : "", (unsigned)clients.size());
    }

    void on_disconnected(const char* clientId) override {
        LOGI("RetroFrontend", "Embedded MQTT client disconnected: id='%s'", clientId ? clientId : "");
    }
};
} // namespace

void FrontendSyncEngine::applyBrokerTransport(bool fromShowcase, const char* host, uint16_t port) {
    stopReconnectTask();
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    espClient.stop();

    m_transportConfigured = true;
    m_activeFromShowcase = fromShowcase;
    m_wasExternalConnected = false;
    copySafeCStr(m_activeBrokerHost, sizeof(m_activeBrokerHost), host);
    m_activeBrokerPort = (port > 0) ? port : 1883;
    lastReconnectAttempt = 0;

    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    const bool localCfgBroker = isLoopbackOrEmptyHost(mqttConfig.broker.c_str());
    const uint16_t rxBufBytes = hasPsram ? 1024U : 768U;

    if (fromShowcase && m_activeBrokerHost[0] != '\0') {
        if (internalBroker) {
            delete internalBroker;
            internalBroker = nullptr;
            LOGI("RetroFrontend", "Stopped embedded PicoMQTT broker (using Showcase MQTT broker %s:%u)",
                 m_activeBrokerHost, m_activeBrokerPort);
        } else {
            LOGI("RetroFrontend", "Using Showcase MQTT broker at %s:%u",
                 m_activeBrokerHost, m_activeBrokerPort);
        }
        // Note: WiFiClient::setTimeout() in Arduino-ESP32 3.x is in MILLISECONDS (Stream::setTimeout),
        // whereas PubSubClient::setSocketTimeout() / setKeepAlive() are in SECONDS.
        espClient.setTimeout(1500);
        mqttClient.setClient(espClient);
        mqttClient.setBufferSize(rxBufBytes);
        mqttClient.setKeepAlive(45);
        mqttClient.setSocketTimeout(2);
        mqttClient.setServer(m_activeBrokerHost, m_activeBrokerPort);
        mqttClient.setCallback(FrontendSyncEngine::callback);
        return;
    }

    if (localCfgBroker) {
        if (!internalBroker) {
            LOGI("RetroFrontend", "Starting embedded PicoMQTT Broker on port %d...", mqttConfig.port);
            internalBroker = new EmbeddedMqttBroker(mqttConfig.port, 5, 1);
            internalBroker->subscribe(MQTT_TOPIC_WILDCARD, [this](const char* topic, const char* payload) {
                this->handleMessage(String(topic), String(payload));
            });
            internalBroker->begin();
            LOGI("RetroFrontend", "Embedded MQTT Broker started successfully.");
        }
    } else {
        if (internalBroker) {
            delete internalBroker;
            internalBroker = nullptr;
        }
        LOGI("RetroFrontend", "Configuring external MQTT Client connecting to %s:%u", m_activeBrokerHost, m_activeBrokerPort);
        espClient.setTimeout(1500);
        mqttClient.setClient(espClient);
        mqttClient.setBufferSize(rxBufBytes);
        mqttClient.setKeepAlive(45);
        mqttClient.setSocketTimeout(2);
        mqttClient.setServer(m_activeBrokerHost, m_activeBrokerPort);
        mqttClient.setCallback(FrontendSyncEngine::callback);
    }
}

void FrontendSyncEngine::ensureShowcaseWorkerRunning() {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (m_showcaseWorkerHandle != nullptr || !m_workerExited.load(std::memory_order_acquire)) {
        return;
    }
    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    // Live HTTPS Pixelcade downloads run only on PSRAM boards (16 KB stack); non-PSRAM esp32dev stays strictly at 6 KB (SD-only).
    const uint32_t stackBytes = hasPsram ? 16384U : 6144U;
    m_workerStopRequested.store(false, std::memory_order_release);
    m_workerExited.store(false, std::memory_order_release);
    if (xTaskCreatePinnedToCore(showcaseWorkerTaskFunc, "SyncShowcaseWkr", stackBytes, this, 2, &m_showcaseWorkerHandle, 0) != pdPASS) {
        m_showcaseWorkerHandle = nullptr;
        m_workerExited.store(true, std::memory_order_release);
    }
#endif
}

void FrontendSyncEngine::begin() {
    stop();
    if (!mqttConfig.enabled) return;

    hasReceivedAnyEvent = false;
    hasPendingEvent = false;
    m_hasPendingNetArt = false;
    m_pendingNetArtDueMs = 0;
    m_pendingNetArtJob = ArtDownloadJobSlot{};
    isGamePlaying = false;
    waitingDisplayed = false;
    m_transportConfigured = false;
    m_activeFromShowcase = false;
    m_wasExternalConnected = false;
    m_activeBrokerHost[0] = '\0';
    m_activeMarqueePath[0] = '\0';
    m_wasShowingInGame = false;
    m_remoteSyncDispatched = false;
    lastReconnectAttempt = 0;
    m_lastBrokerCacheCheckMs = 0;
    m_beginMs = millis();

    m_activeArtReqId.store(0U, std::memory_order_release);
    m_artNetBusy.store(false, std::memory_order_release);
    m_artResultPending.store(false, std::memory_order_release);
    m_artJobSeq.fetch_add(1U, std::memory_order_acq_rel);
    m_artJobSlot = ArtDownloadJobSlot{};
    m_artJobSeq.fetch_add(1U, std::memory_order_release);

    {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const auto& snap = guard.get();
        showcaseConfig = snap.showcase;
        m_systemLang = snap.system.lang.length() > 0 ? snap.system.lang : "en";
        if (m_inGameEngine) {
            m_panelW = m_inGameEngine->surfaceWidth();
            m_panelH = m_inGameEngine->surfaceHeight();
        }
    }
    syncWorkerConfigSlot();
    writeSharedBrokerState(false, mqttConfig.broker.c_str(), static_cast<uint16_t>(mqttConfig.port), false);

    // Core 0 worker exclusively loads and owns `systemMappings` lazily on first system event (zero cross-core String race).
    systemMappings.clear();
    m_mappingsLoaded = false;

    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    const bool wantShowcaseBroker = (showcaseConfig.enabled && mqttConfig.use_showcase_broker);
    bool adoptedCachedBroker = false;
    if (wantShowcaseBroker && showcaseConfig.server_url.length() >= 8) {
        char cachedUrl[112] = "";
        char cachedHost[64] = "";
        uint16_t cachedPort = 8890;
        bool cachedOnline = false;
        if (net::ShowcaseDiscovery::peekCachedUrlStatus(
                showcaseConfig.server_url.c_str(),
                cachedUrl,
                sizeof(cachedUrl),
                cachedHost,
                sizeof(cachedHost),
                cachedPort,
                cachedOnline,
                nullptr,
                0) &&
            cachedOnline && !net::ShowcaseDiscovery::isAutoDiscoverableHost(cachedHost)) {
            writeSharedBrokerState(true, cachedHost, 1883, true);
            applyBrokerTransport(true, cachedHost, 1883);
            adoptedCachedBroker = true;
        }
    }

    if (hasPsram || (wantShowcaseBroker && !adoptedCachedBroker)) {
        m_workerWakeLatch.store(true, std::memory_order_release);
        ensureShowcaseWorkerRunning();
    }

    // Only start local PicoMQTT / static external broker immediately when Showcase broker sharing is disabled.
    // When Showcase broker sharing is enabled, wait up to 1.5s for Core 0's initial Showcase health probe so
    // PicoMQTT never starts when Showcase is online (loop() falls back automatically after 1.5s if offline).
    if (!wantShowcaseBroker) {
        applyBrokerTransport(false, mqttConfig.broker.c_str(), static_cast<uint16_t>(mqttConfig.port));
    }
}

void FrontendSyncEngine::reconnectTaskFunc(void* param) {
    FrontendSyncEngine* self = static_cast<FrontendSyncEngine*>(param);
    if (self) {
        if (!self->stopReconnectRequested.load(std::memory_order_acquire)) {
            self->reconnect();
        }
        self->isReconnecting.store(false, std::memory_order_release);
        self->reconnectTaskHandle = nullptr;
        self->reconnectTaskExited.store(true, std::memory_order_release);
    }
    vTaskDelete(nullptr);
}

void FrontendSyncEngine::stop() {
    cancelInGameShowcaseSession(false);
    resetSyncCardCycle();
    isGamePlaying = false;
    waitingDisplayed = false;
    hasPendingEvent = false;
    m_hasPendingNetArt = false;
    m_pendingNetArtDueMs = 0;
    m_pendingNetArtJob = ArtDownloadJobSlot{};
    m_activeMarqueePath[0] = '\0';
    m_wasShowingInGame = false;
    m_wasExternalConnected = false;

    m_activeArtReqId.store(0U, std::memory_order_release);
    m_artNetBusy.store(false, std::memory_order_release);
    m_artResultPending.store(false, std::memory_order_release);
    m_artJobSeq.fetch_add(1U, std::memory_order_acq_rel);
    m_artJobSlot = ArtDownloadJobSlot{};
    m_artJobSeq.fetch_add(1U, std::memory_order_release);

#if defined(ESP32) && !defined(UNIT_TEST)
    m_workerStopRequested.store(true, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_INGAME_SYNC);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_ARTWORK);
    if (m_showcaseWorkerHandle || !m_workerExited.load(std::memory_order_acquire)) {
        for (int i = 0; i < 200 && !m_workerExited.load(std::memory_order_acquire); ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        m_showcaseWorkerHandle = nullptr;
    }
#endif
    systemMappings.clear();
    m_mappingsLoaded = false;
    freeCatalogPsramCache();
    freeStreamBuffersSafely();

    stopReconnectTask();

    if (internalBroker) {
        delete internalBroker;
        internalBroker = nullptr;
    }
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    m_transportConfigured = false;
    writeSharedBrokerState(false, "", 1883, false);

    if (gif) gif->stop();
    if (message) message->deactivate();
    LOGI("RetroFrontend", "MQTT Listener stopped.");
}

bool FrontendSyncEngine::loop() {
    if (!mqttConfig.enabled) {
        if (waitingDisplayed) {
            waitingDisplayed = false;
            if (message) message->deactivate();
        }
        return true;
    }

    // Consume any completed Core 0 Pixelcade artwork download result (100% lock-free seqlock)
    if (m_artResultPending.exchange(false, std::memory_order_acq_rel)) {
        ArtDownloadResultSlot res{};
        bool validRead = false;
        for (int attempt = 0; attempt < 4; ++attempt) {
            uint32_t s1 = m_artResultSeq.load(std::memory_order_acquire);
            if ((s1 & 1U) != 0U) continue;
            ArtDownloadResultSlot tmp = m_artResultSlot;
            uint32_t s2 = m_artResultSeq.load(std::memory_order_acquire);
            if (s1 == s2) {
                res = tmp;
                validRead = true;
                break;
            }
        }
        if (validRead) {
            if (res.missingHash != 0U) {
                recordMissingArtHash(res.missingHash);
            }
            if (res.downloaded && res.savedPath[0] != '\0' &&
                res.reqId == currentRequestId && isGamePlaying) {
                LOGI("RetroFrontend", "Playing downloaded Pixelcade art: %s", res.savedPath);
                (void)tryPlayMarqueePath(res.savedPath);
            }
        }
    }

    // Check lock-free broker resolution state from Core 0 worker
    bool wantShowcaseBroker = (showcaseConfig.enabled && mqttConfig.use_showcase_broker);
    bool resolvedFromShowcase = false;
    char resolvedHost[96] = "";
    uint16_t resolvedPort = 1883;
    bool probedOnce = false;
    readSharedBrokerState(resolvedFromShowcase, resolvedHost, sizeof(resolvedHost), resolvedPort, probedOnce);

    if (!wantShowcaseBroker) {
        resolvedFromShowcase = false;
        copySafeCStr(resolvedHost, sizeof(resolvedHost), mqttConfig.broker.c_str());
        resolvedPort = static_cast<uint16_t>(mqttConfig.port > 0 ? mqttConfig.port : 1883);
        probedOnce = true;
    } else if (!resolvedFromShowcase && !probedOnce &&
               !hardwareHAL.capabilities().hasPsram && showcaseConfig.server_url.length() >= 8) {
        // Zero-socket passive adoption on non-PSRAM boards before initial worker probe
        const unsigned long nowCheck = millis();
        if (m_lastBrokerCacheCheckMs == 0 || (nowCheck - m_lastBrokerCacheCheckMs) >= 3000UL) {
            m_lastBrokerCacheCheckMs = nowCheck == 0 ? 1UL : nowCheck;
            char cachedUrl[112] = "";
            char cachedHost[64] = "";
            uint16_t cachedPort = 8890;
            bool cachedOnline = false;
            if (net::ShowcaseDiscovery::peekCachedUrlStatus(
                    showcaseConfig.server_url.c_str(),
                    cachedUrl,
                    sizeof(cachedUrl),
                    cachedHost,
                    sizeof(cachedHost),
                    cachedPort,
                    cachedOnline,
                    nullptr,
                    0) &&
                cachedOnline && !net::ShowcaseDiscovery::isAutoDiscoverableHost(cachedHost)) {
                writeSharedBrokerState(true, cachedHost, 1883, true);
                resolvedFromShowcase = true;
                copySafeCStr(resolvedHost, sizeof(resolvedHost), cachedHost);
                resolvedPort = 1883;
                probedOnce = true;
            }
        }
    }

    if (probedOnce || (millis() - m_beginMs > 1500)) {
        const char* desiredHost = resolvedFromShowcase ? resolvedHost : mqttConfig.broker.c_str();
        uint16_t desiredPort = resolvedFromShowcase
            ? resolvedPort
            : static_cast<uint16_t>(mqttConfig.port > 0 ? mqttConfig.port : 1883);
        if (!m_transportConfigured ||
            m_activeFromShowcase != resolvedFromShowcase ||
            m_activeBrokerPort != desiredPort ||
            strcmp(m_activeBrokerHost, desiredHost) != 0) {
            applyBrokerTransport(resolvedFromShowcase, desiredHost, desiredPort);
        }
    }

    // Advance opportunistic High-Score / RetroAchievements card cycle when playing
    updateSyncCardCycle();

    // Restore static PNG marquee cleanly onto shared surface when InGameShowcase yields back (`once` / `alternating`)
    bool showingInGame = wantsInGameShowcaseDisplay();
    if (m_wasShowingInGame && !showingInGame && isGamePlaying && gif && m_activeMarqueePath[0] != '\0') {
        size_t pLen = strlen(m_activeMarqueePath);
        if (pLen > 4 && strcasecmp(m_activeMarqueePath + pLen - 4, ".png") == 0) {
            gif->playGif(m_activeMarqueePath);
        }
    }
    m_wasShowingInGame = showingInGame;

    if (m_transportConfigured) {
        if (internalBroker) {
            internalBroker->loop();
        }
        const bool useExternalClient = m_activeFromShowcase || !isLoopbackOrEmptyHost(mqttConfig.broker.c_str());
        if (useExternalClient) {
            // Strictly serialize access to `mqttClient` and `espClient`: while Core 0 `MqttReconnect`
            // is connecting and writing SUBSCRIBE packets, Core 1 must never touch `mqttClient` or `espClient`.
            if (!isReconnecting.load(std::memory_order_acquire) &&
                reconnectTaskExited.load(std::memory_order_acquire)) {
#if defined(ESP32)
                const bool hasPsram = hardwareHAL.capabilities().hasPsram;
                // Yield the idle FrontendSync MQTT socket ONLY during `WebUiSync` (browser SPA load).
                // Never tear down an already-connected MQTT socket during `MediaShowcaseBase` turns: keeping
                // the established socket avoids `FIN_WAIT` `pbuf` retention and prevents a 4 KB `MqttReconnect`
                // task + TCP handshake from colliding with Showcase video streams 10s later!
                if (!hasPsram && !isGamePlaying &&
                    (WebUiSyncEngine::isRequested() || WebUiSyncEngine::isReadyForServing())) {
                    if (mqttClient.connected()) {
                        mqttClient.disconnect();
                        espClient.stop();
                        m_wasExternalConnected = false;
                        lastReconnectAttempt = millis() == 0 ? 1UL : millis();
                    }
                    return true;
                }
#endif
                if (!mqttClient.connected()) {
                    if (m_wasExternalConnected) {
                        m_wasExternalConnected = false;
                        LOGW("RetroFrontend", "%s MQTT broker disconnected (state=%d); scheduling reconnect...",
                             m_activeFromShowcase ? "Showcase" : "External", mqttClient.state());
                    }
                    unsigned long now = millis();
                    const unsigned long retryInterval = (lastReconnectAttempt == 0)
                        ? 0UL
                        : (m_activeFromShowcase ? 2500UL : 5000UL);
                    bool canSpawnReconnect = true;
                    uint32_t reconnectStack = 4096U;
#if defined(ESP32)
                    if (!hasPsram) {
                        const size_t free8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                        const size_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                        canSpawnReconnect =
                            !WebUiSyncEngine::isRequested() &&
                            !WebUiSyncEngine::isReadyForServing() &&
                            (NetworkBudget::getDmdNetActivity() == NetworkBudget::DmdNetActivity::Idle) &&
                            (free8 >= 14336U) &&
                            (largest8 >= 6144U);
                    }
#endif
                    if (canSpawnReconnect &&
                        (lastReconnectAttempt == 0 || (now - lastReconnectAttempt > retryInterval))) {
                        lastReconnectAttempt = now == 0 ? 1UL : now;
                        stopReconnectRequested.store(false, std::memory_order_release);
                        isReconnecting.store(true, std::memory_order_release);
                        reconnectTaskExited.store(false, std::memory_order_release);
                        if (xTaskCreatePinnedToCore(reconnectTaskFunc, "MqttReconnect", reconnectStack, this, 1, &reconnectTaskHandle, 0) != pdPASS) {
                            reconnectTaskHandle = nullptr;
                            isReconnecting.store(false, std::memory_order_release);
                            reconnectTaskExited.store(true, std::memory_order_release);
                        }
                    }
                } else {
                    m_wasExternalConnected = true;
                    // Drain up to 8 queued MQTT packets per tick so rapid wheel scrolling never lags behind
                    for (int pkt = 0; pkt < 8; ++pkt) {
                        if (!mqttClient.loop()) {
                            break;
                        }
                        if (!espClient.available()) {
                            break;
                        }
                    }
                }
            }
        }
    }

    if (hasPendingEvent) {
        // Tier 1: Snappy 40 ms UI/SD debounce for rapid browsing (covers both "browsing" and Recalbox's "selected");
        // immediate (0 ms) on first event or playing/stopped.
        const bool isPlayingOrStopped =
            (pendingPayload.indexOf("\"status\":\"playing\"") != -1 ||
             pendingPayload.indexOf("\"status\": \"playing\"") != -1 ||
             pendingPayload.indexOf("\"status\":\"stopped\"") != -1 ||
             pendingPayload.indexOf("\"status\": \"stopped\"") != -1);
        const unsigned long debounceMs = (!hasReceivedAnyEvent || isPlayingOrStopped) ? 0UL : 40UL;
        if ((millis() - pendingEventTime) >= debounceMs) {
            hasPendingEvent = false;
            uint32_t reqId = currentRequestId;
            handleGameEvent(pendingPayload, reqId);
        }
    } else if (!hasReceivedAnyEvent && (!waitingDisplayed || (message && !message->isActive())) && message) {
        waitingDisplayed = true;
        MessageConfig cfg = { "WAITING FOR MARQUEE", 0xFFFF, 1, "none", 40, 0, 0 };
        message->displayMessage(cfg);
        LOGI("RetroFrontend", "MQTT Enabled: Displaying WAITING FOR MARQUEE on DMD.");
    }

    // Tier 2: 140 ms network settle window while browsing so rapid wheel scrolling never fires intermediate HTTPS GETs
    if (m_hasPendingNetArt) {
        if (m_pendingNetArtJob.reqId != currentRequestId || !isGamePlaying) {
            m_hasPendingNetArt = false;
        } else if (static_cast<int32_t>(millis() - m_pendingNetArtDueMs) >= 0) {
            ArtDownloadJobSlot settled = m_pendingNetArtJob;
            m_hasPendingNetArt = false;
            dispatchAsyncArtJob(
                settled.primaryFolder,
                settled.secondaryFolder,
                settled.query,
                settled.title,
                settled.localStem,
                settled.reqId,
                settled.negHash,
                settled.isSystem
            );
        }
    }

    return true;
}

void FrontendSyncEngine::reconnect() {
    if (stopReconnectRequested.load(std::memory_order_acquire)) return;
    if (WiFi.status() == WL_CONNECTED) {
        espClient.stop();
        espClient.setTimeout(1500);
        String cid = mqttConfig.deviceName;
        char hwId[8] = "";
        net::ShowcaseDiscovery::hardwareShortId(hwId, sizeof(hwId));
        cid += "_";
        cid += hwId;
        bool connected = false;
        if (!m_activeFromShowcase && mqttConfig.user.length() > 0) {
            connected = mqttClient.connect(cid.c_str(), mqttConfig.user.c_str(), mqttConfig.pass.c_str());
        } else {
            connected = mqttClient.connect(cid.c_str());
        }
        if (connected) {
            espClient.setNoDelay(true);
            if (!stopReconnectRequested.load(std::memory_order_acquire)) {
                const bool subMain = mqttClient.subscribe(MQTT_TOPIC_WILDCARD);
                bool subCmd = true;
                if (m_activeFromShowcase) {
                    subCmd = mqttClient.subscribe(MQTT_TOPIC_SHOWCASE_CMD);
                }
                LOGI("RetroFrontend", "Connected to %s MQTT broker %s:%u (id='%s', sub=%d/%d)",
                     m_activeFromShowcase ? "Showcase" : "external",
                     m_activeBrokerHost,
                     m_activeBrokerPort,
                     cid.c_str(),
                     subMain ? 1 : 0,
                     subCmd ? 1 : 0);
                if (!subMain) {
                    mqttClient.disconnect();
                    espClient.stop();
                }
            }
        } else {
            espClient.stop();
            LOGW("RetroFrontend", "Failed to connect to %s MQTT broker %s:%u (state=%d)",
                 m_activeFromShowcase ? "Showcase" : "external",
                 m_activeBrokerHost,
                 m_activeBrokerPort,
                 mqttClient.state());
        }
    }
}

void FrontendSyncEngine::callback(char* topic, byte* payload, unsigned int length) {
    if (!instance) return;

    String payloadStr;
    payloadStr.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }

    instance->handleMessage(String(topic), payloadStr);
}

void FrontendSyncEngine::handleMessage(String topic, String msg) {
    if (topic.startsWith("system/playing/")) {
        LOGI("RetroFrontend", "MQTT Event Received: Topic = '%s', Payload = '%s'", topic.c_str(), msg.c_str());
        if (msg.indexOf("\"sync_meta\"") != -1 || msg.indexOf("\"celebrate\"") != -1) {
            if (handleCompanionEvent(msg)) {
                return;
            }
        }
        pendingPayload = msg;
        hasPendingEvent = true;
        pendingEventTime = millis();
        m_hasPendingNetArt = false;
        currentRequestId++;
        if (currentRequestId == 0U) {
            currentRequestId = 1U;
        }
        // Immediately cancel and force-close any in-flight Core 0 HTTP download when user scrolls,
        // while keeping warm Keep-Alive TLS sessions alive when Core 0 is idle!
        m_activeArtReqId.store(currentRequestId, std::memory_order_seq_cst);
#if defined(ESP32) && !defined(UNIT_TEST)
        if (m_artNetBusy.load(std::memory_order_seq_cst)) {
            net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_ARTWORK);
        }
#endif
        return;
    }

    if (topic.startsWith("dmd_showcase/clients/") && topic.endsWith("/command")) {
        if (!topic.endsWith("_sync/command")) {
            return;
        }
        char hwPrefix[20] = "";
        char hwId[8] = "";
        net::ShowcaseDiscovery::hardwareShortId(hwId, sizeof(hwId));
        snprintf(hwPrefix, sizeof(hwPrefix), "esp32_%s_", hwId);
        // Only wake up for commands targeting this board's companion *_sync slot (or legacy broadcast *_sync slot)
        if ((topic.indexOf(hwPrefix) != -1 || topic.indexOf("/esp32_games_") != -1) &&
            msg.indexOf("\"play_now\"") != -1) {
            m_readyWakeupHint.store(true, std::memory_order_release);
        }
        return;
    }
}

void FrontendSyncEngine::startInGameShowcaseSession(
    const String& cleanSystem,
    const String& rawGame,
    const String& rawTitle
) {
    if (!showcaseConfig.enabled || !mqttConfig.ingame_showcase_enabled || rawGame.length() == 0) {
        cancelInGameShowcaseSession();
        return;
    }

    uint32_t tok = m_playSessionToken.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_wantsInGameDisplay.store(false, std::memory_order_release);
    m_readyWakeupHint.store(false, std::memory_order_release);
    if (m_inGameEngine) {
        m_inGameEngine->clearFrame();
    }

    m_remoteSyncDispatched = true;
    m_jobSeq.fetch_add(1, std::memory_order_acq_rel);
    copySafeCStr(m_jobSlot.system, sizeof(m_jobSlot.system), cleanSystem.c_str());
    copySafeCStr(m_jobSlot.game, sizeof(m_jobSlot.game), rawGame.c_str());
    copySafeCStr(m_jobSlot.title, sizeof(m_jobSlot.title), rawTitle.c_str());
    m_jobSlot.token = tok;
    m_jobSlot.active = true;
    m_jobSlot.cancelRemote = false;
    m_jobSeq.fetch_add(1, std::memory_order_release);

    m_workerWakeLatch.store(true, std::memory_order_release);
    ensureShowcaseWorkerRunning();
}

void FrontendSyncEngine::cancelInGameShowcaseSession(bool allowWorkerSpawn) {
    m_wantsInGameDisplay.store(false, std::memory_order_release);
    m_readyWakeupHint.store(false, std::memory_order_release);
    if (!m_remoteSyncDispatched) {
        return;
    }
    m_remoteSyncDispatched = false;

    uint32_t tok = m_playSessionToken.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (m_inGameEngine) {
        m_inGameEngine->clearFrame();
    }
    m_jobSeq.fetch_add(1, std::memory_order_acq_rel);
    m_jobSlot.token = tok;
    m_jobSlot.active = false;
    m_jobSlot.cancelRemote = true;
    m_jobSlot.system[0] = '\0';
    m_jobSlot.game[0] = '\0';
    m_jobSlot.title[0] = '\0';
    m_jobSeq.fetch_add(1, std::memory_order_release);

    if (allowWorkerSpawn) {
        m_workerWakeLatch.store(true, std::memory_order_release);
        ensureShowcaseWorkerRunning();
    }
}

bool FrontendSyncEngine::ensureStreamScratchAllocated() {
    if (m_streamIoScratch != nullptr &&
        m_streamIoScratchCap >= engines::showcase::ShowcaseStreamPlayer::MIN_SESSION_SCRATCH_BYTES) {
        return true;
    }
    if (m_streamIoScratch) {
        free(m_streamIoScratch);
        m_streamIoScratch = nullptr;
        m_streamIoScratchCap = 0U;
    }
#if defined(ESP32)
    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
#if SHOWCASE_PLAYER_SUPPORT_PSRAM_PRELOAD
    if (hasPsram) {
        m_streamIoScratch = static_cast<char*>(
            heap_caps_malloc(STREAM_SESSION_SCRATCH_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
        if (m_streamIoScratch) {
            m_streamIoScratchCap = STREAM_SESSION_SCRATCH_BYTES;
            m_streamIoScratch[0] = '\0';
            return true;
        }
    }
#else
    (void)hasPsram;
#endif
    if (!hasPsram) {
        const size_t free8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (free8 < (STREAM_SESSION_SCRATCH_BYTES + 8192U) || largest8 < STREAM_SESSION_SCRATCH_BYTES) {
            return false;
        }
    }
#endif
    m_streamIoScratch = static_cast<char*>(malloc(STREAM_SESSION_SCRATCH_BYTES));
    if (!m_streamIoScratch) {
        return false;
    }
    m_streamIoScratchCap = STREAM_SESSION_SCRATCH_BYTES;
    m_streamIoScratch[0] = '\0';
    return true;
}

void FrontendSyncEngine::freeStreamBuffersSafely() {
    m_wantsInGameDisplay.store(false, std::memory_order_release);
    if (m_inGameEngine) {
        m_inGameEngine->streamPlayer().resetCore1StreamState();
        m_inGameEngine->clearFrame();
        m_inGameEngine->waitRenderBlitQuiescent();
#if defined(ESP32) && !defined(UNIT_TEST)
        for (int i = 0; i < 50 && m_inGameEngine->isDisplayEngaged(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        vTaskDelay(pdMS_TO_TICKS(18));
#endif
        m_inGameEngine->streamPlayer().releaseAll();
    }
    if (m_streamIoScratch) {
        free(m_streamIoScratch);
        m_streamIoScratch = nullptr;
    }
    m_streamIoScratchCap = 0U;
}

void FrontendSyncEngine::freeCatalogPsramCache() {
    if (m_cachedCatalogPsram) {
        free(m_cachedCatalogPsram);
        m_cachedCatalogPsram = nullptr;
    }
    m_cachedCatalogLen = 0U;
    m_cachedCatalogPath[0] = '\0';
}

void FrontendSyncEngine::deleteAllShowcaseQueueHttp(
    const char* resolvedServerUrl,
    const char* syncClientId
) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!resolvedServerUrl || !syncClientId || syncClientId[0] == '\0') return;
    char host[96] = "";
    uint16_t port = 8890;
    if (!net::parseEndpointHostAndPort(resolvedServerUrl, 8890, host, sizeof(host), port, nullptr)) return;
    if (!net::ShowcaseDiscovery::resolveHostInPlace(host, sizeof(host), port)) return;

    char deletePath[128];
    const int n = snprintf(
        deletePath,
        sizeof(deletePath),
        "/api/v1/clients/%s/queue",
        syncClientId
    );
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(deletePath)) return;
    net::sendHttpDeleteOneShotAbort(host, port, deletePath, 800U, 300U);
#else
    (void)resolvedServerUrl;
    (void)syncClientId;
#endif
}

bool FrontendSyncEngine::streamReadyItemOnce(
    const char* resolvedServerUrl,
    const char* syncClientId,
    const char* itemId,
    uint32_t jobToken,
    uint16_t expectedW,
    uint16_t expectedH,
    uint32_t configuredStreamMs
) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!resolvedServerUrl || !itemId || itemId[0] == '\0' || !m_inGameEngine) return false;
    if (!ensureStreamScratchAllocated()) {
        LOGW("RetroFrontend", "Insufficient memory for %ux%u In-Game Showcase stream; keeping marquee.", expectedW, expectedH);
        return false;
    }

    char streamPath[224];
    const int pathLen = snprintf(
        streamPath,
        sizeof(streamPath),
        "/api/v1/games/stream/%s.rgb565.stream?purge=false&codec=dmdz2",
        itemId
    );
    if (pathLen <= 0 || static_cast<size_t>(pathLen) >= sizeof(streamPath)) {
        return false;
    }

    m_inGameEngine->resetStreamTelemetry();

    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    struct StreamCallbackCtx {
        FrontendSyncEngine* self;
        uint32_t expectedToken;
        bool hasPsram;
    } cbCtx{this, jobToken, hasPsram};

    auto checkStopFn = [](void* rawCtx) -> net::StreamStopReason {
        const auto* c = static_cast<const StreamCallbackCtx*>(rawCtx);
        if (c->self->m_workerStopRequested.load(std::memory_order_acquire) ||
            c->self->m_playSessionToken.load(std::memory_order_acquire) != c->expectedToken) {
            return net::StreamStopReason::Deactivated;
        }
        if (!c->hasPsram && WebUiSyncEngine::isRequested()) {
            return net::StreamStopReason::WebUiYield;
        }
        return net::StreamStopReason::Continue;
    };

    engines::showcase::ShowcaseStreamRunOptions runOpts{};
    runOpts.baseServerUrl = resolvedServerUrl;
    runOpts.streamPath = streamPath;
    runOpts.clientId = (syncClientId && syncClientId[0] != '\0') ? syncClientId : "esp32_games_sync";
    runOpts.logTag = "RetroFrontend";
    runOpts.surfaceForReadOnlyCheck = m_inGameEngine->surface();
    runOpts.ioScratch = m_streamIoScratch;
    runOpts.ioScratchCap = m_streamIoScratchCap;
    runOpts.panelWidth = expectedW;
    runOpts.panelHeight = expectedH;
    runOpts.preStreamCardWaitMs = 0U;
    runOpts.cardStartedAtMs = 0U;
    runOpts.maxStreamMs = std::min<uint32_t>(300000U, std::max<uint32_t>(12000U, configuredStreamMs + 6000U));
    runOpts.hasPsram = hasPsram;
    runOpts.purgeOnComplete = false;
    runOpts.waitFirstFrameBeforeLive = true; // Late Preemption: never interrupt marquee unless Frame 0 is buffered!
    runOpts.userCtx = &cbCtx;
    runOpts.checkStop = checkStopFn;
    runOpts.onEnterLiveStream = [](void* rawCtx) {
        auto* c = static_cast<StreamCallbackCtx*>(rawCtx);
        c->self->m_wantsInGameDisplay.store(true, std::memory_order_release);
    };
    runOpts.onExitLiveStream = [](void* rawCtx) {
        auto* c = static_cast<StreamCallbackCtx*>(rawCtx);
        if (c->self->m_inGameEngine) {
            c->self->m_inGameEngine->waitRenderBlitQuiescent();
        }
    };

    return m_inGameEngine->streamPlayer().runStreamSession(runOpts);
#else
    (void)resolvedServerUrl;
    (void)syncClientId;
    (void)itemId;
    (void)jobToken;
    (void)expectedW;
    (void)expectedH;
    (void)configuredStreamMs;
    return false;
#endif
}

void FrontendSyncEngine::showcaseWorkerTaskFunc(void* param) {
    FrontendSyncEngine* self = static_cast<FrontendSyncEngine*>(param);
    if (self) {
        self->showcaseWorkerLoop();
        self->m_showcaseWorkerHandle = nullptr;
        self->m_workerExited.store(true, std::memory_order_release);
    }
    vTaskDelete(nullptr);
}

void FrontendSyncEngine::showcaseWorkerLoop() {
#if defined(ESP32) && !defined(UNIT_TEST)
    static constexpr uint32_t INGAME_QUEUE_READY_TIMEOUT_MS = 120000U;
    static constexpr uint32_t PIXELCADE_SESSION_IDLE_MS = 15000U;
    uint32_t lastHealthProbeMs = 0;
    uint32_t lastCheckedCfgGen = 0;
    uint8_t consecutiveHealthFailures = 0;
    bool currentShowcaseOnline = false;
    uint32_t lastProcessedJobToken = 0;
    uint32_t lastPurgedAtToken = 0;
    uint32_t lastProcessedArtReqId = 0;
    uint32_t lastArtActivityMs = 0;
    bool remoteSyncDirtyOnWorker = false;

    std::unique_ptr<net::SecureHttpSession> pixelcadeSession;

    char activeQueueServerUrl[112] = "";
    char activeQueueClientId[48] = "";
    char activeQueueId[48] = "";

    auto cleanupActiveQueue = [&]() {
        if (remoteSyncDirtyOnWorker && activeQueueClientId[0] != '\0') {
            deleteAllShowcaseQueueHttp(activeQueueServerUrl, activeQueueClientId);
            remoteSyncDirtyOnWorker = false;
            activeQueueId[0] = '\0';
            lastPurgedAtToken = m_playSessionToken.load(std::memory_order_acquire);
        }
    };

    auto readWorkerCfg = [&](WorkerConfigSlot& out) {
        for (int i = 0; i < 4; ++i) {
            uint32_t s1 = m_cfgSeq.load(std::memory_order_acquire);
            if ((s1 & 1U) != 0U) continue;
            WorkerConfigSlot tmp = m_workerCfg;
            uint32_t s2 = m_cfgSeq.load(std::memory_order_acquire);
            if (s1 == s2) {
                out = tmp;
                return;
            }
        }
        out = m_workerCfg;
    };

    auto readJobSlot = [&](InGameJobSlot& out) {
        for (int i = 0; i < 4; ++i) {
            uint32_t s1 = m_jobSeq.load(std::memory_order_acquire);
            if ((s1 & 1U) != 0U) continue;
            InGameJobSlot tmp = m_jobSlot;
            uint32_t s2 = m_jobSeq.load(std::memory_order_acquire);
            if (s1 == s2) {
                out = tmp;
                return;
            }
        }
        out = m_jobSlot;
    };

    auto readArtJobSlot = [&](ArtDownloadJobSlot& out) {
        for (int i = 0; i < 4; ++i) {
            uint32_t s1 = m_artJobSeq.load(std::memory_order_acquire);
            if ((s1 & 1U) != 0U) continue;
            ArtDownloadJobSlot tmp = m_artJobSlot;
            uint32_t s2 = m_artJobSeq.load(std::memory_order_acquire);
            if (s1 == s2) {
                out = tmp;
                return;
            }
        }
        out = m_artJobSlot;
    };

    while (!m_workerStopRequested.load(std::memory_order_acquire)) {
        WorkerConfigSlot wcfg;
        readWorkerCfg(wcfg);
        uint32_t curCfgGen = m_cfgGen.load(std::memory_order_acquire);

        uint32_t nowMs = millis();
        bool hasPsram = hardwareHAL.capabilities().hasPsram;

        // Release idle keep-alive TLS socket on WebUI request, non-PSRAM, or after 15s idle
        if (pixelcadeSession) {
            if (!hasPsram ||
                WebUiSyncEngine::isRequested() ||
                WebUiSyncEngine::isReadyForServing() ||
                (lastArtActivityMs > 0U && (nowMs - lastArtActivityMs) >= PIXELCADE_SESSION_IDLE_MS)) {
                pixelcadeSession.reset();
            }
        }

        // 1. Periodic / config-triggered Showcase MQTT broker discovery
        bool needProbe = (curCfgGen != lastCheckedCfgGen) ||
                         (lastHealthProbeMs == 0) ||
                         ((nowMs - lastHealthProbeMs) >= (currentShowcaseOnline ? (hasPsram ? 12000U : 30000U) : 5000U));
        if (needProbe) {
            [&]() __attribute__((noinline)) {
                if (!hasPsram &&
                    (WebUiSyncEngine::isRequested() ||
                     WebUiSyncEngine::isReadyForServing() ||
                     NetworkBudget::isBulkTransferActive() ||
                     NetworkBudget::getDmdNetActivity() != NetworkBudget::DmdNetActivity::Idle)) {
                    // Fast zero-socket cache reuse when `MediaShowcaseBase` already verified the Showcase host
                    if (wcfg.mqttEnabled && wcfg.showcaseEnabled && wcfg.useShowcaseBroker && strlen(wcfg.showcaseUrl) >= 8) {
                        char cachedUrl[112] = "";
                        char cachedHost[64] = "";
                        uint16_t cachedPort = 8890;
                        bool cachedOnline = false;
                        if (net::ShowcaseDiscovery::peekCachedUrlStatus(
                                wcfg.showcaseUrl,
                                cachedUrl,
                                sizeof(cachedUrl),
                                cachedHost,
                                sizeof(cachedHost),
                                cachedPort,
                                cachedOnline,
                                nullptr,
                                0) &&
                            cachedOnline && !net::ShowcaseDiscovery::isAutoDiscoverableHost(cachedHost)) {
                            consecutiveHealthFailures = 0;
                            currentShowcaseOnline = true;
                            lastCheckedCfgGen = curCfgGen;
                            lastHealthProbeMs = nowMs == 0 ? 1 : nowMs;
                            writeSharedBrokerState(true, cachedHost, 1883, true);
                        }
                    }
                } else {
                    lastCheckedCfgGen = curCfgGen;
                    lastHealthProbeMs = nowMs == 0 ? 1 : nowMs;

                    if (wcfg.mqttEnabled && wcfg.showcaseEnabled && wcfg.useShowcaseBroker &&
                        WiFi.status() == WL_CONNECTED && strlen(wcfg.showcaseUrl) >= 8) {
                        char baseServerUrl[112] = "";
                        char resolvedShowcaseHost[96] = "";
                        uint16_t cachedPort = 8890;
                        bool cachedOnline = false;
                        bool healthOk = false;
                        String brokerHost;
                        uint16_t brokerPort = 1883;

                        // Zero-socket fast path when `MediaShowcaseBase` already holds a fresh verified lease
                        if (!hasPsram &&
                            net::ShowcaseDiscovery::peekCachedUrlStatus(
                                wcfg.showcaseUrl,
                                baseServerUrl,
                                sizeof(baseServerUrl),
                                resolvedShowcaseHost,
                                sizeof(resolvedShowcaseHost),
                                cachedPort,
                                cachedOnline,
                                nullptr,
                                0,
                                60000U) &&
                            cachedOnline && !net::ShowcaseDiscovery::isAutoDiscoverableHost(resolvedShowcaseHost)) {
                            brokerHost = String(resolvedShowcaseHost);
                            brokerPort = 1883;
                            healthOk = true;
                        } else if (resolveBaseServerUrl(wcfg.showcaseUrl, baseServerUrl, sizeof(baseServerUrl), resolvedShowcaseHost, sizeof(resolvedShowcaseHost))) {
                            net::SecureHttpOptions hOpts;
                            hOpts.ownerId = net::OWNER_INGAME_SYNC;
                            hOpts.requestTimeoutMs = 1200;
                            hOpts.keepAlive = false;
                            auto hResp = net::SecureHttpClient::get(String(baseServerUrl) + "/api/v1/health", hOpts);
                            if (hResp.ok()) {
                                String body = hResp.body();
                                healthOk = parseShowcaseHealthBroker(body.c_str(), resolvedShowcaseHost, brokerHost, brokerPort);
                            }
                        }

                        if (healthOk) {
                            consecutiveHealthFailures = 0;
                            currentShowcaseOnline = true;
                            writeSharedBrokerState(true, brokerHost.c_str(), brokerPort, true);
                        } else {
                            if (consecutiveHealthFailures < 10) consecutiveHealthFailures++;
                            if (!currentShowcaseOnline || consecutiveHealthFailures >= 2) {
                                currentShowcaseOnline = false;
                                writeSharedBrokerState(false, wcfg.localBroker, wcfg.localPort, true);
                            }
                        }
                    } else {
                        consecutiveHealthFailures = 0;
                        currentShowcaseOnline = false;
                        writeSharedBrokerState(false, wcfg.localBroker, wcfg.localPort, true);
                    }
                }
            }();
        }

        // 2. Process pending Pixelcade marquee/system artwork download BEFORE In-Game Showcase
        //    so game launch first resolves and caches the marquee cleanly, then starts Showcase stream!
        const bool ranArtJob = [&]() __attribute__((noinline)) -> bool {
            ArtDownloadJobSlot artJob;
            readArtJobSlot(artJob);
            if (artJob.active && artJob.reqId != 0U && artJob.reqId != lastProcessedArtReqId) {
                lastProcessedArtReqId = artJob.reqId;
                if (artJob.reqId == m_activeArtReqId.load(std::memory_order_acquire) &&
                    !m_workerStopRequested.load(std::memory_order_acquire)) {
                    processWorkerArtJob(artJob, pixelcadeSession, lastArtActivityMs);
                }
                if (!hasPsram) {
                    pixelcadeSession.reset();
                }
                return true;
            }
            return false;
        }();
        if (ranArtJob) {
            // Re-check immediately if user scrolled to another game while this download ran
            continue;
        }

        // 3. Check for pending In-Game Showcase launch or cancellation (isolated stack frame)
        [&]() __attribute__((noinline)) {
        InGameJobSlot job;
        readJobSlot(job);
        if (job.token != lastProcessedJobToken) {
            // Guarantee zero mbedTLS DRAM overlap with In-Game Showcase video/card streaming!
            pixelcadeSession.reset();
            cleanupActiveQueue();
            freeStreamBuffersSafely();
            lastProcessedJobToken = job.token;

            uint16_t targetW = m_inGameEngine ? m_inGameEngine->surfaceWidth() : wcfg.panelW;
            uint16_t targetH = m_inGameEngine ? m_inGameEngine->surfaceHeight() : wcfg.panelH;
            if (targetW < 32) targetW = 128;
            if (targetH < 16) targetH = 32;

            char baseServerUrl[112] = "";
            char resolvedShowcaseHost[96] = "";
            char syncClientId[48] = "";
            const bool targetResolved =
                wcfg.mqttEnabled && wcfg.showcaseEnabled &&
                WiFi.status() == WL_CONNECTED && strlen(wcfg.showcaseUrl) >= 8 &&
                resolveBaseServerUrl(wcfg.showcaseUrl, baseServerUrl, sizeof(baseServerUrl), resolvedShowcaseHost, sizeof(resolvedShowcaseHost));
            if (targetResolved) {
                net::ShowcaseDiscovery::formatClientId("games", targetW, targetH, true, syncClientId, sizeof(syncClientId));
            }

            if (!job.active && job.cancelRemote && job.token != lastPurgedAtToken && targetResolved) {
                deleteAllShowcaseQueueHttp(baseServerUrl, syncClientId);
                lastPurgedAtToken = job.token;
            }

            if (job.active && wcfg.ingameShowcaseEnabled && job.game[0] != '\0' && targetResolved) {
                // Arm active queue target BEFORE POST so a quick (<100ms) game exit still purges the sync slot
                copySafeCStr(activeQueueServerUrl, sizeof(activeQueueServerUrl), baseServerUrl);
                copySafeCStr(activeQueueClientId, sizeof(activeQueueClientId), syncClientId);
                activeQueueId[0] = '\0';
                remoteSyncDirtyOnWorker = true;

                MqttConfig reqMqttCfg;
                reqMqttCfg.card_style_source = wcfg.cardStyleSource;
                reqMqttCfg.include_card_intro = wcfg.includeCardIntro;
                reqMqttCfg.card_layout = wcfg.cardLayout;
                reqMqttCfg.card_theme = wcfg.cardTheme;
                reqMqttCfg.show_poster = wcfg.showPoster;
                reqMqttCfg.show_synopsis = wcfg.showSynopsis;
                reqMqttCfg.text_scroll_speed = wcfg.textScrollSpeed;
                reqMqttCfg.jacket_panorama = wcfg.jacketPanorama;
                reqMqttCfg.marquee_panorama = wcfg.marqueePanorama;
                reqMqttCfg.snapshot_panorama_count = wcfg.snapshotPanoramaCount;
                reqMqttCfg.show_video = wcfg.showVideo;
                reqMqttCfg.video_max_duration_s = wcfg.videoMaxDurationS;
                reqMqttCfg.video_fps = wcfg.videoFps;
                reqMqttCfg.video_mode = wcfg.videoMode;
                reqMqttCfg.color_boost = wcfg.colorBoost;

                String postBody = buildShowcaseQueueRequestJson(
                    reqMqttCfg,
                    String(job.system),
                    String(job.game),
                    targetW,
                    targetH,
                    String(wcfg.lang),
                    String(job.title)
                );

                net::SecureHttpOptions qOpts;
                qOpts.ownerId = net::OWNER_INGAME_SYNC;
                qOpts.requestTimeoutMs = 3500;
                qOpts.keepAlive = false;

                char endpointUrl[192];
                snprintf(endpointUrl, sizeof(endpointUrl), "%s/api/v1/clients/%s/queue/async", baseServerUrl, syncClientId);

                char readyItemId[80] = "";
                bool enqueuedOk = false;
                int postHttpStatus = 0;
                {
                    auto qResp = net::SecureHttpClient::post(endpointUrl, "application/json", postBody, qOpts);
                    postHttpStatus = qResp.statusCode();
                    if (qResp.ok()) {
                        StaticJsonDocument<192> qFilter;
                        qFilter["queue_id"] = true;
                        qFilter["status"] = true;
                        qFilter["item_id"] = true;
                        qFilter["item"]["queue_id"] = true;
                        qFilter["item"]["status"] = true;
                        qFilter["item"]["item_id"] = true;

                        StaticJsonDocument<384> qDoc;
                        if (deserializeJson(qDoc, qResp.stream(), DeserializationOption::Filter(qFilter)) == DeserializationError::Ok) {
                            JsonVariantConst itemVar = qDoc.containsKey("item")
                                ? qDoc["item"].as<JsonVariantConst>()
                                : qDoc.as<JsonVariantConst>();
                            const char* qId = itemVar["queue_id"] | "";
                            const char* qStatus = itemVar["status"] | "";
                            const char* qItemId = itemVar["item_id"] | "";

                            if (qId[0] != '\0') {
                                copySafeCStr(activeQueueId, sizeof(activeQueueId), qId);
                                if ((strcmp(qStatus, "ready") == 0 || strcmp(qStatus, "playing") == 0) && qItemId[0] != '\0') {
                                    copySafeCStr(readyItemId, sizeof(readyItemId), qItemId);
                                }
                                enqueuedOk = true;
                            }
                        }
                    }
                }

                if (!enqueuedOk) {
                    LOGW("RetroFrontend", "Failed to enqueue In-Game Showcase for '%s' (%s): HTTP %d; keeping marquee.", job.game, job.system, postHttpStatus);
                    if (postHttpStatus >= 400) {
                        remoteSyncDirtyOnWorker = false;
                    } else {
                        cleanupActiveQueue();
                    }
                } else {
                    if (m_playSessionToken.load(std::memory_order_acquire) != job.token ||
                        m_workerStopRequested.load(std::memory_order_acquire)) {
                        cleanupActiveQueue();
                    } else {
                        // Wait for async preparation while marquee stays undisturbed on the DMD
                        uint32_t waitStartMs = millis();
                        bool endedWithoutStreamableItem = false;
                        snprintf(endpointUrl, sizeof(endpointUrl), "%s/api/v1/clients/%s/queue", baseServerUrl, syncClientId);
                        while (readyItemId[0] == '\0' &&
                               !m_workerStopRequested.load(std::memory_order_acquire) &&
                               m_playSessionToken.load(std::memory_order_acquire) == job.token &&
                               (millis() - waitStartMs) < INGAME_QUEUE_READY_TIMEOUT_MS) {
                            for (int w = 0; w < 15; ++w) {
                                if (m_workerStopRequested.load(std::memory_order_acquire) ||
                                    m_playSessionToken.load(std::memory_order_acquire) != job.token ||
                                    m_readyWakeupHint.exchange(false, std::memory_order_acq_rel)) {
                                    break;
                                }
                                vTaskDelay(pdMS_TO_TICKS(50));
                            }
                            if (m_workerStopRequested.load(std::memory_order_acquire) ||
                                m_playSessionToken.load(std::memory_order_acquire) != job.token) {
                                break;
                            }

                            bool keepPolling = true;
                            {
                                auto pResp = net::SecureHttpClient::get(endpointUrl, qOpts);
                                if (pResp.ok()) {
                                    StaticJsonDocument<128> pFilter;
                                    pFilter["items"][0]["queue_id"] = true;
                                    pFilter["items"][0]["status"] = true;
                                    pFilter["items"][0]["item_id"] = true;

                                    StaticJsonDocument<768> pDoc;
                                    if (deserializeJson(pDoc, pResp.stream(), DeserializationOption::Filter(pFilter)) == DeserializationError::Ok) {
                                        JsonArrayConst itemsArr = pDoc["items"].as<JsonArrayConst>();
                                        bool foundInQueue = false;
                                        for (JsonObjectConst it : itemsArr) {
                                            const char* itQid = it["queue_id"] | "";
                                            if (strcmp(itQid, activeQueueId) == 0) {
                                                foundInQueue = true;
                                                const char* st = it["status"] | "";
                                                const char* iid = it["item_id"] | "";
                                                if ((strcmp(st, "ready") == 0 || strcmp(st, "playing") == 0) && iid[0] != '\0') {
                                                    copySafeCStr(readyItemId, sizeof(readyItemId), iid);
                                                } else if (strcmp(st, "error") == 0) {
                                                    foundInQueue = false;
                                                }
                                                break;
                                            }
                                        }
                                        if (!foundInQueue) {
                                            keepPolling = false;
                                            endedWithoutStreamableItem = true;
                                        }
                                    }
                                }
                            }
                            if (!keepPolling) break;
                        }

                        if (readyItemId[0] == '\0' &&
                            !m_workerStopRequested.load(std::memory_order_acquire) &&
                            m_playSessionToken.load(std::memory_order_acquire) == job.token) {
                            if (endedWithoutStreamableItem) {
                                LOGW(
                                    "RetroFrontend",
                                    "In-Game Showcase preparation ended without streamable package for '%s' (%s); keeping marquee.",
                                    job.game,
                                    job.system
                                );
                            } else {
                                LOGW(
                                    "RetroFrontend",
                                    "In-Game Showcase preparation timed out after %u s for '%s' (%s); aborting remote worker and keeping marquee.",
                                    static_cast<unsigned>(INGAME_QUEUE_READY_TIMEOUT_MS / 1000U),
                                    job.game,
                                    job.system
                                );
                            }
                        }

                        // Stream ready item according to configured cycle mode (`once`, `loop`, `alternating`)
                        while (readyItemId[0] != '\0' &&
                               !m_workerStopRequested.load(std::memory_order_acquire) &&
                               m_playSessionToken.load(std::memory_order_acquire) == job.token) {
                            uint32_t estStreamMs =
                                (wcfg.includeCardIntro ? 8000U : 0U) +
                                (wcfg.jacketPanorama ? 5000U : 0U) +
                                (wcfg.marqueePanorama ? 5000U : 0U) +
                                (static_cast<uint32_t>(wcfg.snapshotPanoramaCount) * 5000U) +
                                (wcfg.showVideo ? (static_cast<uint32_t>(wcfg.videoMaxDurationS) * 1000U) : 0U);
                            if (estStreamMs < 12000U) estStreamMs = 12000U;

                            bool playedOk = streamReadyItemOnce(
                                baseServerUrl,
                                syncClientId,
                                readyItemId,
                                job.token,
                                targetW,
                                targetH,
                                estStreamMs
                            );
                            if (!playedOk ||
                                m_workerStopRequested.load(std::memory_order_acquire) ||
                                m_playSessionToken.load(std::memory_order_acquire) != job.token) {
                                break;
                            }

                            readWorkerCfg(wcfg);
                            if (strcasecmp(wcfg.cycleMode, "loop") == 0) {
                                continue;
                            } else if (strcasecmp(wcfg.cycleMode, "alternating") == 0) {
                                // Yield display back to Pixelcade marquee (TransitionMode::RESUME) for marqueeIntervalSec
                                m_wantsInGameDisplay.store(false, std::memory_order_release);
                                uint32_t pauseMs = static_cast<uint32_t>(wcfg.marqueeIntervalSec) * 1000U;
                                uint32_t pauseT0 = millis();
                                while (!m_workerStopRequested.load(std::memory_order_acquire) &&
                                       m_playSessionToken.load(std::memory_order_acquire) == job.token &&
                                       (millis() - pauseT0) < pauseMs) {
                                    vTaskDelay(pdMS_TO_TICKS(50));
                                }
                                continue;
                            } else {
                                // `once`: release display back to marquee and clean up queue item
                                break;
                            }
                        }
                        cleanupActiveQueue();
                        freeStreamBuffersSafely();
                    }
                }
            }
        }
        }();

        if (!hasPsram) {
            const bool shouldExitIdle = [&]() __attribute__((noinline)) -> bool {
                InGameJobSlot latestJob;
                readJobSlot(latestJob);
                ArtDownloadJobSlot latestArt;
                readArtJobSlot(latestArt);
                const bool brokerCheckSettled =
                    !(wcfg.mqttEnabled && wcfg.showcaseEnabled && wcfg.useShowcaseBroker) ||
                    currentShowcaseOnline ||
                    (consecutiveHealthFailures >= 2);
                const bool pendingCancel =
                    latestJob.cancelRemote && (latestJob.token != lastProcessedJobToken);
                const bool pendingArt =
                    latestArt.active && latestArt.reqId != 0U && (latestArt.reqId != lastProcessedArtReqId);
                return !latestJob.active &&
                       !pendingCancel &&
                       !pendingArt &&
                       brokerCheckSettled &&
                       lastCheckedCfgGen == m_cfgGen.load(std::memory_order_acquire) &&
                       !m_workerWakeLatch.exchange(false, std::memory_order_acq_rel);
            }();
            if (shouldExitIdle) {
                break;
            }
        }

        for (int slice = 0; slice < 4; ++slice) {
            if (m_workerStopRequested.load(std::memory_order_acquire) ||
                m_workerWakeLatch.exchange(false, std::memory_order_acq_rel)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    pixelcadeSession.reset();
    cleanupActiveQueue();
    freeStreamBuffersSafely();
#endif
}

void FrontendSyncEngine::resetSyncCardCycle() {
    m_wantsSyncCardDisplay.store(false, std::memory_order_release);
    m_hasSessionSyncMeta = false;
    m_syncCardsCompletedOnce = false;
    m_syncCardPhaseStartedMs = 0;
    m_lastSyncCardCycleEndedMs = 0;
    m_currentSyncCardPhase = SyncCardMode::None;
    m_sessionSyncMeta = SyncMetaPayloadPod{};
    if (m_inGameEngine) {
        m_inGameEngine->clearSyncMeta();
    }
}

void FrontendSyncEngine::updateSyncCardCycle() {
    if (!isGamePlaying || !m_hasSessionSyncMeta || !m_inGameEngine) {
        if (m_currentSyncCardPhase != SyncCardMode::None || m_wantsSyncCardDisplay.load(std::memory_order_acquire)) {
            m_currentSyncCardPhase = SyncCardMode::None;
            m_wantsSyncCardDisplay.store(false, std::memory_order_release);
            if (m_inGameEngine) {
                m_inGameEngine->setActiveCardMode(SyncCardMode::None);
            }
        }
        return;
    }

    // If a live DMDZ Showcase stream is currently occupying InGameShowcaseEngine, yield card mode to the stream
    if (m_wantsInGameDisplay.load(std::memory_order_acquire)) {
        if (m_currentSyncCardPhase != SyncCardMode::None) {
            m_currentSyncCardPhase = SyncCardMode::None;
            m_inGameEngine->setActiveCardMode(SyncCardMode::None);
            m_wantsSyncCardDisplay.store(false, std::memory_order_release);
            m_syncCardsCompletedOnce = true;
            m_lastSyncCardCycleEndedMs = millis();
        }
        return;
    }

    const bool canShowHof = mqttConfig.sync_show_hiscores && (m_sessionSyncMeta.hiscoreCount > 0U);
    const bool canShowRa = mqttConfig.sync_show_ra && (m_sessionSyncMeta.ra.valid != 0U) && (m_sessionSyncMeta.ra.total > 0U);
    if (!canShowHof && !canShowRa) {
        if (m_currentSyncCardPhase != SyncCardMode::None || m_wantsSyncCardDisplay.load(std::memory_order_acquire)) {
            m_currentSyncCardPhase = SyncCardMode::None;
            m_inGameEngine->setActiveCardMode(SyncCardMode::None);
            m_wantsSyncCardDisplay.store(false, std::memory_order_release);
        }
        return;
    }

    const uint32_t now = millis();
    const uint32_t cardDurMs = static_cast<uint32_t>(constrain(mqttConfig.sync_card_duration_sec, 4, 20)) * 1000UL;

    if (m_currentSyncCardPhase == SyncCardMode::None) {
        // Keep the initial Pixelcade marquee visible for at least 3.5s after game launch
        if (m_gameStartedAtMs != 0U && (now - m_gameStartedAtMs) < 3500UL) {
            return;
        }
        if (m_syncCardsCompletedOnce) {
            if (!mqttConfig.ingame_cycle_mode.equalsIgnoreCase("loop") &&
                !mqttConfig.ingame_cycle_mode.equalsIgnoreCase("alternating")) {
                return;
            }
            const uint32_t intervalMs = static_cast<uint32_t>(constrain(mqttConfig.ingame_marquee_interval_sec, 5, 600)) * 1000UL;
            if (m_lastSyncCardCycleEndedMs != 0U && (now - m_lastSyncCardCycleEndedMs) < intervalMs) {
                return;
            }
        }
        m_currentSyncCardPhase = canShowHof ? SyncCardMode::HallOfFame : SyncCardMode::RetroAchievements;
        m_syncCardPhaseStartedMs = now;
        m_inGameEngine->setActiveCardMode(m_currentSyncCardPhase);
        m_wantsSyncCardDisplay.store(true, std::memory_order_release);
        return;
    }

    if ((now - m_syncCardPhaseStartedMs) >= cardDurMs) {
        if (m_currentSyncCardPhase == SyncCardMode::HallOfFame && canShowRa) {
            m_currentSyncCardPhase = SyncCardMode::RetroAchievements;
            m_syncCardPhaseStartedMs = now;
            m_inGameEngine->setActiveCardMode(SyncCardMode::RetroAchievements);
            m_wantsSyncCardDisplay.store(true, std::memory_order_release);
        } else {
            m_currentSyncCardPhase = SyncCardMode::None;
            m_inGameEngine->setActiveCardMode(SyncCardMode::None);
            m_wantsSyncCardDisplay.store(false, std::memory_order_release);
            m_syncCardsCompletedOnce = true;
            m_lastSyncCardCycleEndedMs = now;
        }
    }
}

bool FrontendSyncEngine::handleCompanionEvent(const String& jsonPayload) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, jsonPayload) != DeserializationError::Ok) {
        return false;
    }
    const char* status = doc["status"] | "";
    if (strcmp(status, "sync_meta") == 0) {
        hasReceivedAnyEvent = true;
        if (!isGamePlaying || !m_inGameEngine) {
            return true;
        }
        SyncMetaPayloadPod meta{};
        copySafeCStr(meta.system, sizeof(meta.system), cleanSystemName(String(doc["system"] | "")).c_str());
        copySafeCStr(meta.game, sizeof(meta.game), doc["game"] | "");
        copySafeCStr(meta.title, sizeof(meta.title), doc["title"] | "");
        copySafeCStr(meta.lang, sizeof(meta.lang), m_systemLang.c_str());
        meta.showHiscores = mqttConfig.sync_show_hiscores ? 1U : 0U;
        meta.showRa = mqttConfig.sync_show_ra ? 1U : 0U;
        meta.cardDurationSec = static_cast<uint8_t>(constrain(mqttConfig.sync_card_duration_sec, 4, 20));

        if (doc["hiscores"].is<JsonArrayConst>()) {
            JsonArrayConst hsArr = doc["hiscores"].as<JsonArrayConst>();
            uint8_t idx = 0;
            for (JsonObjectConst hObj : hsArr) {
                if (idx >= 5U) break;
                const char* sc = hObj["score"] | "";
                if (sc[0] == '\0') continue;
                meta.hiscores[idx].rank = static_cast<uint8_t>(hObj["rank"] | (idx + 1U));
                copySafeCStr(meta.hiscores[idx].tag, sizeof(meta.hiscores[idx].tag), hObj["tag"] | "AAA");
                copySafeCStr(meta.hiscores[idx].score, sizeof(meta.hiscores[idx].score), sc);
                ++idx;
            }
            meta.hiscoreCount = idx;
        }

        if (doc["ra"].is<JsonObjectConst>()) {
            JsonObjectConst raObj = doc["ra"].as<JsonObjectConst>();
            const uint16_t total = static_cast<uint16_t>(raObj["total"] | 0);
            if (total > 0U) {
                meta.ra.unlocked = static_cast<uint16_t>(raObj["unlocked"] | 0);
                meta.ra.total = total;
                meta.ra.points = static_cast<uint32_t>(raObj["points"] | 0);
                meta.ra.hardcore = (raObj["hardcore"] | false) ? 1U : 0U;
                meta.ra.valid = 1U;
                if (raObj["recent"].is<JsonObjectConst>()) {
                    JsonObjectConst recObj = raObj["recent"].as<JsonObjectConst>();
                    copySafeCStr(meta.ra.recentTitle, sizeof(meta.ra.recentTitle), recObj["title"] | "");
                    meta.ra.recentPoints = static_cast<uint16_t>(recObj["pts"] | (recObj["points"] | 0));
                } else {
                    copySafeCStr(meta.ra.recentTitle, sizeof(meta.ra.recentTitle), raObj["recent_title"] | "");
                }
            }
        }

        if (meta.hiscoreCount == 0U && meta.ra.valid == 0U) {
            return true;
        }
        m_sessionSyncMeta = meta;
        m_hasSessionSyncMeta = true;
        m_syncCardsCompletedOnce = false;
        m_inGameEngine->publishSyncMeta(m_sessionSyncMeta);
        return true;
    }

    if (strcmp(status, "celebrate") == 0) {
        hasReceivedAnyEvent = true;
        if (!mqttConfig.sync_celebrate_events || !m_celebrationEngine) {
            return true;
        }
        SyncCelebrationPayloadPod celeb{};
        const char* kindStr = doc["kind"] | "achievement";
        celeb.kind = (strcasecmp(kindStr, "highscore") == 0)
            ? SyncCelebrationKind::HighScore
            : SyncCelebrationKind::Achievement;
        celeb.rank = static_cast<uint8_t>(constrain(static_cast<int>(doc["rank"] | 1), 1, 99));
        celeb.points = static_cast<uint16_t>(doc["points"] | (doc["pts"] | 0));
        celeb.hardcore = (doc["hardcore"] | false) ? 1U : 0U;
        copySafeCStr(celeb.system, sizeof(celeb.system), cleanSystemName(String(doc["system"] | "")).c_str());
        copySafeCStr(celeb.game, sizeof(celeb.game), doc["game"] | "");
        copySafeCStr(celeb.title, sizeof(celeb.title), doc["title"] | "");
        copySafeCStr(celeb.tag, sizeof(celeb.tag), doc["tag"] | "AAA");
        copySafeCStr(celeb.score, sizeof(celeb.score), doc["score"] | "");
        copySafeCStr(celeb.lang, sizeof(celeb.lang), m_systemLang.c_str());
        m_celebrationEngine->triggerCelebration(celeb);
        return true;
    }

    return false;
}

bool FrontendSyncEngine::tryPlayMarqueePath(const char* path) {
    if (!gif || !path || path[0] == '\0') return false;
    if (!gif->playGif(path)) {
        LOGW("RetroFrontend", "GifEngine rejected marquee asset %s; keeping fallback banner.", path);
        return false;
    }
    copySafeCStr(m_activeMarqueePath, sizeof(m_activeMarqueePath), path);
    if (message) {
        message->deactivate();
    }
    return true;
}

void FrontendSyncEngine::showCyanTitleBanner(const char* primaryTitle, const String& fallbackName) {
    if (gif) {
        gif->stop();
    }
    if (!message) return;

    String titleSource = (primaryTitle && primaryTitle[0] != '\0') ? String(primaryTitle) : fallbackName;
    String clean = formatDisplayTitle(titleSource);
    if (clean.length() == 0) {
        clean = formatDisplayTitle(fallbackName);
    }
    if (clean.length() == 0) {
        clean = "Unknown";
    }
    String textToShow = (clean.length() > 10) ? (String(" ") + clean + " ") : clean;
    MessageConfig cfg = { textToShow, 0x07FF, 1, clean.length() > 8 ? "rtl" : "none", 40, 0, 0 };
    message->displayMessage(cfg);
}

void FrontendSyncEngine::scheduleOrDispatchArtJob(
    const char* primaryFolder,
    const char* secondaryFolder,
    const char* query,
    const char* title,
    const char* localStem,
    uint32_t reqId,
    uint32_t negHash,
    bool isSystem,
    bool immediate
) {
    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    // Immediate dispatch on game launch ("playing") or on non-PSRAM boards (0-network offline SD scan).
    // During rapid wheel browsing on PSRAM boards, wait 140 ms for selection to settle before firing HTTPS GETs.
    if (immediate || !hasPsram) {
        m_hasPendingNetArt = false;
        dispatchAsyncArtJob(primaryFolder, secondaryFolder, query, title, localStem, reqId, negHash, isSystem);
        return;
    }

    copySafeCStr(m_pendingNetArtJob.primaryFolder, sizeof(m_pendingNetArtJob.primaryFolder), primaryFolder);
    copySafeCStr(m_pendingNetArtJob.secondaryFolder, sizeof(m_pendingNetArtJob.secondaryFolder), secondaryFolder);
    copySafeCStr(m_pendingNetArtJob.query, sizeof(m_pendingNetArtJob.query), query);
    copySafeCStr(m_pendingNetArtJob.title, sizeof(m_pendingNetArtJob.title), title);
    copySafeCStr(m_pendingNetArtJob.localStem, sizeof(m_pendingNetArtJob.localStem), localStem);
    m_pendingNetArtJob.reqId = reqId;
    m_pendingNetArtJob.negHash = negHash;
    m_pendingNetArtJob.isSystem = isSystem;
    m_pendingNetArtJob.active = true;
    m_pendingNetArtDueMs = millis() + 140U;
    m_hasPendingNetArt = true;
}

void FrontendSyncEngine::dispatchAsyncArtJob(
    const char* primaryFolder,
    const char* secondaryFolder,
    const char* query,
    const char* title,
    const char* localStem,
    uint32_t reqId,
    uint32_t negHash,
    bool isSystem
) {
#if defined(ESP32) && !defined(UNIT_TEST)
    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    if (!hasPsram) {
        // Non-PSRAM boards (`esp32dev`) NEVER perform live HTTPS/TLS downloads from GitHub:
        // artwork must be pre-cached onto the SD card via `tools/pixelcade_sync/pixelcade_sync.{sh,ps1}`.
        // Wake Core 0 for system variant SD lookup or when a local `/pixelcade/_catalog/<folder>.txt` exists.
        if (WebUiSyncEngine::isRequested() ||
            WebUiSyncEngine::isReadyForServing() ||
            NetworkBudget::isBulkTransferActive()) {
            return;
        }
        if (!isSystem) {
            bool hasLocalCatalog = false;
            {
                SdLockGuard guard(pdMS_TO_TICKS(10));
                if (guard) {
                    char catPath[96];
                    if (primaryFolder && primaryFolder[0] != '\0') {
                        snprintf(catPath, sizeof(catPath), "/pixelcade/_catalog/%s.txt", primaryFolder);
                        hasLocalCatalog = sd.exists(catPath);
                    }
                    if (!hasLocalCatalog && secondaryFolder && secondaryFolder[0] != '\0') {
                        snprintf(catPath, sizeof(catPath), "/pixelcade/_catalog/%s.txt", secondaryFolder);
                        hasLocalCatalog = sd.exists(catPath);
                    }
                }
            }
            if (!hasLocalCatalog) {
                return;
            }
        }
    } else if (WiFi.status() != WL_CONNECTED && !isSystem) {
        // Offline on PSRAM boards: still allow local SD `_catalog` resolution if present
        bool hasLocalCatalog = false;
        SdLockGuard guard(pdMS_TO_TICKS(10));
        if (guard && primaryFolder && primaryFolder[0] != '\0') {
            char catPath[96];
            snprintf(catPath, sizeof(catPath), "/pixelcade/_catalog/%s.txt", primaryFolder);
            hasLocalCatalog = sd.exists(catPath);
        }
        if (!hasLocalCatalog) {
            return;
        }
    }

    m_activeArtReqId.store(reqId, std::memory_order_seq_cst);
    m_artJobSeq.fetch_add(1U, std::memory_order_acq_rel);
    copySafeCStr(m_artJobSlot.primaryFolder, sizeof(m_artJobSlot.primaryFolder), primaryFolder);
    copySafeCStr(m_artJobSlot.secondaryFolder, sizeof(m_artJobSlot.secondaryFolder), secondaryFolder);
    copySafeCStr(m_artJobSlot.query, sizeof(m_artJobSlot.query), query);
    copySafeCStr(m_artJobSlot.title, sizeof(m_artJobSlot.title), title);
    copySafeCStr(m_artJobSlot.localStem, sizeof(m_artJobSlot.localStem), localStem);
    m_artJobSlot.reqId = reqId;
    m_artJobSlot.negHash = negHash;
    m_artJobSlot.isSystem = isSystem;
    m_artJobSlot.active = true;
    m_artJobSeq.fetch_add(1U, std::memory_order_release);

    m_workerWakeLatch.store(true, std::memory_order_release);
    ensureShowcaseWorkerRunning();
#else
    (void)primaryFolder;
    (void)secondaryFolder;
    (void)query;
    (void)title;
    (void)localStem;
    (void)reqId;
    (void)negHash;
    (void)isSystem;
#endif
}

void FrontendSyncEngine::handleGameEvent(const String& jsonPayload, uint32_t reqId) {
    if (jsonPayload.indexOf("\"sync_meta\"") != -1 || jsonPayload.indexOf("\"celebrate\"") != -1) {
        if (handleCompanionEvent(jsonPayload)) {
            return;
        }
    }
    StaticJsonDocument<768> doc;
    DeserializationError err = deserializeJson(doc, jsonPayload);
    if (err) {
        LOGE("RetroFrontend", "Failed to parse MQTT JSON payload: %s", err.c_str());
        return;
    }

    const char* status = doc["status"] | "";
    const char* gameRaw = doc["game"] | "";
    const char* titleRaw = doc["title"] | "";
    const char* systemRaw = doc["system"] | "";
    const char* typeRaw = doc["type"] | "";
    const char* romPathRaw = doc["rom_path"] | (doc["path"] | "");

    hasReceivedAnyEvent = true;

    if (strcmp(status, "stopped") == 0) {
        LOGI("RetroFrontend", "Received stopped event, releasing any In-Game Showcase and keeping last marquee.");
        cancelInGameShowcaseSession();
        resetSyncCardCycle();
        hasPendingEvent = false;
        m_hasPendingNetArt = false;
        return;
    }

    const bool isPlaying = (strcmp(status, "playing") == 0);
    if (!isPlaying) {
        // Menu navigation ("browsing" or system selection) -> 100% local/async marquee, cancel any previous in-game Showcase
        cancelInGameShowcaseSession();
        resetSyncCardCycle();
    }

    String rawSysResolved = extractSystemFromRomPath(systemRaw, romPathRaw);
    String cleanSystem = cleanSystemName(rawSysResolved);
    String cleanGame = cleanSystemName(String(gameRaw));

    if (!isPlaying && (strcmp(typeRaw, "system") == 0 || cleanGame.length() == 0 || cleanGame.equalsIgnoreCase(cleanSystem))) {
        handleSystemEvent(cleanSystem.length() > 0 ? cleanSystem : String(systemRaw), reqId);
        return;
    }

    isGamePlaying = true;
    waitingDisplayed = false;
    m_activeMarqueePath[0] = '\0';

    if (isPlaying && gameRaw[0] != '\0') {
        resetSyncCardCycle();
        m_gameStartedAtMs = (millis() == 0U) ? 1U : millis();
        startInGameShowcaseSession(cleanSystem, String(gameRaw), String(titleRaw));
    }

    String game = String(gameRaw);
    String primaryFolder = mapSystemToPixelcadeFolder(cleanSystem);
    String secondaryFolder = getSecondaryPixelcadeFolder(cleanSystem);

    char localStemBuf[96] = "";
    sanitizeLocalStem(gameRaw, localStemBuf, sizeof(localStemBuf));
    if (localStemBuf[0] == '\0' && titleRaw[0] != '\0') {
        sanitizeLocalStem(titleRaw, localStemBuf, sizeof(localStemBuf));
    }

    char cleanGameStripped[96] = "";
    engines::PixelcadeArtResolver::stripRomOrArtExtensionAndTags(gameRaw, cleanGameStripped, sizeof(cleanGameStripped));

    // Fast Core 1 local SD cache probe (< 3 ms, bounded 15 ms lock, checks only existing directories)
    char foundArtPath[128] = "";
    {
        SdLockGuard guard(pdMS_TO_TICKS(15));
        if (guard) {
            const char* candFolders[4] = {
                primaryFolder.c_str(),
                secondaryFolder.c_str(),
                cleanSystem.c_str(),
                nullptr
            };
            const char* candStems[3] = {
                localStemBuf,
                (cleanGameStripped[0] != '\0' && strcmp(cleanGameStripped, localStemBuf) != 0) ? cleanGameStripped : nullptr,
                (game.length() > 0 && game != localStemBuf && game != cleanGameStripped) ? game.c_str() : nullptr
            };

            for (size_t fi = 0U; fi < 3U && foundArtPath[0] == '\0'; ++fi) {
                const char* fName = candFolders[fi];
                if (!fName || fName[0] == '\0') continue;
                bool dupFolder = false;
                for (size_t prev = 0U; prev < fi; ++prev) {
                    if (candFolders[prev] && strcasecmp(candFolders[prev], fName) == 0) {
                        dupFolder = true;
                        break;
                    }
                }
                if (dupFolder) continue;

                char dirCandidates[2][80];
                snprintf(dirCandidates[0], sizeof(dirCandidates[0]), "/pixelcade/%s", fName);
                snprintf(dirCandidates[1], sizeof(dirCandidates[1]), "/%s", fName);

                for (size_t di = 0U; di < 2U && foundArtPath[0] == '\0'; ++di) {
                    if (!sd.exists(dirCandidates[di])) continue;
                    for (size_t si = 0U; si < 3U && foundArtPath[0] == '\0'; ++si) {
                        const char* stem = candStems[si];
                        if (!stem || stem[0] == '\0') continue;
                        char probe[128];
                        snprintf(probe, sizeof(probe), "%s/%s.png", dirCandidates[di], stem);
                        if (sd.exists(probe)) {
                            copySafeCStr(foundArtPath, sizeof(foundArtPath), probe);
                            break;
                        }
                        snprintf(probe, sizeof(probe), "%s/%s.gif", dirCandidates[di], stem);
                        if (sd.exists(probe)) {
                            copySafeCStr(foundArtPath, sizeof(foundArtPath), probe);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (reqId != currentRequestId) return;

    if (foundArtPath[0] != '\0' && tryPlayMarqueePath(foundArtPath)) {
        LOGI("RetroFrontend", "Playing cached Pixelcade art: %s", foundArtPath);
        return;
    }

    // Immediate non-blocking cyan banner on Core 1 so HUB75 renders on the very next frame!
    showCyanTitleBanner(titleRaw, game);

    const bool primaryEmpty = isKnownEmptyPixelcadeFolder(primaryFolder.c_str());
    const bool secondaryEmpty = secondaryFolder.isEmpty() || isKnownEmptyPixelcadeFolder(secondaryFolder.c_str());
    if (primaryEmpty && secondaryEmpty) {
        LOGD("RetroFrontend", "System folder '%s' has no Pixelcade catalog; keeping cyan game banner.", primaryFolder.c_str());
        return;
    }

    const char* effectivePrimary = !primaryEmpty ? primaryFolder.c_str() : secondaryFolder.c_str();
    const char* effectiveSecondary = (!primaryEmpty && !secondaryEmpty) ? secondaryFolder.c_str() : "";

    const uint32_t negHash = hashArtKey(effectivePrimary, localStemBuf[0] != '\0' ? localStemBuf : game.c_str());
    if (isMissingArtHashCached(negHash)) {
        LOGD("RetroFrontend", "Game marquee %s/%s in negative cache; keeping cyan banner.", effectivePrimary, game.c_str());
        return;
    }

    scheduleOrDispatchArtJob(
        effectivePrimary,
        effectiveSecondary,
        gameRaw,
        titleRaw,
        localStemBuf[0] != '\0' ? localStemBuf : game.c_str(),
        reqId,
        negHash,
        false,
        /*immediate=*/isPlaying
    );
}

void FrontendSyncEngine::handleSystemEvent(const String& systemId, uint32_t reqId) {
    isGamePlaying = true;
    waitingDisplayed = false;
    m_activeMarqueePath[0] = '\0';

    char sysStem[96] = "";
    String cleanedSys = cleanSystemName(systemId);
    sanitizeLocalStem(cleanedSys.length() > 0 ? cleanedSys.c_str() : systemId.c_str(), sysStem, sizeof(sysStem));
    for (size_t i = 0U; sysStem[i] != '\0'; ++i) {
        if (sysStem[i] >= 'A' && sysStem[i] <= 'Z') {
            sysStem[i] = static_cast<char>(sysStem[i] + ('a' - 'A'));
        } else if (sysStem[i] == ' ') {
            sysStem[i] = '_';
        }
    }

    char canonicalStem[96] = "";
    if (sysStem[0] != '\0') {
        snprintf(canonicalStem, sizeof(canonicalStem), "sys_%s", sysStem);
    }

    // Ultra-fast Core 1 local SD check (<= 6 probes, < 2 ms on 128 GB exFAT): check canonical cached path
    // and direct `<sysStem>` / `default-<sysStem>` so Core 1 NEVER stalls before rendering the cyan banner.
    // Full `sysVariants` SD & `_catalog` resolution runs asynchronously on Core 0.
    char foundArtPath[128] = "";
    {
        SdLockGuard guard(pdMS_TO_TICKS(10));
        if (guard && sysStem[0] != '\0') {
            char defaultStem[104] = "";
            snprintf(defaultStem, sizeof(defaultStem), "default-%s", sysStem);
            const char* fastStems[3] = {
                canonicalStem[0] != '\0' ? canonicalStem : nullptr,
                defaultStem,
                sysStem
            };
            const char* consoleDirs[2] = { "/pixelcade/console", "/console" };
            for (const char* cDir : consoleDirs) {
                if (foundArtPath[0] != '\0' || !sd.exists(cDir)) continue;
                for (const char* st : fastStems) {
                    if (!st || st[0] == '\0') continue;
                    char probe[128];
                    snprintf(probe, sizeof(probe), "%s/%s.png", cDir, st);
                    if (sd.exists(probe)) {
                        copySafeCStr(foundArtPath, sizeof(foundArtPath), probe);
                        break;
                    }
                    snprintf(probe, sizeof(probe), "%s/%s.gif", cDir, st);
                    if (sd.exists(probe)) {
                        copySafeCStr(foundArtPath, sizeof(foundArtPath), probe);
                        break;
                    }
                }
            }
        }
    }

    if (reqId != currentRequestId) return;

    if (foundArtPath[0] != '\0' && tryPlayMarqueePath(foundArtPath)) {
        LOGI("RetroFrontend", "Playing cached Pixelcade system art: %s", foundArtPath);
        return;
    }

    // Immediate non-blocking cyan system title banner on Core 1
    showCyanTitleBanner(nullptr, systemId);

    const uint32_t negHash = hashArtKey("console", canonicalStem[0] != '\0' ? canonicalStem : systemId.c_str());
    if (isMissingArtHashCached(negHash)) {
        LOGD("RetroFrontend", "System logo console/%s in negative cache; keeping cyan banner.", systemId.c_str());
        return;
    }

    scheduleOrDispatchArtJob(
        "console",
        "",
        systemId.c_str(),
        "",
        canonicalStem[0] != '\0' ? canonicalStem : "sys_default",
        reqId,
        negHash,
        true,
        /*immediate=*/false
    );
}

void FrontendSyncEngine::processWorkerArtJob(
    const ArtDownloadJobSlot& job,
    std::unique_ptr<net::SecureHttpSession>& session,
    uint32_t& lastArtActivityMs
) {
#if defined(ESP32) && !defined(UNIT_TEST)
    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    auto isCancelled = [&]() -> bool {
        return m_workerStopRequested.load(std::memory_order_acquire) ||
               (m_activeArtReqId.load(std::memory_order_acquire) != job.reqId) ||
               (!hasPsram &&
                (WebUiSyncEngine::isRequested() || WebUiSyncEngine::isReadyForServing()));
    };

    if (isCancelled()) return;

    if (job.isSystem && !m_mappingsLoaded) {
        systemMappings = loadMappingsFromSD();
        m_mappingsLoaded = true;
    }

    std::vector<SystemVariant> sysVariants;
    if (job.isSystem) {
        sysVariants = getSystemNameVariantsMapped(systemMappings, String(job.query));
    }

    char savedPath[128] = "";
    bool downloaded = false;
    bool allFoldersConfirmedMiss = true;

    // Fast offline SD check across all mapped `sysVariants` on Core 0 (zero Core 1 stall, zero network I/O)
    if (job.isSystem && !sysVariants.empty()) {
        const char* consoleDirs[2] = { "/pixelcade/console", "/console" };
        for (const char* cDir : consoleDirs) {
            if (downloaded || isCancelled()) break;
            bool dirExists = false;
            {
                SdLockGuard guard(pdMS_TO_TICKS(100));
                if (guard) {
                    dirExists = sd.exists(cDir);
                }
            }
            if (!dirExists) continue;

            for (size_t vi = 0U; vi < sysVariants.size() && !downloaded; ++vi) {
                if (isCancelled()) return;
                SdLockGuard guard(pdMS_TO_TICKS(100));
                if (!guard) break;
                char probe[128];
                snprintf(probe, sizeof(probe), "%s/%s.png", cDir, sysVariants[vi].name.c_str());
                if (sd.exists(probe)) {
                    copySafeCStr(savedPath, sizeof(savedPath), probe);
                    downloaded = true;
                    break;
                }
                snprintf(probe, sizeof(probe), "%s/%s.gif", cDir, sysVariants[vi].name.c_str());
                if (sd.exists(probe)) {
                    copySafeCStr(savedPath, sizeof(savedPath), probe);
                    downloaded = true;
                    break;
                }
            }
            esp_task_wdt_reset();
        }
    }

    const char* foldersToSearch[2] = {
        job.primaryFolder,
        (!job.isSystem && job.secondaryFolder[0] != '\0' &&
         strcasecmp(job.secondaryFolder, job.primaryFolder) != 0) ? job.secondaryFolder : nullptr
    };

    for (size_t fi = 0U; fi < 2U && !downloaded; ++fi) {
        const char* folder = foldersToSearch[fi];
        if (!folder || folder[0] == '\0') continue;
        if (isCancelled()) return;

        bool catalog404 = false;
        const bool hasCatalog = ensureFolderCatalogCachedOnWorker(folder, job.reqId, session, catalog404);
        lastArtActivityMs = millis() == 0U ? 1U : millis();
        if (isCancelled()) return;

        if (hasCatalog) {
            char catalogPath[96];
            snprintf(catalogPath, sizeof(catalogPath), "/pixelcade/_catalog/%s.txt", folder);

            char matchedFilename[160] = "";
            char fallbackFilename[160] = "";
            if (findMatchInCatalogFile(
                    catalogPath,
                    job,
                    job.isSystem ? &sysVariants : nullptr,
                    matchedFilename,
                    sizeof(matchedFilename),
                    fallbackFilename,
                    sizeof(fallbackFilename))) {
                if (isCancelled()) return;

                const char* candidates[2] = {
                    matchedFilename,
                    (fallbackFilename[0] != '\0' && strcasecmp(fallbackFilename, matchedFilename) != 0)
                        ? fallbackFilename
                        : nullptr
                };

                // 1. Check if `pixelcade_sync` already placed the matched file (or fallback) on the SD card (0 network / 0 TLS!)
                {
                    SdLockGuard guard(pdMS_TO_TICKS(100));
                    if (guard) {
                        for (size_t ci = 0U; ci < 2U && !downloaded; ++ci) {
                            const char* cand = candidates[ci];
                            if (!cand || cand[0] == '\0') continue;
                            char localCand1[224];
                            char localCand2[224];
                            const int n1 = snprintf(localCand1, sizeof(localCand1), "/pixelcade/%s/%s", folder, cand);
                            const int n2 = snprintf(localCand2, sizeof(localCand2), "/%s/%s", folder, cand);
                            if (n1 > 0 && static_cast<size_t>(n1) < sizeof(localCand1) &&
                                static_cast<size_t>(n1) < sizeof(savedPath) && sd.exists(localCand1)) {
                                copySafeCStr(savedPath, sizeof(savedPath), localCand1);
                                downloaded = true;
                                break;
                            }
                            if (n2 > 0 && static_cast<size_t>(n2) < sizeof(localCand2) &&
                                static_cast<size_t>(n2) < sizeof(savedPath) && sd.exists(localCand2)) {
                                copySafeCStr(savedPath, sizeof(savedPath), localCand2);
                                downloaded = true;
                                break;
                            }
                        }
                    }
                }
                if (downloaded) break;

                // 2. Live HTTPS download ONLY on PSRAM boards (`esp32s3`); non-PSRAM `esp32dev` stays strictly SD-only!
                if (!hasPsram || WiFi.status() != WL_CONNECTED) {
                    continue;
                }

                const char* saveDir = job.isSystem ? "console" : job.primaryFolder;
                bool folderConfirmedPermanentSkip = true;
                for (size_t ci = 0U; ci < 2U && !downloaded; ++ci) {
                    const char* cand = candidates[ci];
                    if (!cand || cand[0] == '\0') continue;
                    if (isCancelled()) return;

                    bool file404 = false;
                    bool permanentSkip = false;
                    if (downloadExactPixelcadeFile(
                            folder,
                            cand,
                            saveDir,
                            job.localStem,
                            job.reqId,
                            session,
                            savedPath,
                            sizeof(savedPath),
                            &file404,
                            &permanentSkip)) {
                        downloaded = true;
                        lastArtActivityMs = millis() == 0U ? 1U : millis();
                        break;
                    }
                    if (!file404 && !permanentSkip) {
                        folderConfirmedPermanentSkip = false;
                        break;
                    }
                }
                if (!downloaded && !folderConfirmedPermanentSkip) {
                    allFoldersConfirmedMiss = false;
                }
            }
            // If `hasCatalog` is true and `findMatchInCatalogFile` returned false, the folder
            // definitively does NOT contain this marquee — zero 404 probes needed!
        } else if (!catalog404 && hasPsram && WiFi.status() == WL_CONNECTED) {
            // Transient network/SD error fetching catalog on PSRAM board: bounded 2-candidate direct fallback
            allFoldersConfirmedMiss = false;
            const char* saveDir = job.isSystem ? "console" : job.primaryFolder;
            if (job.isSystem) {
                const size_t limit = (sysVariants.size() < 2U) ? sysVariants.size() : 2U;
                for (size_t vi = 0U; vi < limit && !downloaded; ++vi) {
                    if (isCancelled()) return;
                    String candPng = sysVariants[vi].name + ".png";
                    if (downloadExactPixelcadeFile(
                            "console",
                            candPng.c_str(),
                            saveDir,
                            job.localStem,
                            job.reqId,
                            session,
                            savedPath,
                            sizeof(savedPath),
                            nullptr)) {
                        downloaded = true;
                        break;
                    }
                }
            } else if (fi == 0U && job.query[0] != '\0') {
                char stripped[96] = "";
                engines::PixelcadeArtResolver::stripRomOrArtExtensionAndTags(job.query, stripped, sizeof(stripped));
                const char* base = (stripped[0] != '\0') ? stripped : job.query;
                char candName[112];
                snprintf(candName, sizeof(candName), "%s.png", base);
                if (downloadExactPixelcadeFile(
                        folder,
                        candName,
                        saveDir,
                        job.localStem,
                        job.reqId,
                        session,
                        savedPath,
                        sizeof(savedPath),
                        nullptr)) {
                    downloaded = true;
                }
            }
        }
    }

    if (isCancelled()) return;

    m_artResultSeq.fetch_add(1U, std::memory_order_acq_rel);
    copySafeCStr(m_artResultSlot.savedPath, sizeof(m_artResultSlot.savedPath), downloaded ? savedPath : "");
    m_artResultSlot.reqId = job.reqId;
    m_artResultSlot.missingHash = (!downloaded && allFoldersConfirmedMiss) ? job.negHash : 0U;
    m_artResultSlot.downloaded = downloaded;
    m_artResultSeq.fetch_add(1U, std::memory_order_release);
    m_artResultPending.store(true, std::memory_order_release);
#else
    (void)job;
    (void)session;
    (void)lastArtActivityMs;
#endif
}

bool FrontendSyncEngine::ensureFolderCatalogCachedOnWorker(
    const char* folder,
    uint32_t reqId,
    std::unique_ptr<net::SecureHttpSession>& session,
    bool& outDefinitive404
) {
    outDefinitive404 = false;
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!folder || folder[0] == '\0') {
        outDefinitive404 = true;
        return false;
    }
    if (isKnownEmptyPixelcadeFolder(folder)) {
        outDefinitive404 = true;
        return false;
    }

    const uint32_t catHash = hashArtKey("_catalog", folder);
    if (isMissingCatalogHashCached(catHash)) {
        outDefinitive404 = true;
        return false;
    }

    char finalPath[96];
    snprintf(finalPath, sizeof(finalPath), "/pixelcade/_catalog/%s.txt", folder);

    {
        SdLockGuard guard(pdMS_TO_TICKS(100));
        if (guard && sd.exists(finalPath)) {
            FsFile check = sd.open(finalPath, FILE_OPEN_READ);
            if (check) {
                const size_t sz = check.size();
                check.close();
                if (sz >= 4U) {
                    return true;
                }
            }
            sd.remove(finalPath);
        }
    }

    // Non-PSRAM boards (`esp32dev`) NEVER perform live HTTPS catalog downloads: SD-only!
    if (!hardwareHAL.capabilities().hasPsram || WiFi.status() != WL_CONNECTED) {
        outDefinitive404 = true;
        return false;
    }

    struct ArtNetBusyGuard {
        std::atomic<bool>& flag;
        bool engaged{true};
        explicit ArtNetBusyGuard(std::atomic<bool>& f) : flag(f) {
            flag.store(true, std::memory_order_seq_cst);
        }
        void release() {
            if (engaged) {
                engaged = false;
                flag.store(false, std::memory_order_release);
            }
        }
        ~ArtNetBusyGuard() { release(); }
    } netBusyGuard(m_artNetBusy);

    if (m_workerStopRequested.load(std::memory_order_acquire) ||
        m_activeArtReqId.load(std::memory_order_seq_cst) != reqId) {
        return false;
    }

    if (!session || session->isAborted() || session->state() == net::SessionState::Closed) {
        net::SecureHttpOptions opts;
        opts.ownerId = net::OWNER_ARTWORK;
        opts.followRedirects = true;
        session.reset(new (std::nothrow) net::SecureHttpSession(
            net::SecureHttpClient::session("raw.githubusercontent.com", 443, opts)));
        if (!session) return false;
    }

    String encFolder = urlEncodeSegment(folder);
    String urlPath = "/red77290/pixelcade/master/_catalog/" + encFolder + ".txt";
    LOGI("RetroFrontend", "Caching Pixelcade catalog %s on Core 0...", urlPath.c_str());

    auto resp = session->get(urlPath);
    if (m_workerStopRequested.load(std::memory_order_acquire) ||
        m_activeArtReqId.load(std::memory_order_acquire) != reqId ||
        session->isAborted()) {
        session->abort();
        session.reset();
        return false;
    }
    if (!resp.ok()) {
        if (resp.statusCode() == 404) {
            outDefinitive404 = true;
            recordMissingCatalogHash(catHash);
        }
        return false;
    }

    char tmpPath[104];
    snprintf(tmpPath, sizeof(tmpPath), "/pixelcade/_catalog/%s.tmp", folder);

    FsFile outFile{};
    {
        SdLockGuard guard(pdMS_TO_TICKS(500));
        if (!guard) {
            session->abort();
            session.reset();
            return false;
        }
        if (!sd.exists("/pixelcade")) sd.mkdir("/pixelcade");
        if (!sd.exists("/pixelcade/_catalog")) sd.mkdir("/pixelcade/_catalog");
        if (sd.exists(tmpPath)) sd.remove(tmpPath);
        outFile = sd.open(tmpPath, FILE_OPEN_WRITE);
        if (!outFile) {
            session->abort();
            session.reset();
            return false;
        }
    }

    Stream& stream = resp.stream();
    const size_t expectedLen = resp.contentLength();
    static constexpr size_t MAX_CATALOG_BYTES = 524288U;
    static constexpr size_t kCatalogChunkBytes = 1024U;
    std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[kCatalogChunkBytes]);
    if (!chunk) {
        (void)closeSdFileSafely(outFile, tmpPath, false);
        session->abort();
        session.reset();
        return false;
    }
    size_t totalWritten = 0U;
    uint32_t lastReadMs = millis();
    bool writeFailed = false;
    bool aborted = false;

    while (expectedLen == 0U || totalWritten < expectedLen) {
        if (m_workerStopRequested.load(std::memory_order_acquire) ||
            m_activeArtReqId.load(std::memory_order_acquire) != reqId ||
            session->isAborted()) {
            aborted = true;
            break;
        }

        int avail = stream.available();
        if (avail > 0) {
            size_t toRead = static_cast<size_t>(avail);
            if (toRead > kCatalogChunkBytes) toRead = kCatalogChunkBytes;
            if (expectedLen > 0U && (totalWritten + toRead) > expectedLen) {
                toRead = expectedLen - totalWritten;
            }
            const size_t nRead = stream.readBytes(reinterpret_cast<char*>(chunk.get()), toRead);
            if (nRead > 0U) {
                if ((totalWritten + nRead) > MAX_CATALOG_BYTES) {
                    writeFailed = true;
                    break;
                }
                {
                    SdLockGuard guard(pdMS_TO_TICKS(500));
                    if (!guard || outFile.write(chunk.get(), nRead) != nRead) {
                        writeFailed = true;
                        break;
                    }
                }
                totalWritten += nRead;
                lastReadMs = millis();
            }
        } else {
            if (!session->isConnected() && stream.available() <= 0) {
                break;
            }
            if ((millis() - lastReadMs) > 3500U) {
                writeFailed = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        esp_task_wdt_reset();
    }

    if (aborted || writeFailed) {
        session->abort();
        session.reset();
    } else {
        resp.consume();
    }
    netBusyGuard.release();

    const bool complete = !aborted && !writeFailed && totalWritten >= 4U &&
                          (expectedLen == 0U || totalWritten == expectedLen);
    if (!closeSdFileSafely(outFile, complete ? nullptr : tmpPath, complete)) {
        return false;
    }

    bool committed = false;
    if (complete) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            esp_task_wdt_reset();
            SdLockGuard guard(pdMS_TO_TICKS(500));
            if (guard) {
                if (sd.exists(finalPath)) sd.remove(finalPath);
                committed = sd.rename(tmpPath, finalPath);
                if (!committed && sd.exists(tmpPath)) {
                    sd.remove(tmpPath);
                }
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    return committed;
#else
    (void)folder;
    (void)reqId;
    (void)session;
    return false;
#endif
}

bool FrontendSyncEngine::findMatchInCatalogFile(
    const char* catalogPath,
    const ArtDownloadJobSlot& job,
    const std::vector<SystemVariant>* sysVariants,
    char* outMatchedFilename,
    size_t outCap,
    char* outFallbackFilename,
    size_t fallbackCap
) {
    if (outFallbackFilename && fallbackCap > 0U) outFallbackFilename[0] = '\0';
    if (!outMatchedFilename || outCap == 0U) return false;
    outMatchedFilename[0] = '\0';
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!catalogPath || catalogPath[0] == '\0') return false;

    // Heap-allocate scan state so `esp32dev` (`6144U` worker stack) uses 0 extra stack bytes!
    struct CatalogScanState {
        engines::PixelcadeQueryPlan plan{};
        char lineBuf[192] = "";
        char bestPngFile[160] = "";
        char bestGifFile[160] = "";
        int bestPngScore = 0;
        int bestGifScore = 0;
    };
    std::unique_ptr<CatalogScanState> st(new (std::nothrow) CatalogScanState());
    if (!st) return false;

    if (!job.isSystem) {
        engines::PixelcadeArtResolver::buildQueryPlan(job.query, job.title, st->plan);
    }

    auto evaluateLine = [&](const char* line, size_t len) -> bool {
        if (!line || len < 5U || line[0] == '#') return false;
        int sc = 0;
        if (job.isSystem && sysVariants) {
            sc = engines::PixelcadeArtResolver::scoreSystemCatalogEntry(line, *sysVariants);
        } else {
            sc = engines::PixelcadeArtResolver::scoreCatalogEntryWithPlan(line, st->plan);
        }
        if (sc <= 0) return false;

        const bool isPng = (strcasecmp(line + len - 4U, ".png") == 0);
        if (isPng) {
            if (sc > st->bestPngScore) {
                st->bestPngScore = sc;
                copySafeCStr(st->bestPngFile, sizeof(st->bestPngFile), line);
            }
        } else {
            if (sc > st->bestGifScore) {
                st->bestGifScore = sc;
                copySafeCStr(st->bestGifFile, sizeof(st->bestGifFile), line);
            }
        }
        // Fast-exit ONLY on top `.png` match (`>= 2002` for system variant #0 `.png`, `>= 1044` for exact `<rom>.png`)
        // so `.gif` NEVER preempts `.png` on the very next alphabetical line!
        return (st->bestPngScore >= 2002 || (!job.isSystem && st->bestPngScore >= 1044));
    };

    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    bool cancelled = false;
    bool scannedFromPsram = false;

    if (hasPsram) {
        if (!m_cachedCatalogPsram || m_cachedCatalogLen < 4U ||
            strcmp(m_cachedCatalogPath, catalogPath) != 0) {
            freeCatalogPsramCache();

            FsFile catFile{};
            size_t fileSize = 0U;
            {
                SdLockGuard guard(pdMS_TO_TICKS(300));
                if (guard && sd.exists(catalogPath)) {
                    catFile = sd.open(catalogPath, FILE_OPEN_READ);
                    if (catFile) {
                        fileSize = catFile.size();
                    }
                }
            }
            if (catFile) {
                static constexpr size_t kMaxPsramCatalogBytes = 384U * 1024U;
                static constexpr size_t kPsramLoadChunk = 16384U;
                if (fileSize >= 4U && fileSize <= kMaxPsramCatalogBytes) {
                    char* psBuf = static_cast<char*>(
                        heap_caps_malloc(fileSize + 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                    );
                    if (psBuf) {
                        size_t totalRead = 0U;
                        while (totalRead < fileSize) {
                            if (m_workerStopRequested.load(std::memory_order_acquire) ||
                                m_activeArtReqId.load(std::memory_order_acquire) != job.reqId) {
                                cancelled = true;
                                break;
                            }
                            const size_t want = ((fileSize - totalRead) < kPsramLoadChunk)
                                ? (fileSize - totalRead)
                                : kPsramLoadChunk;
                            int nRead = 0;
                            {
                                SdLockGuard guard(pdMS_TO_TICKS(300));
                                if (!guard) {
                                    cancelled = true;
                                    break;
                                }
                                nRead = catFile.read(reinterpret_cast<uint8_t*>(psBuf + totalRead), want);
                            }
                            if (nRead <= 0) break;
                            totalRead += static_cast<size_t>(nRead);
                            esp_task_wdt_reset();
                        }
                        if (!cancelled && totalRead == fileSize) {
                            psBuf[totalRead] = '\0';
                            m_cachedCatalogPsram = psBuf;
                            m_cachedCatalogLen = totalRead;
                            copySafeCStr(m_cachedCatalogPath, sizeof(m_cachedCatalogPath), catalogPath);
                        } else {
                            free(psBuf);
                        }
                    }
                }
                (void)closeSdFileSafely(catFile, nullptr, false);
            }
        }

        if (!cancelled && m_cachedCatalogPsram && m_cachedCatalogLen >= 4U &&
            strcmp(m_cachedCatalogPath, catalogPath) == 0) {
            scannedFromPsram = true;
            const char* buf = m_cachedCatalogPsram;
            const size_t total = m_cachedCatalogLen;
            size_t lineStart = 0U;
            uint32_t linesChecked = 0U;

            for (size_t pos = 0U; pos <= total; ++pos) {
                const bool atEnd = (pos == total);
                const char c = atEnd ? '\n' : buf[pos];
                if (c == '\n') {
                    size_t len = pos - lineStart;
                    if (len > 0U && buf[lineStart + len - 1U] == '\r') {
                        --len;
                    }
                    if (len > 0U && len + 1U < sizeof(st->lineBuf)) {
                        memcpy(st->lineBuf, buf + lineStart, len);
                        st->lineBuf[len] = '\0';
                        if (evaluateLine(st->lineBuf, len)) {
                            break;
                        }
                    }
                    lineStart = pos + 1U;
                    if ((++linesChecked & 511U) == 0U) {
                        esp_task_wdt_reset();
                        if (m_workerStopRequested.load(std::memory_order_acquire) ||
                            m_activeArtReqId.load(std::memory_order_acquire) != job.reqId) {
                            cancelled = true;
                            break;
                        }
                    }
                }
            }
            esp_task_wdt_reset();
        }
    }

    if (!cancelled && !scannedFromPsram) {
        static constexpr size_t kScanChunkBytes = 2048U;
        std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[kScanChunkBytes]);
        if (!chunk) return false;

        FsFile catFile{};
        {
            SdLockGuard guard(pdMS_TO_TICKS(300));
            if (!guard || !sd.exists(catalogPath)) return false;
            catFile = sd.open(catalogPath, FILE_OPEN_READ);
            if (!catFile) return false;
        }

        size_t lineLen = 0U;
        bool lineOverflow = false;
        while (true) {
            if (m_workerStopRequested.load(std::memory_order_acquire) ||
                m_activeArtReqId.load(std::memory_order_acquire) != job.reqId) {
                cancelled = true;
                break;
            }

            int nRead = 0;
            {
                SdLockGuard guard(pdMS_TO_TICKS(300));
                if (!guard) {
                    cancelled = true;
                    break;
                }
                nRead = catFile.read(chunk.get(), kScanChunkBytes);
            }
            if (nRead <= 0) break;

            for (int i = 0; i < nRead; ++i) {
                char c = static_cast<char>(chunk[static_cast<size_t>(i)]);
                if (c == '\r') continue;
                if (c == '\n') {
                    if (!lineOverflow && lineLen > 0U) {
                        st->lineBuf[lineLen] = '\0';
                        if (evaluateLine(st->lineBuf, lineLen)) {
                            nRead = 0;
                            break;
                        }
                    }
                    lineLen = 0U;
                    lineOverflow = false;
                } else if (!lineOverflow) {
                    if (lineLen + 1U < sizeof(st->lineBuf)) {
                        st->lineBuf[lineLen++] = c;
                    } else {
                        lineOverflow = true;
                    }
                }
            }
            esp_task_wdt_reset();
            if (nRead == 0) break;
        }

        if (!cancelled && !lineOverflow && lineLen > 0U) {
            st->lineBuf[lineLen] = '\0';
            (void)evaluateLine(st->lineBuf, lineLen);
        }

        (void)closeSdFileSafely(catFile, nullptr, false);
    }

    if (cancelled || (st->bestPngScore <= 0 && st->bestGifScore <= 0)) {
        outMatchedFilename[0] = '\0';
        return false;
    }

    if (st->bestPngScore >= st->bestGifScore && st->bestPngScore > 0) {
        copySafeCStr(outMatchedFilename, outCap, st->bestPngFile);
        if (outFallbackFilename && fallbackCap > 0U && st->bestGifScore > 0) {
            copySafeCStr(outFallbackFilename, fallbackCap, st->bestGifFile);
        }
    } else {
        copySafeCStr(outMatchedFilename, outCap, st->bestGifFile);
        if (outFallbackFilename && fallbackCap > 0U && st->bestPngScore > 0) {
            copySafeCStr(outFallbackFilename, fallbackCap, st->bestPngFile);
        }
    }
    return outMatchedFilename[0] != '\0';
#else
    (void)catalogPath;
    (void)job;
    (void)sysVariants;
    (void)outFallbackFilename;
    (void)fallbackCap;
    return false;
#endif
}

bool FrontendSyncEngine::downloadExactPixelcadeFile(
    const char* remoteFolder,
    const char* remoteFilename,
    const char* saveFolder,
    const char* localStem,
    uint32_t reqId,
    std::unique_ptr<net::SecureHttpSession>& session,
    char* outSavedPath,
    size_t outCap,
    bool* outDefinitive404,
    bool* outPermanentSkip
) {
    if (outSavedPath && outCap > 0U) outSavedPath[0] = '\0';
    if (outDefinitive404) *outDefinitive404 = false;
    if (outPermanentSkip) *outPermanentSkip = false;
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!hardwareHAL.capabilities().hasPsram) {
        return false;
    }
    if (!remoteFolder || !remoteFilename || !saveFolder || !localStem ||
        remoteFolder[0] == '\0' || remoteFilename[0] == '\0' || localStem[0] == '\0') {
        return false;
    }

    const size_t rLen = strlen(remoteFilename);
    const char* ext = ".png";
    if (rLen >= 4U && strcasecmp(remoteFilename + rLen - 4U, ".gif") == 0) {
        ext = ".gif";
    }

    char dirPath[64];
    char finalPath[128];
    char tmpPath[128];
    snprintf(dirPath, sizeof(dirPath), "/pixelcade/%s", saveFolder);
    const int fpLen = snprintf(finalPath, sizeof(finalPath), "%s/%s%s", dirPath, localStem, ext);
    const int tpLen = snprintf(tmpPath, sizeof(tmpPath), "%s/.dl_%u.tmp", dirPath, static_cast<unsigned>(reqId & 0xFFFFU));
    if (fpLen <= 0 || static_cast<size_t>(fpLen) >= sizeof(finalPath) ||
        tpLen <= 0 || static_cast<size_t>(tpLen) >= sizeof(tmpPath)) {
        return false;
    }

    struct ArtNetBusyGuard {
        std::atomic<bool>& flag;
        bool engaged{true};
        explicit ArtNetBusyGuard(std::atomic<bool>& f) : flag(f) {
            flag.store(true, std::memory_order_seq_cst);
        }
        void release() {
            if (engaged) {
                engaged = false;
                flag.store(false, std::memory_order_release);
            }
        }
        ~ArtNetBusyGuard() { release(); }
    } netBusyGuard(m_artNetBusy);

    if (m_workerStopRequested.load(std::memory_order_acquire) ||
        m_activeArtReqId.load(std::memory_order_seq_cst) != reqId) {
        return false;
    }

    if (!session || session->isAborted() || session->state() == net::SessionState::Closed) {
        net::SecureHttpOptions opts;
        opts.ownerId = net::OWNER_ARTWORK;
        opts.followRedirects = true;
        session.reset(new (std::nothrow) net::SecureHttpSession(
            net::SecureHttpClient::session("raw.githubusercontent.com", 443, opts)));
        if (!session) return false;
    }

    String encFolder = urlEncodeSegment(remoteFolder);
    String encFile = urlEncodeSegment(remoteFilename);
    String urlPath = "/red77290/pixelcade/master/" + encFolder + "/" + encFile;
    LOGI("RetroFrontend", "Downloading Pixelcade art %s -> %s", urlPath.c_str(), finalPath);

    auto resp = session->get(urlPath);
    if (m_workerStopRequested.load(std::memory_order_acquire) ||
        m_activeArtReqId.load(std::memory_order_acquire) != reqId ||
        session->isAborted()) {
        session->abort();
        session.reset();
        return false;
    }
    if (!resp.ok()) {
        if (resp.statusCode() == 404) {
            if (outDefinitive404) *outDefinitive404 = true;
            if (outPermanentSkip) *outPermanentSkip = true;
        }
        return false;
    }

    const bool hasPsram = hardwareHAL.capabilities().hasPsram;
    const size_t maxArtBytes = hasPsram ? (768U * 1024U) : (256U * 1024U);
    const size_t expectedLen = resp.contentLength();
    if (expectedLen > maxArtBytes) {
        LOGW("RetroFrontend", "Skipping oversized marquee (%u B > %u B): %s",
             static_cast<unsigned>(expectedLen), static_cast<unsigned>(maxArtBytes), remoteFilename);
        if (outPermanentSkip) *outPermanentSkip = true;
        session->abort();
        session.reset();
        return false;
    }

    static constexpr size_t kDownloadChunkBytes = 1024U;
    std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[kDownloadChunkBytes]);
    if (!chunk) {
        session->abort();
        session.reset();
        return false;
    }

    FsFile outFile{};
    {
        SdLockGuard guard(pdMS_TO_TICKS(500));
        if (!guard) {
            session->abort();
            session.reset();
            return false;
        }
        if (!sd.exists("/pixelcade")) sd.mkdir("/pixelcade");
        if (!sd.exists(dirPath)) sd.mkdir(dirPath);
        if (sd.exists(tmpPath)) sd.remove(tmpPath);
        outFile = sd.open(tmpPath, FILE_OPEN_WRITE);
        if (!outFile) {
            session->abort();
            session.reset();
            return false;
        }
    }

    Stream& stream = resp.stream();
    uint8_t hdrBuf[24];
    size_t hdrCollected = 0U;
    bool hdrValidated = false;
    size_t totalWritten = 0U;
    uint32_t lastReadMs = millis();
    bool failed = false;
    bool permanentReject = false;
    bool aborted = false;

    auto validateImageHeader = [&](const uint8_t* h, size_t n) -> bool {
        if (n < 10U) return false;
        // PNG magic + IHDR dimensions <= 512x512
        if (h[0] == 0x89U && h[1] == 'P' && h[2] == 'N' && h[3] == 'G' && n >= 24U) {
            uint32_t w = (static_cast<uint32_t>(h[16]) << 24) |
                         (static_cast<uint32_t>(h[17]) << 16) |
                         (static_cast<uint32_t>(h[18]) << 8) |
                         static_cast<uint32_t>(h[19]);
            uint32_t hg = (static_cast<uint32_t>(h[20]) << 24) |
                          (static_cast<uint32_t>(h[21]) << 16) |
                          (static_cast<uint32_t>(h[22]) << 8) |
                          static_cast<uint32_t>(h[23]);
            return (w > 0U && w <= 512U && hg > 0U && hg <= 512U);
        }
        // GIF87a / GIF89a + logical screen <= 512x512
        if (h[0] == 'G' && h[1] == 'I' && h[2] == 'F' && h[3] == '8' &&
            (h[4] == '7' || h[4] == '9') && h[5] == 'a') {
            uint16_t w = static_cast<uint16_t>(h[6]) | (static_cast<uint16_t>(h[7]) << 8);
            uint16_t hg = static_cast<uint16_t>(h[8]) | (static_cast<uint16_t>(h[9]) << 8);
            return (w > 0U && w <= 512U && hg > 0U && hg <= 512U);
        }
        return false;
    };

    while (expectedLen == 0U || totalWritten < expectedLen) {
        if (m_workerStopRequested.load(std::memory_order_acquire) ||
            m_activeArtReqId.load(std::memory_order_acquire) != reqId ||
            session->isAborted()) {
            aborted = true;
            break;
        }

        int avail = stream.available();
        if (avail > 0) {
            size_t toRead = static_cast<size_t>(avail);
            if (toRead > kDownloadChunkBytes) toRead = kDownloadChunkBytes;
            if (expectedLen > 0U && (totalWritten + toRead) > expectedLen) {
                toRead = expectedLen - totalWritten;
            }
            const size_t nRead = stream.readBytes(reinterpret_cast<char*>(chunk.get()), toRead);
            if (nRead > 0U) {
                if ((totalWritten + nRead) > maxArtBytes) {
                    failed = true;
                    permanentReject = true;
                    break;
                }
                if (!hdrValidated) {
                    const size_t copyN = ((hdrCollected + nRead) <= sizeof(hdrBuf))
                        ? nRead
                        : (sizeof(hdrBuf) - hdrCollected);
                    if (copyN > 0U) {
                        memcpy(hdrBuf + hdrCollected, chunk.get(), copyN);
                        hdrCollected += copyN;
                    }
                    if (hdrCollected >= sizeof(hdrBuf) ||
                        (expectedLen > 0U && (totalWritten + nRead) == expectedLen)) {
                        if (!validateImageHeader(hdrBuf, hdrCollected)) {
                            LOGW("RetroFrontend", "Rejected invalid/oversized image header in %s", remoteFilename);
                            failed = true;
                            permanentReject = true;
                            break;
                        }
                        hdrValidated = true;
                    }
                }
                {
                    SdLockGuard guard(pdMS_TO_TICKS(500));
                    if (!guard || outFile.write(chunk.get(), nRead) != nRead) {
                        failed = true;
                        break;
                    }
                }
                totalWritten += nRead;
                lastReadMs = millis();
            }
        } else {
            if (!session->isConnected() && stream.available() <= 0) {
                break;
            }
            if ((millis() - lastReadMs) > 3500U) {
                failed = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        esp_task_wdt_reset();
    }

    if (!hdrValidated && !aborted && !failed) {
        hdrValidated = validateImageHeader(hdrBuf, hdrCollected);
        if (!hdrValidated) {
            failed = true;
            permanentReject = true;
        }
    }
    if (permanentReject && outPermanentSkip) {
        *outPermanentSkip = true;
    }

    if (aborted || failed) {
        session->abort();
        session.reset();
    } else {
        resp.consume();
    }
    netBusyGuard.release();

    const bool complete = !aborted && !failed && hdrValidated && totalWritten >= 32U &&
                          (expectedLen == 0U || totalWritten == expectedLen);
    if (!closeSdFileSafely(outFile, complete ? nullptr : tmpPath, complete)) {
        return false;
    }

    bool committed = false;
    if (complete) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            esp_task_wdt_reset();
            SdLockGuard guard(pdMS_TO_TICKS(500));
            if (guard) {
                if (sd.exists(finalPath)) sd.remove(finalPath);
                committed = sd.rename(tmpPath, finalPath);
                if (!committed && sd.exists(tmpPath)) {
                    sd.remove(tmpPath);
                }
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (committed) {
        copySafeCStr(outSavedPath, outCap, finalPath);
        return true;
    }
    return false;
#else
    (void)remoteFolder;
    (void)remoteFilename;
    (void)saveFolder;
    (void)localStem;
    (void)reqId;
    (void)session;
    (void)outPermanentSkip;
    return false;
#endif
}

String FrontendSyncEngine::cleanSystemName(const String& rawSystem) {
    return engines::PixelcadeArtResolver::cleanSystemName(rawSystem);
}

String FrontendSyncEngine::formatDisplayTitle(const String& rawName) {
    return engines::PixelcadeArtResolver::formatDisplayTitle(rawName);
}

std::vector<FrontendSyncEngine::SystemVariant> FrontendSyncEngine::getSystemNameVariants(const String& rawSystem) {
    return engines::PixelcadeArtResolver::getSystemNameVariants(rawSystem);
}

std::vector<FrontendSyncEngine::SystemVariant> FrontendSyncEngine::getSystemNameVariantsMapped(
    const std::map<String, std::vector<String>>& mappings,
    const String& rawSystem) {
    return engines::PixelcadeArtResolver::getSystemNameVariantsMapped(mappings, rawSystem);
}

String FrontendSyncEngine::mapSystemToPixelcadeFolder(const String& systemId) {
    return engines::PixelcadeArtResolver::mapSystemToPixelcadeFolder(systemId);
}

String FrontendSyncEngine::getSecondaryPixelcadeFolder(const String& systemId) {
    return engines::PixelcadeArtResolver::getSecondaryPixelcadeFolder(systemId);
}

bool FrontendSyncEngine::isKnownEmptyPixelcadeFolder(const char* folder) {
    return engines::PixelcadeArtResolver::isKnownEmptyPixelcadeFolder(folder);
}

String FrontendSyncEngine::extractSystemFromRomPath(const char* currentSys, const char* romPath) {
    return engines::PixelcadeArtResolver::extractSystemFromRomPath(currentSys, romPath);
}

void FrontendSyncEngine::sanitizeLocalStem(const char* raw, char* out, size_t outCap) {
    engines::PixelcadeArtResolver::sanitizeLocalStem(raw, out, outCap);
}

void FrontendSyncEngine::normalizeMarqueeKey(const char* raw, char* out, size_t outCap) {
    engines::PixelcadeArtResolver::normalizeMarqueeKey(raw, out, outCap);
}

int FrontendSyncEngine::scoreCatalogEntry(
    const char* candidateFile,
    const char* rawGame,
    const char* rawTitle,
    const char* normGame,
    const char* normTitle
) {
    return engines::PixelcadeArtResolver::scoreCatalogEntry(candidateFile, rawGame, rawTitle, normGame, normTitle);
}

String FrontendSyncEngine::urlEncodeSegment(const char* raw) {
    return engines::PixelcadeArtResolver::urlEncodeSegment(raw);
}

uint32_t FrontendSyncEngine::hashArtKey(const char* folder, const char* name) {
    return engines::PixelcadeArtResolver::hashArtKey(folder, name);
}

bool FrontendSyncEngine::isMissingArtHashCached(uint32_t needle) const {
    if (needle == 0U) return false;
    for (size_t i = 0; i < MISSING_ART_CACHE_SIZE; ++i) {
        if (m_missingGameArtHashes[i] == needle) {
            return true;
        }
    }
    return false;
}

void FrontendSyncEngine::recordMissingArtHash(uint32_t needle) {
    if (needle == 0U || isMissingArtHashCached(needle)) {
        return;
    }
    m_missingGameArtHashes[m_missingGameArtWriteIdx % MISSING_ART_CACHE_SIZE] = needle;
    m_missingGameArtWriteIdx = static_cast<uint8_t>((m_missingGameArtWriteIdx + 1U) % MISSING_ART_CACHE_SIZE);
}

bool FrontendSyncEngine::isMissingCatalogHashCached(uint32_t needle) const {
    if (needle == 0U) return false;
    for (size_t i = 0; i < MISSING_CATALOG_CACHE_SIZE; ++i) {
        if (m_missingCatalogHashes[i] == needle) {
            return true;
        }
    }
    return false;
}

void FrontendSyncEngine::recordMissingCatalogHash(uint32_t needle) {
    if (needle == 0U || isMissingCatalogHashCached(needle)) {
        return;
    }
    m_missingCatalogHashes[m_missingCatalogWriteIdx % MISSING_CATALOG_CACHE_SIZE] = needle;
    m_missingCatalogWriteIdx = static_cast<uint8_t>((m_missingCatalogWriteIdx + 1U) % MISSING_CATALOG_CACHE_SIZE);
}

std::map<String, std::vector<String>> FrontendSyncEngine::loadMappingsFromSD() {
    std::map<String, std::vector<String>> mappings;

    const char* jsonPaths[] = { "/pixelcade/systems.json", "/systems.json" };
    bool loadedJson = false;

    SdLockGuard guard(pdMS_TO_TICKS(2000));
    if (!guard) return mappings;

    for (const char* p : jsonPaths) {
        if (sd.exists(p)) {
            FsFile f = sd.open(p, FILE_OPEN_READ);
            if (f) {
                SpiRamJsonDocument doc(8192);
                DeserializationError err = deserializeJson(doc, f);
                f.close();
                if (!err && doc.is<JsonObject>()) {
                    JsonObject root = doc.as<JsonObject>();
                    for (JsonPair kv : root) {
                        String key = String(kv.key().c_str());
                        key.trim();
                        key.toLowerCase();
                        std::vector<String> targets;
                        if (kv.value().is<JsonArray>()) {
                            for (JsonVariant val : kv.value().as<JsonArray>()) {
                                if (val.is<const char*>()) {
                                    targets.push_back(String(val.as<const char*>()));
                                }
                            }
                        } else if (kv.value().is<const char*>()) {
                            targets.push_back(String(kv.value().as<const char*>()));
                        }
                        if (key.length() > 0 && targets.size() > 0) {
                            mappings[key] = targets;
                        }
                    }
                    LOGI("RetroFrontend", "Loaded system mappings from SD JSON: %s (%u entries)", p, (unsigned int)mappings.size());
                    loadedJson = true;
                    break;
                }
            }
        }
    }

    if (!loadedJson) {
        const char* csvPaths[] = { "/pixelcade/console.csv", "/console.csv" };
        for (const char* p : csvPaths) {
            if (sd.exists(p)) {
                FsFile f = sd.open(p, FILE_OPEN_READ);
                if (f) {
                    while (f.available()) {
                        String line = f.readStringUntil('\n');
                        line.trim();
                        if (line.length() == 0 || line.startsWith("#")) continue;
                        int commaIdx = line.indexOf(',');
                        if (commaIdx != -1) {
                            String key = line.substring(0, commaIdx);
                            key.trim();
                            key.toLowerCase();
                            String rest = line.substring(commaIdx + 1);
                            std::vector<String> targets;
                            while (rest.length() > 0) {
                                int nextComma = rest.indexOf(',');
                                String target;
                                if (nextComma != -1) {
                                    target = rest.substring(0, nextComma);
                                    rest = rest.substring(nextComma + 1);
                                } else {
                                    target = rest;
                                    rest = "";
                                }
                                target.trim();
                                if (target.length() > 0) {
                                    targets.push_back(target);
                                }
                            }
                            if (key.length() > 0 && targets.size() > 0) {
                                mappings[key] = targets;
                            }
                        }
                    }
                    f.close();
                    LOGI("RetroFrontend", "Loaded system mappings from SD CSV: %s", p);
                    break;
                }
            }
        }
    }
    return mappings;
}
