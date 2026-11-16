#include "AgendaEngine.h"
#include "clocks/ClockSpriteBank.h"
#include "../core/drawing/IDrawingSurface.h"
#include "../core/I18n.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#if defined(ESP32) && !defined(UNIT_TEST)
#include "../core/SdLockGuard.h"
#include "../core/SDUtils.h"
#include "../core/net/SecureHttpClient.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if defined(ESP32)
#include <WiFi.h>
#include <esp_task_wdt.h>
#endif

#if defined(HARDWARE_PROFILE_ESP32_DEV)
#pragma GCC optimize("Os")
#endif

namespace {

void drawClippedText5x7(IDrawingSurface* surface, const char* text, int16_t startX, int16_t y,
                        int16_t clipMinX, int16_t clipMaxX, uint16_t color) {
    if (!surface || !text || *text == '\0' || clipMaxX <= clipMinX) return;
    constexpr int16_t charW = 6;
    int firstIdx = 0;
    if (startX < (clipMinX - charW)) {
        firstIdx = (clipMinX - charW - startX) / charW;
        if (firstIdx < 0) firstIdx = 0;
    }
    const int len = static_cast<int>(strlen(text));
    for (int i = firstIdx; i < len; ++i) {
        int16_t cx = startX + static_cast<int16_t>(i * charW);
        if (cx >= clipMaxX) break;
        if (cx + charW <= clipMinX) continue;
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= ' ') continue;
        surface->drawChar(cx, y, c, color, color, 1);
    }
}

void drawTinyDigit3x5(IDrawingSurface* surface, int16_t x, int16_t y, char c, uint16_t color) {
    static const uint8_t kGlyphs[12][5] = {
        {0x07, 0x05, 0x05, 0x05, 0x07}, // 0
        {0x02, 0x06, 0x02, 0x02, 0x07}, // 1
        {0x07, 0x01, 0x07, 0x04, 0x07}, // 2
        {0x07, 0x01, 0x07, 0x01, 0x07}, // 3
        {0x05, 0x05, 0x07, 0x01, 0x01}, // 4
        {0x07, 0x04, 0x07, 0x01, 0x07}, // 5
        {0x07, 0x04, 0x07, 0x05, 0x07}, // 6
        {0x07, 0x01, 0x02, 0x02, 0x02}, // 7
        {0x07, 0x05, 0x07, 0x05, 0x07}, // 8
        {0x07, 0x05, 0x07, 0x01, 0x07}, // 9
        {0x00, 0x02, 0x00, 0x02, 0x00}, // :
        {0x00, 0x00, 0x07, 0x00, 0x00}  // -
    };
    int idx = -1;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == ':') idx = 10;
    else if (c == '-') idx = 11;
    if (idx < 0) return;

    for (int16_t r = 0; r < 5; ++r) {
        uint8_t bits = kGlyphs[idx][r];
        for (int16_t b = 0; b < 3; ++b) {
            if ((bits >> (2 - b)) & 1U) {
                surface->drawPixel(x + b, y + r, color);
            }
        }
    }
}

void drawTinyTimeString3x5(IDrawingSurface* surface, int16_t x, int16_t y, const char* str, uint16_t color) {
    if (!surface || !str) return;
    int16_t cx = x;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] >= '0' && str[i] <= '9') {
            drawTinyDigit3x5(surface, cx, y, str[i], color);
            cx += 4;
        } else if (str[i] == ':') {
            drawTinyDigit3x5(surface, cx, y, ':', color);
            cx += 2;
        } else {
            surface->drawChar(cx, y - 1, str[i], color, color, 1);
            cx += 6;
        }
    }
}

int32_t localDayIndex(uint32_t epochUtc, int32_t tzOffsetSec) {
    int64_t localSec = static_cast<int64_t>(epochUtc) + static_cast<int64_t>(tzOffsetSec);
    return static_cast<int32_t>((localSec >= 0) ? (localSec / 86400LL) : ((localSec - 86399LL) / 86400LL));
}

} // namespace

AgendaEngine::AgendaEngine() {
    m_geometry.width = 128;
    m_geometry.height = 32;
    m_geometry.layoutClass = LayoutClass::WIDE;
    m_configCrc32 = AgendaIcsParser::computeConfigCrc32(m_icsUrl1, m_icsUrl2, m_horizonDays);
}

AgendaEngine::~AgendaEngine() {
#if defined(ESP32) && !defined(UNIT_TEST)
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_AGENDA);
#endif
}

uint16_t AgendaEngine::parseHexColor565(const char* hex, uint16_t fallback) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return fallback;
    char* endPtr = nullptr;
    unsigned long rgb = strtoul(hex + 1, &endPtr, 16);
    if (!endPtr || *endPtr != '\0') return fallback;
    uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(rgb & 0xFF);
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void AgendaEngine::applyConfig(const EngineConfig* config) {
    if (!config) return;

    String instId = config->getString("instance_id", "");
    if (instId.length() > 0) {
        strncpy(m_instanceId, instId.c_str(), sizeof(m_instanceId) - 1);
        m_instanceId[sizeof(m_instanceId) - 1] = '\0';
    }

    String u1 = config->getString("ics_url_1", "");
    String u2 = config->getString("ics_url_2", "");
    AgendaIcsParser::normalizeIcsUrl(u1.c_str(), m_icsUrl1, sizeof(m_icsUrl1));
    AgendaIcsParser::normalizeIcsUrl(u2.c_str(), m_icsUrl2, sizeof(m_icsUrl2));

    String l1 = config->getString("ics_label_1", "Perso");
    String l2 = config->getString("ics_label_2", "Pro");
    AgendaIcsParser::normalizeIcsText(l1.c_str(), m_icsLabel1, sizeof(m_icsLabel1));
    AgendaIcsParser::normalizeIcsText(l2.c_str(), m_icsLabel2, sizeof(m_icsLabel2));
    if (m_icsLabel1[0] == '\0') strcpy(m_icsLabel1, "Perso");
    if (m_icsLabel2[0] == '\0') strcpy(m_icsLabel2, "Pro");

    String c1 = config->getString("ics_color_1", "#00E5FF");
    String c2 = config->getString("ics_color_2", "#FFB300");
    m_icsColor1 = parseHexColor565(c1.c_str(), 0x073F);
    m_icsColor2 = parseHexColor565(c2.c_str(), 0xFD80);

    int ttl = config->getInt("cache_ttl_min", 60);
    if (ttl < 15) ttl = 15;
    if (ttl > 1440) ttl = 1440;
    m_cacheTtlMin = static_cast<uint16_t>(ttl);

    int hor = config->getInt("horizon_days", 7);
    if (hor < 1) hor = 1;
    if (hor > 30) hor = 30;
    m_horizonDays = static_cast<uint8_t>(hor);

    int shown = config->getInt("events_shown", 3);
    if (shown < 1) shown = 1;
    if (shown > 6) shown = 6;
    m_eventsShown = static_cast<uint8_t>(shown);

    int imm = config->getInt("imminent_min", 15);
    if (imm < 0) imm = 0;
    if (imm > 60) imm = 60;
    m_imminentMin = static_cast<uint8_t>(imm);

    String emp = config->getString("empty_behavior", "chill");
    m_emptyChill = !emp.equalsIgnoreCase("skip");
    m_funSprites = config->getBool("fun_sprites", true);

    uint32_t newCrc = AgendaIcsParser::computeConfigCrc32(m_icsUrl1, m_icsUrl2, m_horizonDays);
    if (newCrc != m_configCrc32) {
        m_configCrc32 = newCrc;
        m_hasValidSdCache = false;
        m_lastFetchEpoch = 0;
    }
}

uint32_t AgendaEngine::currentEpoch() const {
    if (m_testNowEpoch > 0) return m_testNowEpoch;
    time_t now = time(nullptr);
    if (now > 1600000000) {
        return static_cast<uint32_t>(now);
    }
    return 0;
}

void AgendaEngine::updateLocalTzOffset() {
    time_t now = time(nullptr);
    if (now <= 1600000000) return;
    struct tm locTm{};
    struct tm utcTm{};
    localtime_r(&now, &locTm);
    gmtime_r(&now, &utcTm);
    int32_t dayDiff = (locTm.tm_year - utcTm.tm_year) * 365 + (locTm.tm_yday - utcTm.tm_yday);
    if (dayDiff > 1) dayDiff = 1;
    if (dayDiff < -1) dayDiff = -1;
    m_localTzOffsetSec = dayDiff * 86400 +
                         (locTm.tm_hour - utcTm.tm_hour) * 3600 +
                         (locTm.tm_min - utcTm.tm_min) * 60;
}

void AgendaEngine::buildCacheFilePath(char* outPath, size_t outSize, bool tempFile) const {
    char safeId[24]{};
    size_t w = 0;
    for (size_t i = 0; m_instanceId[i] != '\0' && w + 1 < sizeof(safeId); ++i) {
        char c = m_instanceId[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            safeId[w++] = c;
        }
    }
    if (w == 0) strcpy(safeId, "main");
    snprintf(outPath, outSize, tempFile ? "/cache/agenda_%s.bin.tmp" : "/cache/agenda_%s.bin", safeId);
}

bool AgendaEngine::serializeCacheToBuffer(uint8_t* outBuf, size_t maxLen, const AgendaEventPod* events, uint8_t count, uint32_t fetchEpoch, size_t& outWritten) const {
    outWritten = 0;
    if (count > AGENDA_MAX_CACHED_EVENTS) count = AGENDA_MAX_CACHED_EVENTS;
    size_t totalBytes = sizeof(AgendaCacheHeader) + static_cast<size_t>(count) * sizeof(AgendaEventPod);
    if (!outBuf || maxLen < totalBytes) return false;

    AgendaCacheHeader hdr{};
    hdr.magic = AGENDA_CACHE_MAGIC;
    hdr.version = AGENDA_CACHE_VERSION;
    hdr.recordCount = count;
    hdr.reserved = 0;
    hdr.configCrc32 = m_configCrc32;
    hdr.lastFetchEpoch = fetchEpoch;
    hdr.payloadCrc32 = (count > 0 && events)
        ? AgendaIcsParser::computeCrc32(events, static_cast<size_t>(count) * sizeof(AgendaEventPod))
        : 0U;
    hdr.schemaFlags = 0;

    memcpy(outBuf, &hdr, sizeof(AgendaCacheHeader));
    if (count > 0 && events) {
        memcpy(outBuf + sizeof(AgendaCacheHeader), events, static_cast<size_t>(count) * sizeof(AgendaEventPod));
    }
    outWritten = totalBytes;
    return true;
}

bool AgendaEngine::loadHeaderFromBuffer(const uint8_t* buf, size_t len) {
    if (!buf || len < sizeof(AgendaCacheHeader)) return false;
    AgendaCacheHeader hdr{};
    memcpy(&hdr, buf, sizeof(AgendaCacheHeader));
    if (hdr.magic != AGENDA_CACHE_MAGIC ||
        hdr.version != AGENDA_CACHE_VERSION ||
        hdr.recordCount > AGENDA_MAX_CACHED_EVENTS ||
        hdr.configCrc32 != m_configCrc32) {
        return false;
    }
    m_lastFetchEpoch = hdr.lastFetchEpoch;
    m_hasValidSdCache = true;
    return true;
}

bool AgendaEngine::loadActiveEventsFromBuffer(const uint8_t* buf, size_t len, uint32_t nowEpoch) {
    if (!buf || len < sizeof(AgendaCacheHeader)) return false;
    AgendaCacheHeader hdr{};
    memcpy(&hdr, buf, sizeof(AgendaCacheHeader));
    if (hdr.magic != AGENDA_CACHE_MAGIC ||
        hdr.version != AGENDA_CACHE_VERSION ||
        hdr.recordCount > AGENDA_MAX_CACHED_EVENTS ||
        hdr.configCrc32 != m_configCrc32) {
        return false;
    }

    size_t payloadBytes = static_cast<size_t>(hdr.recordCount) * sizeof(AgendaEventPod);
    if (len < sizeof(AgendaCacheHeader) + payloadBytes) return false;

    const uint8_t* payloadPtr = buf + sizeof(AgendaCacheHeader);
    uint32_t calcCrc = (hdr.recordCount > 0) ? AgendaIcsParser::computeCrc32(payloadPtr, payloadBytes) : 0U;
    if (calcCrc != hdr.payloadCrc32) {
        return false;
    }

    m_lastFetchEpoch = hdr.lastFetchEpoch;
    m_hasValidSdCache = true;
    m_activeCount = 0;

    const AgendaEventPod* records = reinterpret_cast<const AgendaEventPod*>(payloadPtr);
    for (uint8_t i = 0; i < hdr.recordCount && m_activeCount < MAX_ACTIVE_EVENTS; ++i) {
        // Locally filter out expired events without any network round-trip
        if (nowEpoch == 0 || records[i].endEpoch > nowEpoch) {
            m_activeEvents[m_activeCount++] = records[i];
        }
    }
    return true;
}

bool AgendaEngine::probeSdCacheHeaderOnly() {
#if defined(ESP32) && !defined(PIO_UNIT_TESTING)
    char path[64]{};
    buildCacheFilePath(path, sizeof(path), false);
    SdLockGuard guard(pdMS_TO_TICKS(50));
    if (!guard.isLocked()) return false;
    if (!sd.exists(path)) return false;
    FsFile f = sd.open(path, FILE_OPEN_READ);
    if (!f) return false;
    uint8_t hdrBytes[sizeof(AgendaCacheHeader)]{};
    int n = f.read(hdrBytes, sizeof(hdrBytes));
    f.close();
    if (n != static_cast<int>(sizeof(AgendaCacheHeader))) return false;
    return loadHeaderFromBuffer(hdrBytes, sizeof(hdrBytes));
#else
    return m_hasValidSdCache;
#endif
}

bool AgendaEngine::loadUpcomingFromSdCache(uint32_t nowEpoch) {
#if defined(ESP32) && !defined(PIO_UNIT_TESTING)
    char path[64]{};
    buildCacheFilePath(path, sizeof(path), false);
    SdLockGuard guard(pdMS_TO_TICKS(50));
    if (!guard.isLocked()) return false;
    if (!sd.exists(path)) return false;
    FsFile f = sd.open(path, FILE_OPEN_READ);
    if (!f) return false;
    uint8_t fileBuf[sizeof(AgendaCacheHeader) + AGENDA_MAX_CACHED_EVENTS * sizeof(AgendaEventPod)]{};
    int n = f.read(fileBuf, sizeof(fileBuf));
    f.close();
    if (n < static_cast<int>(sizeof(AgendaCacheHeader))) return false;
    return loadActiveEventsFromBuffer(fileBuf, static_cast<size_t>(n), nowEpoch);
#else
    // In native unit tests, filter in-place on m_activeEvents
    uint8_t dst = 0;
    for (uint8_t i = 0; i < m_activeCount; ++i) {
        if (nowEpoch == 0 || m_activeEvents[i].endEpoch > nowEpoch) {
            if (dst != i) m_activeEvents[dst] = m_activeEvents[i];
            dst++;
        }
    }
    m_activeCount = dst;
    return m_hasValidSdCache;
#endif
}

bool AgendaEngine::writeEventsToSdCache(const AgendaEventPod* events, uint8_t count, uint32_t fetchEpoch) {
#if defined(ESP32) && !defined(PIO_UNIT_TESTING)
    uint8_t fileBuf[sizeof(AgendaCacheHeader) + AGENDA_MAX_CACHED_EVENTS * sizeof(AgendaEventPod)]{};
    size_t totalBytes = 0;
    if (!serializeCacheToBuffer(fileBuf, sizeof(fileBuf), events, count, fetchEpoch, totalBytes)) {
        return false;
    }

    char tmpPath[64]{};
    char finalPath[64]{};
    buildCacheFilePath(tmpPath, sizeof(tmpPath), true);
    buildCacheFilePath(finalPath, sizeof(finalPath), false);

    SdLockGuard guard(pdMS_TO_TICKS(50));
    if (!guard.isLocked()) return false;
    if (!sd.exists("/cache")) {
        sd.mkdir("/cache");
    }
    if (sd.exists(tmpPath)) {
        sd.remove(tmpPath);
    }
    FsFile f = sd.open(tmpPath, FILE_OPEN_WRITE);
    if (!f) return false;
    size_t written = f.write(fileBuf, totalBytes);
    f.flush();
    f.close();
    if (written != totalBytes) {
        sd.remove(tmpPath);
        return false;
    }
    if (sd.exists(finalPath)) {
        sd.remove(finalPath);
    }
    if (!sd.rename(tmpPath, finalPath)) {
        sd.remove(tmpPath);
        return false;
    }
    m_lastFetchEpoch = fetchEpoch;
    m_hasValidSdCache = true;
    return true;
#else
    injectEventsForTest(events, count, fetchEpoch);
    return true;
#endif
}

void AgendaEngine::injectEventsForTest(const AgendaEventPod* events, uint8_t count, uint32_t fetchEpoch) {
    if (count > MAX_ACTIVE_EVENTS) count = MAX_ACTIVE_EVENTS;
    m_activeCount = count;
    for (uint8_t i = 0; i < count; ++i) {
        m_activeEvents[i] = events[i];
    }
    m_lastFetchEpoch = fetchEpoch;
    m_hasValidSdCache = true;
}

EngineError AgendaEngine::initialize(EngineContext* context, const EngineConfig* config) {
    if (context) {
        m_geometry = context->getGeometry();
    }
    if (config) {
        applyConfig(config);
    }
    updateLocalTzOffset();
    probeSdCacheHeaderOnly();
    return EngineError::OK;
}

void AgendaEngine::onConfigChanged(const EngineConfig* config) {
    applyConfig(config);
    probeSdCacheHeaderOnly();
    requestRedraw();
}

void AgendaEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_geometry = geometry;
    recomputeSlideScrollBounds(geometry.width);
    requestRedraw();
}

bool AgendaEngine::needsTlsFetch() const {
    if (m_icsUrl1[0] == '\0' && m_icsUrl2[0] == '\0') {
        return false;
    }
    if (!m_hasValidSdCache || m_lastFetchEpoch == 0) {
        return true;
    }
    uint32_t now = currentEpoch();
    if (now == 0) {
        return false; // NTP not yet synced; serve cached events from SD
    }
    uint32_t ttlSec = static_cast<uint32_t>(m_cacheTtlMin) * 60U;
    if (now >= m_lastFetchEpoch && (now - m_lastFetchEpoch) < ttlSec) {
        return false; // Cache is fresh (< TTL): 0 TLS call, 8-bit maintained, 0 ms blanking!
    }
    return true;
}

void AgendaEngine::prefetchData() {
    if (!needsTlsFetch()) return;
#if defined(ESP32) && !defined(PIO_UNIT_TESTING)
    if (WiFi.status() != WL_CONNECTED) return;
    if (!NetworkBudget::canStartTlsSession()) {
        LOGW("Agenda", "Skipping .ics prefetch: TLS budget admission denied.");
        return;
    }

    updateLocalTzOffset();
    uint32_t now = currentEpoch();
    if (now == 0) return;

    // Stack-only bounded event array (12 * 96 = 1,152 bytes on stack; 0 bytes on heap)
    AgendaEventPod top[AGENDA_MAX_CACHED_EVENTS]{};
    size_t topCount = 0;
    bool anySuccess = false;

    const char* urls[2] = { m_icsUrl1, m_icsUrl2 };
    const char* labels[2] = { m_icsLabel1, m_icsLabel2 };
    const uint16_t colors[2] = { m_icsColor1, m_icsColor2 };

    net::ParsedUrl parsed[2];
    bool validUrl[2] = { false, false };
    for (int i = 0; i < 2; ++i) {
        if (urls[i][0] != '\0') {
            validUrl[i] = net::ParsedUrl::parse(String(urls[i]), parsed[i]);
        }
    }

    net::SecureHttpOptions opts;
    opts.requestTimeoutMs = 7000;
    opts.handshakeTimeoutSec = 8;
    opts.keepAlive = true;
    opts.followRedirects = true;
    opts.ownerId = net::OWNER_AGENDA;

    // Keep-Alive consolidation: if both .ics URLs share the same host & port, reuse 1 TLS session!
    const bool shareSession = validUrl[0] && validUrl[1] &&
                              parsed[0].host.equalsIgnoreCase(parsed[1].host) &&
                              parsed[0].port == parsed[1].port &&
                              parsed[0].isHttps == parsed[1].isHttps;

    if (shareSession) {
        net::SecureHttpSession session = net::SecureHttpClient::session(parsed[0].host.c_str(), parsed[0].port, opts);
        session.setHttps(parsed[0].isHttps);
        for (uint8_t idx = 0; idx < 2; ++idx) {
            esp_task_wdt_reset();
            net::SecureHttpResponse resp = session.get(parsed[idx].path);
            if (resp.ok()) {
                AgendaSourceMeta meta;
                meta.color565 = colors[idx];
                meta.sourceIdx = idx;
                strncpy(meta.defaultBadge, labels[idx], sizeof(meta.defaultBadge) - 1);
                topCount = AgendaIcsParser::parseStream(
                    resp.stream(), now, m_horizonDays, m_localTzOffsetSec,
                    meta, top, topCount, AGENDA_MAX_CACHED_EVENTS
                );
                anySuccess = true;
            }
            resp.consume();
        }
        session.close();
    } else {
        for (uint8_t idx = 0; idx < 2; ++idx) {
            if (!validUrl[idx]) continue;
            esp_task_wdt_reset();
            net::SecureHttpResponse resp = net::SecureHttpClient::get(String(urls[idx]), opts);
            if (resp.ok()) {
                AgendaSourceMeta meta;
                meta.color565 = colors[idx];
                meta.sourceIdx = idx;
                strncpy(meta.defaultBadge, labels[idx], sizeof(meta.defaultBadge) - 1);
                topCount = AgendaIcsParser::parseStream(
                    resp.stream(), now, m_horizonDays, m_localTzOffsetSec,
                    meta, top, topCount, AGENDA_MAX_CACHED_EVENTS
                );
                anySuccess = true;
            }
            resp.consume();
        }
    }

    if (anySuccess) {
        writeEventsToSdCache(top, static_cast<uint8_t>(topCount), now);
        LOGI("Agenda", "Cached %u upcoming events to SD (/cache/agenda_%s.bin)", (unsigned)topCount, m_instanceId);
    }
#endif
}

void AgendaEngine::activate() {
    updateLocalTzOffset();
    uint32_t now = currentEpoch();

#if defined(ESP32) && !defined(PIO_UNIT_TESTING)
    // On PSRAM boards where DisplayRuntime skips releasePanel(), prefetch on activate if needed
    if (needsTlsFetch() && WiFi.status() == WL_CONNECTED && NetworkBudget::canStartTlsSession()) {
        prefetchData();
    }
#endif

    loadUpcomingFromSdCache(now);

    m_currentSlideIdx = 0;
    m_slidesShownCycle = 0;
    m_scrollPhase = ScrollPhase::PauseStart;
    m_scrollOffsetPx = 0;
    uint32_t ms = millis();
    m_phaseStartMs = ms;
    m_lastSlideMs = ms;
    m_lastAnimTickMs = ms;
    m_animTick = 0;
    m_finished = (m_activeCount == 0 && !m_emptyChill);
    recomputeSlideScrollBounds(m_geometry.width);
    requestRedraw();
}

void AgendaEngine::deactivate() {
#if defined(ESP32) && !defined(UNIT_TEST)
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_AGENDA);
#endif
    // Invalidate active slot state (0 dynamic bytes retained at rest)
    m_activeCount = 0;
    m_currentSlideIdx = 0;
    m_scrollOffsetPx = 0;
    m_maxScrollPx = 0;
    m_redrawFrames = 0;
    m_renderedNewFrame = false;
    m_isRealtimeNow = false;
}

bool AgendaEngine::shutdownForDestruction() {
#if defined(ESP32) && !defined(UNIT_TEST)
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_AGENDA);
#endif
    return true;
}

void AgendaEngine::recomputeSlideScrollBounds(uint16_t panelW) {
    m_scrollOffsetPx = 0;
    m_scrollPhase = ScrollPhase::PauseStart;
    m_phaseStartMs = millis();

    if (m_activeCount == 0 || m_currentSlideIdx >= m_activeCount) {
        m_maxScrollPx = 0;
        return;
    }

    const AgendaEventPod& ev = m_activeEvents[m_currentSlideIdx];
    int16_t titlePx = static_cast<int16_t>(strlen(ev.title)) * 6;
    int16_t availW = static_cast<int16_t>(panelW) - 36;
    if (m_funSprites) availW -= 14;
    if (availW < 20) availW = 20;

    m_maxScrollPx = (titlePx > availW) ? (titlePx - availW) : 0;
}

void AgendaEngine::update(EngineContext* context) {
    m_renderedNewFrame = false;
    if (context) {
        DisplayGeometry g = context->getGeometry();
        if (g.width != m_geometry.width || g.height != m_geometry.height || g.layoutClass != m_geometry.layoutClass) {
            onDisplayGeometryChanged(g);
        }
    }

    if (m_activeCount == 0 && !m_emptyChill) {
        m_finished = true;
        m_isRealtimeNow = false;
        return;
    }

    uint32_t nowMs = millis();
    bool dirty = false;

    // Advance sprite animation tick at ~4 Hz (250 ms)
    if (nowMs - m_lastAnimTickMs >= 250U) {
        m_lastAnimTickMs = nowMs;
        m_animTick++;
        if (m_funSprites || m_activeCount == 0) {
            dirty = true;
        }
    }

    // Compact wide layout scrolls overflowing title smoothly at ~30 FPS
    const bool isCompactWide = (m_geometry.layoutClass == LayoutClass::WIDE && m_geometry.height < 64);
    if (isCompactWide && m_activeCount > 0 && m_maxScrollPx > 0) {
        m_isRealtimeNow = true;
        switch (m_scrollPhase) {
            case ScrollPhase::PauseStart:
                if (nowMs - m_phaseStartMs >= 1200U) {
                    m_scrollPhase = ScrollPhase::Scrolling;
                    m_phaseStartMs = nowMs;
                }
                break;
            case ScrollPhase::Scrolling:
                if (nowMs - m_phaseStartMs >= 33U) {
                    m_phaseStartMs = nowMs;
                    m_scrollOffsetPx++;
                    dirty = true;
                    if (m_scrollOffsetPx >= m_maxScrollPx) {
                        m_scrollOffsetPx = m_maxScrollPx;
                        m_scrollPhase = ScrollPhase::PauseEnd;
                    }
                }
                break;
            case ScrollPhase::PauseEnd:
                if (nowMs - m_phaseStartMs >= 1000U) {
                    uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
                    if (limit > 1) {
                        m_currentSlideIdx = (m_currentSlideIdx + 1) % limit;
                    }
                    m_slidesShownCycle++;
                    m_lastSlideMs = nowMs;
                    recomputeSlideScrollBounds(m_geometry.width);
                    dirty = true;
                }
                break;
        }
    } else {
        m_isRealtimeNow = false;
        // Advance compact slide every 5 seconds when static
        if (isCompactWide && m_activeCount > 1 && (nowMs - m_lastSlideMs >= 5000U)) {
            uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
            if (limit > 1) {
                m_currentSlideIdx = (m_currentSlideIdx + 1) % limit;
                recomputeSlideScrollBounds(m_geometry.width);
                dirty = true;
            }
            m_lastSlideMs = nowMs;
        }
    }

    if (m_redrawFrames > 0) {
        m_redrawFrames--;
        dirty = true;
    }

    m_renderedNewFrame = dirty;
}

void AgendaEngine::formatEventTimePill(uint32_t startEpoch, bool allDay, int32_t tzOffsetSec, uint32_t nowEpoch, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    Lang l = I18n::getLang();
    if (allDay) {
        int32_t evDay = localDayIndex(startEpoch, tzOffsetSec);
        int32_t curDay = (nowEpoch > 0) ? localDayIndex(nowEpoch, tzOffsetSec) : evDay;
        if (evDay == curDay) {
            strncpy(out, I18n::getAgendaTodayLabel(l, true), outSize - 1);
        } else if (evDay == curDay + 1) {
            strncpy(out, I18n::getAgendaTomorrowLabel(l, true), outSize - 1);
        } else {
            strncpy(out, I18n::getAgendaAllDayLabel(l), outSize - 1);
        }
        out[outSize - 1] = '\0';
        return;
    }

    int64_t localSec = static_cast<int64_t>(startEpoch) + static_cast<int64_t>(tzOffsetSec);
    int64_t rem = localSec % 86400LL;
    if (rem < 0) rem += 86400LL;
    int hh = static_cast<int>(rem / 3600LL);
    int mm = static_cast<int>((rem % 3600LL) / 60LL);
    snprintf(out, outSize, "%02d:%02d", hh, mm);
}

void AgendaEngine::renderChillScene(IDrawingSurface* surface, uint16_t w, uint16_t h) const {
    // Draw retro brick ground along the bottom
    ClockSpriteBank::SpriteFrameRef tile = ClockSpriteBank::getCameoFrame(ClockSpriteBank::CameoSprite::GROUND_TILE_MEGAMAN, 0);
    int16_t groundY = static_cast<int16_t>(h) - 6;
    for (int16_t gx = 0; gx < static_cast<int16_t>(w); gx += 8) {
        ClockSpriteBank::blitSprite(surface, tile, gx, groundY, false);
    }

    // Sleeping Metool with floating Zzz
    int16_t mx = (w >= 96) ? 6 : static_cast<int16_t>((w - 16) / 2);
    int16_t my = groundY - 13;
    ClockSpriteBank::drawAgendaIcon(surface, ClockSpriteBank::SPRITE_METOOL_SLEEP, mx, my, 16, m_animTick);

    Lang l = I18n::getLang();
    const char* label = I18n::getAgendaFreePlayLabel(l, w < 120);
    int16_t tx = (w >= 96) ? 28 : 2;
    int16_t ty = (w >= 96) ? static_cast<int16_t>((groundY - 8) / 2) : 3;
    drawClippedText5x7(surface, label, tx, ty, tx, static_cast<int16_t>(w) - 2, 0x07FF);
}

void AgendaEngine::renderWideCompact(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    const AgendaEventPod& ev = m_activeEvents[m_currentSlideIdx < m_activeCount ? m_currentSlideIdx : 0];
    const bool allDay = (ev.flags & AGENDA_FLAG_ALL_DAY) != 0;

    // Top row (y = 0..17): colored time pill + contextual 12x12 sprite + scrolling/static title
    char timeStr[12]{};
    formatEventTimePill(ev.startEpoch, allDay, m_localTzOffsetSec, nowEpoch, timeStr, sizeof(timeStr));

    int16_t pillW = 24;
    surface->fillRect(1, 2, pillW, 11, ev.calendarColor565);
    drawTinyTimeString3x5(surface, 3, 5, timeStr, 0x0000);

    int16_t textMinX = pillW + 3;
    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, ev.spriteId, textMinX, 2, 12, m_animTick);
        textMinX += 13;
    }

    drawClippedText5x7(surface, ev.title, textMinX - m_scrollOffsetPx, 4, textMinX, static_cast<int16_t>(w) - 1, 0xFFFF);

    // Subtle divider line
    surface->drawFastHLine(0, 16, w, 0x2104);

    // Bottom row (y = 19..31): Imminent gauge / Active progress / Next event preview
    Lang l = I18n::getLang();
    bool isImminent = false;
    int32_t minsUntil = 0;
    if (nowEpoch > 0 && ev.startEpoch > nowEpoch) {
        uint32_t diffSec = ev.startEpoch - nowEpoch;
        minsUntil = static_cast<int32_t>((diffSec + 59U) / 60U);
        if (m_imminentMin > 0 && minsUntil <= static_cast<int32_t>(m_imminentMin)) {
            isImminent = true;
        }
    }
    const bool isActiveNow = (nowEpoch > 0 && nowEpoch >= ev.startEpoch && nowEpoch < ev.endEpoch);

    if (isImminent) {
        // VS-Fighting style pulsing imminent bar
        uint16_t barCol = (m_animTick & 1U) ? 0xFFE0 : 0xFD20;
        int16_t barMaxW = static_cast<int16_t>(w) - 4;
        int16_t fillW = (m_imminentMin > 0)
            ? static_cast<int16_t>((barMaxW * (static_cast<int32_t>(m_imminentMin) - minsUntil + 1)) / static_cast<int32_t>(m_imminentMin))
            : barMaxW;
        if (fillW < 6) fillW = 6;
        if (fillW > barMaxW) fillW = barMaxW;
        surface->drawRect(2, 19, barMaxW, 11, barCol);
        surface->fillRect(3, 20, fillW - 2, 9, 0x4100);

        char immBuf[24]{};
        I18n::formatAgendaImminent(immBuf, sizeof(immBuf), minsUntil, l);
        drawClippedText5x7(surface, immBuf, 6, 21, 4, static_cast<int16_t>(w) - 4, barCol);
    } else if (isActiveNow) {
        // Active slot progress bar
        uint32_t totalDur = (ev.endEpoch > ev.startEpoch) ? (ev.endEpoch - ev.startEpoch) : 3600U;
        uint32_t elapsed = nowEpoch - ev.startEpoch;
        int32_t remMin = static_cast<int32_t>((ev.endEpoch - nowEpoch + 59U) / 60U);
        int16_t barMaxW = static_cast<int16_t>(w) - 4;
        int16_t fillW = static_cast<int16_t>((static_cast<uint64_t>(barMaxW) * elapsed) / totalDur);
        if (fillW > barMaxW) fillW = barMaxW;
        surface->drawRect(2, 19, barMaxW, 11, 0x07E0);
        if (fillW > 2) surface->fillRect(3, 20, fillW - 2, 9, 0x0320);

        char nowBuf[32]{};
        snprintf(nowBuf, sizeof(nowBuf), "%s (-%dm)", I18n::getAgendaActiveNowLabel(l), (int)remMin);
        drawClippedText5x7(surface, nowBuf, 6, 21, 4, static_cast<int16_t>(w) - 4, 0x07E0);
    } else if (m_activeCount > 1) {
        uint8_t nextIdx = (m_currentSlideIdx + 1) % m_activeCount;
        const AgendaEventPod& nextEv = m_activeEvents[nextIdx];
        char nextTime[12]{};
        formatEventTimePill(nextEv.startEpoch, (nextEv.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, nextTime, sizeof(nextTime));
        char line2[64]{};
        snprintf(line2, sizeof(line2), "%s %s %s", I18n::getAgendaNextPrefix(l), nextTime, nextEv.title);
        drawClippedText5x7(surface, line2, 2, 21, 2, static_cast<int16_t>(w) - 14, 0xAD55);

        // Pagination dots on bottom right
        uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
        for (uint8_t i = 0; i < limit; ++i) {
            uint16_t dotCol = (i == m_currentSlideIdx) ? 0x07FF : 0x4208;
            surface->fillRect(static_cast<int16_t>(w) - static_cast<int16_t>((limit - i) * 4), 28, 2, 2, dotCol);
        }
    } else {
        drawClippedText5x7(surface, ev.badgeLabel, 2, 21, 2, static_cast<int16_t>(w) - 2, ev.calendarColor565);
    }
}

void AgendaEngine::renderWideHd(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    const int16_t leftColW = (w >= 200) ? 72 : 52;

    // Left Column: Beveled Arcade Calendar Block + Large Contextual Sprite
    surface->drawRect(2, 2, leftColW - 4, h - 4, 0x07FF);
    surface->fillRect(3, 3, leftColW - 6, 12, 0xF800);

    Lang l = I18n::getLang();
    const AgendaEventPod& firstEv = m_activeEvents[0];
    drawClippedText5x7(surface, I18n::getAgendaTodayLabel(l, true), 6, 5, 4, leftColW - 4, 0xFFFF);

    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, firstEv.spriteId, (leftColW / 2) - 7, 20, 16, m_animTick);
    }
    ClockSpriteBank::drawPacman(surface, (leftColW / 2) - 6, h - 18, static_cast<uint8_t>(m_animTick), true);

    // Right Column: Up to 3 simultaneous event cards with vertical accent bar
    uint8_t count = (m_activeCount < 3) ? m_activeCount : 3;
    int16_t rowH = static_cast<int16_t>(h) / 3;
    for (uint8_t i = 0; i < count; ++i) {
        const AgendaEventPod& ev = m_activeEvents[i];
        int16_t ry = static_cast<int16_t>(i) * rowH + 2;
        surface->fillRect(leftColW + 2, ry, 3, rowH - 3, ev.calendarColor565);

        char tbuf[12]{};
        formatEventTimePill(ev.startEpoch, (ev.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, tbuf, sizeof(tbuf));
        drawTinyTimeString3x5(surface, leftColW + 8, ry + 2, tbuf, ev.calendarColor565);
        drawClippedText5x7(surface, ev.title, leftColW + 32, ry + 1, leftColW + 32, static_cast<int16_t>(w) - 2, 0xFFFF);
        drawClippedText5x7(surface, ev.badgeLabel, leftColW + 32, ry + 10, leftColW + 32, static_cast<int16_t>(w) - 2, 0x8410);
    }
}

void AgendaEngine::renderSquare(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    Lang l = I18n::getLang();
    surface->fillRect(0, 0, w, 12, 0x1084);
    drawClippedText5x7(surface, I18n::getAgendaTodayLabel(l, true), 2, 2, 2, static_cast<int16_t>(w) - 18, 0x07FF);
    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, m_activeEvents[0].spriteId, static_cast<int16_t>(w) - 15, 0, 12, m_animTick);
    }

    uint8_t count = (m_activeCount < 2) ? m_activeCount : 2;
    for (uint8_t i = 0; i < count; ++i) {
        const AgendaEventPod& ev = m_activeEvents[i];
        int16_t cy = 15 + static_cast<int16_t>(i) * 24;
        surface->fillRect(1, cy, 2, 20, ev.calendarColor565);
        char tbuf[12]{};
        formatEventTimePill(ev.startEpoch, (ev.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, tbuf, sizeof(tbuf));
        drawTinyTimeString3x5(surface, 5, cy + 1, tbuf, ev.calendarColor565);
        drawClippedText5x7(surface, ev.badgeLabel, 28, cy, 28, static_cast<int16_t>(w) - 1, 0xAD55);
        drawClippedText5x7(surface, ev.title, 5, cy + 10, 5, static_cast<int16_t>(w) - 1, 0xFFFF);
    }
}

void AgendaEngine::renderTateQuestLog(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    Lang l = I18n::getLang();

    // Header: Quest Log Date + Contextual Sprite on Ground Bricks
    drawClippedText5x7(surface, I18n::getAgendaTodayLabel(l, w < 64), 2, 2, 2, static_cast<int16_t>(w) - 2, 0xFFE0);
    ClockSpriteBank::SpriteFrameRef tile = ClockSpriteBank::getCameoFrame(ClockSpriteBank::CameoSprite::GROUND_TILE_MARIO, 0);
    for (int16_t gx = 0; gx < static_cast<int16_t>(w); gx += 8) {
        ClockSpriteBank::blitSprite(surface, tile, gx, 22, false);
    }
    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, m_activeEvents[0].spriteId, static_cast<int16_t>(w / 2) - 6, 10, 12, m_animTick);
    }

    int16_t yCursor = 32;
    uint8_t maxCards = (h >= 200) ? 7 : ((h >= 120) ? 4 : 2);
    uint8_t limit = (m_activeCount < maxCards) ? m_activeCount : maxCards;
    for (uint8_t i = 0; i < limit && yCursor + 20 <= static_cast<int16_t>(h); ++i) {
        const AgendaEventPod& ev = m_activeEvents[i];
        surface->drawRect(1, yCursor, w - 2, 20, ev.calendarColor565);
        char tbuf[12]{};
        formatEventTimePill(ev.startEpoch, (ev.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, tbuf, sizeof(tbuf));
        drawTinyTimeString3x5(surface, 4, yCursor + 2, tbuf, ev.calendarColor565);
        drawClippedText5x7(surface, ev.title, 4, yCursor + 10, 4, static_cast<int16_t>(w) - 3, 0xFFFF);
        yCursor += 23;
    }
}

void AgendaEngine::render(EngineContext* context) {
    if (!context || !context->getSurface()) return;
    IDrawingSurface* surface = context->getSurface();
    const uint16_t w = surface->width();
    const uint16_t h = surface->height();

    surface->clear(0);

    if (m_activeCount == 0) {
        if (m_emptyChill) {
            renderChillScene(surface, w, h);
        }
        return;
    }

    uint32_t now = currentEpoch();
    switch (m_geometry.layoutClass) {
        case LayoutClass::PORTRAIT:
        case LayoutClass::TALL:
            renderTateQuestLog(surface, w, h, now);
            break;
        case LayoutClass::SQUARE:
            renderSquare(surface, w, h, now);
            break;
        case LayoutClass::WIDE:
        default:
            if (h >= 64) {
                renderWideHd(surface, w, h, now);
            } else {
                renderWideCompact(surface, w, h, now);
            }
            break;
    }
}

namespace {
constexpr FieldTableEntry kAgendaFields[] = {
    {"ics_url_1", ConfigType::STRING, "Primary Calendar URL (.ics)", "Private iCal (.ics / webcal://) URL (Google, iCloud, Outlook, Proton, Nextcloud).", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept},
    {"ics_label_1", ConfigType::STRING, "Primary Calendar Badge", "Short label displayed for the first calendar.", "Perso", false, "", "", "", "", "", false, "", ValidationPolicy::Accept},
    {"ics_color_1", ConfigType::COLOR, "Primary Calendar Color", "Accent color for the first calendar.", "#00E5FF", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault},
    {"ics_url_2", ConfigType::STRING, "Secondary Calendar URL (.ics)", "Optional second iCal (.ics / webcal://) URL (Work, Family, Birthdays).", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept},
    {"ics_label_2", ConfigType::STRING, "Secondary Calendar Badge", "Short label displayed for the second calendar.", "Pro", false, "", "", "", "", "", false, "", ValidationPolicy::Accept},
    {"ics_color_2", ConfigType::COLOR, "Secondary Calendar Color", "Accent color for the second calendar.", "#FFB300", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault},
    {"cache_ttl_min", ConfigType::INTEGER, "SD Cache Duration (min)", "Duration in minutes to serve cached events from MicroSD before refreshing.", "60", false, "15", "1440", "5", "", "", false, "", ValidationPolicy::Clamp},
    {"horizon_days", ConfigType::INTEGER, "Lookahead Horizon (days)", "Number of upcoming days to scan for events.", "7", false, "1", "30", "1", "", "", false, "", ValidationPolicy::Clamp},
    {"events_shown", ConfigType::INTEGER, "Max Events per Cycle", "Maximum number of upcoming events displayed per rotation cycle.", "3", false, "1", "6", "1", "", "", false, "", ValidationPolicy::Clamp},
    {"empty_behavior", ConfigType::ENUM, "Empty Schedule Behavior", "Action when no upcoming event is scheduled: 'chill' shows retro Free Play scene, 'skip' advances rotation immediately.", "chill", false, "", "", "", "chill,skip", "", false, "", ValidationPolicy::FallbackDefault},
    {"imminent_min", ConfigType::INTEGER, "Imminent Alert Threshold (min)", "Arcade VS-Fighting countdown gauge when an event starts within N minutes (0 = disabled).", "15", false, "0", "60", "5", "", "", false, "", ValidationPolicy::Clamp},
    {"fun_sprites", ConfigType::BOOLEAN, "Contextual Pixel-Art Sprites", "Display animated contextual arcade sprites (birthday cake, cyber call, heart, pizza, cameos).", "true", false}
};
} // namespace

EngineDescriptor AgendaEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = EngineMetadata("agenda", "Connected Agenda (.ics)", "info", FIRMWARE_VERSION);
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.realtime = false;
    desc.capabilities.interruptible = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;

    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = true;
    desc.requirements.needsSd = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.prefersDoubleBuffer = false;
    desc.requirements.targetFps = 30;
    desc.requirements.internalPersistentBytes = 1200;
    desc.requirements.internalContiguousBytes = 8192;
    desc.requirements.shadowBytesPerFrame = 0;

    desc.schema.fields.assign(std::begin(kAgendaFields), std::end(kAgendaFields));
    desc.factory = []() { return std::unique_ptr<IEngine>(new AgendaEngine()); };
    return desc;
}
