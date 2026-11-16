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

void civilFromDaysLocal(int64_t z, int& y, unsigned& m, unsigned& d, int& weekday0) {
    // 1970-01-01 was a Thursday (weekday 4 where 0 = Sunday)
    int64_t wd = (z + 4) % 7;
    if (wd < 0) wd += 7;
    weekday0 = static_cast<int>(wd);

    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2) ? 1 : 0;
}

void drawClippedVerticalText5x7(IDrawingSurface* surface, const char* text, int16_t startX, int16_t y,
                                int16_t clipMinX, int16_t clipMaxX, int16_t clipMinY, int16_t clipMaxY,
                                uint16_t color) {
    if (!surface || !text || *text == '\0') return;
    if (y + 7 <= clipMinY || y >= clipMaxY) return;
    drawClippedText5x7(surface, text, startX, y, clipMinX, clipMaxX, color);
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

    int shown = config->getInt("events_shown", 5);
    if (shown < 1) shown = 1;
    if (shown > static_cast<int>(MAX_ACTIVE_EVENTS)) shown = static_cast<int>(MAX_ACTIVE_EVENTS);
    m_eventsShown = static_cast<uint8_t>(shown);

    int imm = config->getInt("imminent_min", 15);
    if (imm < 0) imm = 0;
    if (imm > 60) imm = 60;
    m_imminentMin = static_cast<uint8_t>(imm);

    int slideSec = config->getInt("slide_duration_sec", 5);
    if (slideSec < 3) slideSec = 3;
    if (slideSec > 20) slideSec = 20;
    m_slideDurationSec = static_cast<uint8_t>(slideSec);

    String vm = config->getString("view_mode", "auto");
    if (vm.equalsIgnoreCase("multiday_scroll")) m_viewMode = ViewMode::MultiDayScroll;
    else if (vm.equalsIgnoreCase("cards")) m_viewMode = ViewMode::Cards;
    else if (vm.equalsIgnoreCase("split_deck")) m_viewMode = ViewMode::SplitDeck;
    else if (vm.equalsIgnoreCase("ticker")) m_viewMode = ViewMode::Ticker;
    else m_viewMode = ViewMode::Auto;

    String th = config->getString("theme", "cyber_arcade");
    if (th.equalsIgnoreCase("amber_dmd")) m_theme = AgendaTheme::AmberDmd;
    else if (th.equalsIgnoreCase("matrix_green")) m_theme = AgendaTheme::MatrixGreen;
    else if (th.equalsIgnoreCase("snes_rpg")) m_theme = AgendaTheme::SnesRpg;
    else if (th.equalsIgnoreCase("minimal_clean")) m_theme = AgendaTheme::MinimalClean;
    else m_theme = AgendaTheme::CyberArcade;

    String spd = config->getString("scroll_speed", "normal");
    if (spd.equalsIgnoreCase("slow")) m_scrollSpeed = ScrollSpeed::Slow;
    else if (spd.equalsIgnoreCase("fast")) m_scrollSpeed = ScrollSpeed::Fast;
    else m_scrollSpeed = ScrollSpeed::Normal;

    m_showRelativeTime = config->getBool("show_relative_time", true);
    m_showBadges = config->getBool("show_badges", true);

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

uint32_t AgendaEngine::scrollStepIntervalMs() const {
    switch (m_scrollSpeed) {
        case ScrollSpeed::Slow: return 55U;
        case ScrollSpeed::Fast: return 20U;
        case ScrollSpeed::Normal:
        default: return 33U;
    }
}

AgendaEngine::ThemeColors AgendaEngine::resolveThemeColors() const {
    switch (m_theme) {
        case AgendaTheme::AmberDmd:
            return { 0x2080, 0xFD20, 0xFFE0, 0xFD20, 0xC380, 0x4100, 0x0000, true, 0xFD20 };
        case AgendaTheme::MatrixGreen:
            return { 0x0120, 0x07E0, 0x87F0, 0x07E0, 0x04A0, 0x0240, 0x0000, true, 0x07E0 };
        case AgendaTheme::SnesRpg:
            return { 0x108B, 0xFFE0, 0xFFFF, 0xFFFF, 0xAD55, 0x6B4D, 0x0845, false, 0x07FF };
        case AgendaTheme::MinimalClean:
            return { 0x1082, 0xAD55, 0xFFFF, 0xFFFF, 0x8410, 0x2104, 0x0000, false, 0xFFFF };
        case AgendaTheme::CyberArcade:
        default:
            return { 0x0863, 0x07FF, 0x07FF, 0xFFFF, 0x8410, 0x2104, 0x0000, false, 0x07FF };
    }
}

uint16_t AgendaEngine::resolveEventAccent(const AgendaEventPod& ev, const ThemeColors& tc) const {
    if (tc.overrideCalendarColor) {
        return tc.primaryAccent;
    }
    return ev.calendarColor565 ? ev.calendarColor565 : tc.primaryAccent;
}

void AgendaEngine::formatEventDayHeader(uint32_t startEpoch, uint32_t nowEpoch, bool shortForm, char* out, size_t outSize) const {
    int32_t evDay = localDayIndex(startEpoch, m_localTzOffsetSec);
    int32_t curDay = (nowEpoch > 0) ? localDayIndex(nowEpoch, m_localTzOffsetSec) : evDay;
    int y = 1970, wd = 0;
    unsigned m = 1, d = 1;
    civilFromDaysLocal(evDay, y, m, d, wd);
    I18n::formatAgendaDayBanner(evDay, curDay, wd, static_cast<int>(d), static_cast<int>(m),
                                I18n::getLang(), m_showRelativeTime && !shortForm, shortForm, out, outSize);
}

void AgendaEngine::recomputeSlideScrollBounds(uint16_t panelW) {
    m_scrollOffsetPx = 0;
    m_scrollPhase = ScrollPhase::PauseStart;
    m_phaseStartMs = millis();

    const uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
    if (limit == 0) {
        m_maxScrollPx = 0;
        return;
    }

    if (m_viewMode == ViewMode::MultiDayScroll) {
        // Calculate total vertical height of day headers + event rows
        int16_t totalH = 0;
        int32_t prevDay = -999999;
        for (uint8_t i = 0; i < limit; ++i) {
            int32_t evDay = localDayIndex(m_activeEvents[i].startEpoch, m_localTzOffsetSec);
            if (evDay != prevDay) {
                prevDay = evDay;
                totalH += 11;
            }
            totalH += 12;
        }
        int16_t viewH = static_cast<int16_t>(m_geometry.height) - 2;
        m_maxScrollPx = (totalH > viewH) ? (totalH - viewH + 6) : 0;
        return;
    }

    if (m_viewMode == ViewMode::Ticker) {
        int16_t totalW = 0;
        for (uint8_t i = 0; i < limit; ++i) {
            totalW += static_cast<int16_t>(strlen(m_activeEvents[i].title)) * 6 + 96;
        }
        m_maxScrollPx = (totalW > static_cast<int16_t>(panelW)) ? totalW : static_cast<int16_t>(panelW);
        return;
    }

    if (m_currentSlideIdx >= limit) m_currentSlideIdx = 0;
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
    const uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
    const uint32_t stepMs = scrollStepIntervalMs();
    const uint32_t slideDurMs = static_cast<uint32_t>(m_slideDurationSec) * 1000U;

    // Advance sprite animation tick at ~4 Hz (250 ms)
    if (nowMs - m_lastAnimTickMs >= 250U) {
        m_lastAnimTickMs = nowMs;
        m_animTick++;
        if (m_funSprites || m_activeCount == 0) {
            dirty = true;
        }
    }

    if (m_viewMode == ViewMode::Ticker && limit > 0) {
        m_isRealtimeNow = true;
        if (nowMs - m_phaseStartMs >= stepMs) {
            m_phaseStartMs = nowMs;
            m_scrollOffsetPx++;
            if (m_scrollOffsetPx >= m_maxScrollPx) {
                m_scrollOffsetPx = 0;
            }
            dirty = true;
        }
    } else if (m_viewMode == ViewMode::MultiDayScroll && limit > 0 && m_maxScrollPx > 0) {
        m_isRealtimeNow = true;
        switch (m_scrollPhase) {
            case ScrollPhase::PauseStart:
                if (nowMs - m_phaseStartMs >= 1400U) {
                    m_scrollPhase = ScrollPhase::Scrolling;
                    m_phaseStartMs = nowMs;
                }
                break;
            case ScrollPhase::Scrolling:
                if (nowMs - m_phaseStartMs >= stepMs) {
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
                if (nowMs - m_phaseStartMs >= 1400U) {
                    m_scrollOffsetPx = 0;
                    m_scrollPhase = ScrollPhase::PauseStart;
                    m_phaseStartMs = nowMs;
                    dirty = true;
                }
                break;
        }
    } else {
        const bool isCompactOrCards = (m_viewMode == ViewMode::Cards) ||
                                      (m_viewMode == ViewMode::Auto && m_geometry.layoutClass == LayoutClass::WIDE && m_geometry.height < 64);
        if (isCompactOrCards && limit > 0 && m_maxScrollPx > 0) {
            m_isRealtimeNow = true;
            switch (m_scrollPhase) {
                case ScrollPhase::PauseStart:
                    if (nowMs - m_phaseStartMs >= 1200U) {
                        m_scrollPhase = ScrollPhase::Scrolling;
                        m_phaseStartMs = nowMs;
                    }
                    break;
                case ScrollPhase::Scrolling:
                    if (nowMs - m_phaseStartMs >= stepMs) {
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
            if (limit > 1 && (nowMs - m_lastSlideMs >= slideDurMs)) {
                m_currentSlideIdx = (m_currentSlideIdx + 1) % limit;
                recomputeSlideScrollBounds(m_geometry.width);
                m_lastSlideMs = nowMs;
                dirty = true;
            }
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
    const ThemeColors tc = resolveThemeColors();
    const uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
    const uint8_t curIdx = (limit > 0 && m_currentSlideIdx < limit) ? m_currentSlideIdx : 0;
    const AgendaEventPod& ev = m_activeEvents[curIdx];
    const bool allDay = (ev.flags & AGENDA_FLAG_ALL_DAY) != 0;
    const uint16_t accent = resolveEventAccent(ev, tc);

    if (tc.cardBg != 0) {
        surface->fillRect(0, 0, w, h, tc.cardBg);
    }

    // Top row (y = 0..17): colored time pill + contextual 12x12 sprite + scrolling/static title
    char timeStr[12]{};
    formatEventTimePill(ev.startEpoch, allDay, m_localTzOffsetSec, nowEpoch, timeStr, sizeof(timeStr));

    int16_t pillW = 24;
    surface->fillRect(1, 2, pillW, 11, accent);
    drawTinyTimeString3x5(surface, 3, 5, timeStr, 0x0000);

    int16_t textMinX = pillW + 3;
    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, ev.spriteId, textMinX, 2, 12, m_animTick);
        textMinX += 13;
    }

    drawClippedText5x7(surface, ev.title, textMinX - m_scrollOffsetPx, 4, textMinX, static_cast<int16_t>(w) - 1, tc.titleText);

    // Subtle divider line
    surface->drawFastHLine(0, 16, w, tc.divider);

    // Bottom row (y = 19..31): Imminent gauge / Active progress / Day + Badge + Next event preview
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
    } else {
        char dayHdr[28]{};
        formatEventDayHeader(ev.startEpoch, nowEpoch, true, dayHdr, sizeof(dayHdr));
        char line2[72]{};
        if (m_showBadges && ev.badgeLabel[0] != '\0') {
            snprintf(line2, sizeof(line2), "%s [%s]", dayHdr, ev.badgeLabel);
        } else {
            snprintf(line2, sizeof(line2), "%s", dayHdr);
        }
        drawClippedText5x7(surface, line2, 2, 21, 2, static_cast<int16_t>(w) - 18, tc.headerText);

        // Pagination dots on bottom right
        for (uint8_t i = 0; i < limit; ++i) {
            uint16_t dotCol = (i == curIdx) ? tc.headerAccent : tc.divider;
            surface->fillRect(static_cast<int16_t>(w) - static_cast<int16_t>((limit - i) * 4), 28, 2, 2, dotCol);
        }
    }
}

void AgendaEngine::renderWideHd(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    const ThemeColors tc = resolveThemeColors();
    const uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
    const uint8_t startIdx = (limit > 3) ? ((m_currentSlideIdx / 3) * 3) % limit : 0;
    const AgendaEventPod& leadEv = m_activeEvents[startIdx];
    const int16_t leftColW = (w >= 200) ? 72 : 54;

    if (tc.cardBg != 0) {
        surface->fillRect(0, 0, w, h, tc.cardBg);
    }

    // Left Column: Beveled Arcade Calendar Block + Dynamic Day/Date + Large Contextual Sprite
    surface->drawRect(2, 2, leftColW - 4, h - 4, tc.headerAccent);
    surface->fillRect(3, 3, leftColW - 6, 12, tc.headerBg);

    char dayHdr[24]{};
    formatEventDayHeader(leadEv.startEpoch, nowEpoch, true, dayHdr, sizeof(dayHdr));
    drawClippedText5x7(surface, dayHdr, 5, 5, 4, leftColW - 4, tc.headerText);

    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, leadEv.spriteId, (leftColW / 2) - 7, 20, 16, m_animTick);
    }
    ClockSpriteBank::drawPacman(surface, (leftColW / 2) - 6, h - 18, static_cast<uint8_t>(m_animTick), true);

    // Right Column: Up to 3 simultaneous event cards (paginated across multi-day horizon)
    int16_t rowH = static_cast<int16_t>(h) / 3;
    for (uint8_t row = 0; row < 3 && row < limit; ++row) {
        uint8_t idx = (startIdx + row) % limit;
        const AgendaEventPod& ev = m_activeEvents[idx];
        uint16_t accent = resolveEventAccent(ev, tc);
        int16_t ry = static_cast<int16_t>(row) * rowH + 2;
        surface->fillRect(leftColW + 2, ry, 3, rowH - 3, accent);

        char tbuf[12]{};
        formatEventTimePill(ev.startEpoch, (ev.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, tbuf, sizeof(tbuf));
        drawTinyTimeString3x5(surface, leftColW + 8, ry + 2, tbuf, accent);
        drawClippedText5x7(surface, ev.title, leftColW + 32, ry + 1, leftColW + 32, static_cast<int16_t>(w) - 2, tc.titleText);

        char subLine[40]{};
        char evDayShort[20]{};
        formatEventDayHeader(ev.startEpoch, nowEpoch, true, evDayShort, sizeof(evDayShort));
        if (m_showBadges && ev.badgeLabel[0] != '\0') {
            snprintf(subLine, sizeof(subLine), "[%s] %s", ev.badgeLabel, evDayShort);
        } else {
            snprintf(subLine, sizeof(subLine), "%s", evDayShort);
        }
        drawClippedText5x7(surface, subLine, leftColW + 32, ry + 10, leftColW + 32, static_cast<int16_t>(w) - 2, tc.dimText);
    }
}

void AgendaEngine::renderSquare(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    const ThemeColors tc = resolveThemeColors();
    const uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
    const uint8_t startIdx = (limit > 2) ? (m_currentSlideIdx % limit) : 0;
    const AgendaEventPod& leadEv = m_activeEvents[startIdx];

    if (tc.cardBg != 0) {
        surface->fillRect(0, 0, w, h, tc.cardBg);
    }
    surface->fillRect(0, 0, w, 12, tc.headerBg);
    char dayHdr[24]{};
    formatEventDayHeader(leadEv.startEpoch, nowEpoch, true, dayHdr, sizeof(dayHdr));
    drawClippedText5x7(surface, dayHdr, 2, 2, 2, static_cast<int16_t>(w) - 16, tc.headerText);
    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, leadEv.spriteId, static_cast<int16_t>(w) - 14, 0, 12, m_animTick);
    }

    uint8_t count = (limit < 2) ? limit : 2;
    for (uint8_t row = 0; row < count; ++row) {
        const AgendaEventPod& ev = m_activeEvents[(startIdx + row) % limit];
        uint16_t accent = resolveEventAccent(ev, tc);
        int16_t cy = 15 + static_cast<int16_t>(row) * 24;
        surface->fillRect(1, cy, 2, 20, accent);
        char tbuf[12]{};
        formatEventTimePill(ev.startEpoch, (ev.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, tbuf, sizeof(tbuf));
        drawTinyTimeString3x5(surface, 5, cy + 1, tbuf, accent);
        if (m_showBadges) {
            drawClippedText5x7(surface, ev.badgeLabel, 28, cy, 28, static_cast<int16_t>(w) - 1, tc.dimText);
        }
        drawClippedText5x7(surface, ev.title, 5, cy + 10, 5, static_cast<int16_t>(w) - 1, tc.titleText);
    }
}

void AgendaEngine::renderTateQuestLog(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    const ThemeColors tc = resolveThemeColors();
    const uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
    const uint8_t startIdx = (limit > 0) ? (m_currentSlideIdx % limit) : 0;
    const AgendaEventPod& leadEv = m_activeEvents[startIdx];

    if (tc.cardBg != 0) {
        surface->fillRect(0, 0, w, h, tc.cardBg);
    }

    char dayHdr[24]{};
    formatEventDayHeader(leadEv.startEpoch, nowEpoch, w < 64, dayHdr, sizeof(dayHdr));
    drawClippedText5x7(surface, dayHdr, 2, 2, 2, static_cast<int16_t>(w) - 2, tc.headerText);
    ClockSpriteBank::SpriteFrameRef tile = ClockSpriteBank::getCameoFrame(ClockSpriteBank::CameoSprite::GROUND_TILE_MARIO, 0);
    for (int16_t gx = 0; gx < static_cast<int16_t>(w); gx += 8) {
        ClockSpriteBank::blitSprite(surface, tile, gx, 22, false);
    }
    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, leadEv.spriteId, static_cast<int16_t>(w / 2) - 6, 10, 12, m_animTick);
    }

    int16_t yCursor = 32;
    uint8_t maxCards = (h >= 200) ? 7 : ((h >= 120) ? 4 : 2);
    uint8_t count = (limit < maxCards) ? limit : maxCards;
    for (uint8_t i = 0; i < count && yCursor + 20 <= static_cast<int16_t>(h); ++i) {
        const AgendaEventPod& ev = m_activeEvents[(startIdx + i) % limit];
        uint16_t accent = resolveEventAccent(ev, tc);
        surface->drawRect(1, yCursor, w - 2, 20, accent);
        char tbuf[12]{};
        formatEventTimePill(ev.startEpoch, (ev.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, tbuf, sizeof(tbuf));
        drawTinyTimeString3x5(surface, 4, yCursor + 2, tbuf, accent);
        drawClippedText5x7(surface, ev.title, 4, yCursor + 10, 4, static_cast<int16_t>(w) - 3, tc.titleText);
        yCursor += 23;
    }
}

void AgendaEngine::renderMultiDayVerticalScroll(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    const ThemeColors tc = resolveThemeColors();
    if (tc.cardBg != 0) {
        surface->fillRect(0, 0, w, h, tc.cardBg);
    }

    const uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
    int16_t yCursor = 1 - m_scrollOffsetPx;
    int32_t prevDay = -999999;

    for (uint8_t i = 0; i < limit; ++i) {
        const AgendaEventPod& ev = m_activeEvents[i];
        int32_t evDay = localDayIndex(ev.startEpoch, m_localTzOffsetSec);

        // Day group header banner whenever the date changes
        if (evDay != prevDay) {
            prevDay = evDay;
            if (yCursor + 10 > 0 && yCursor < static_cast<int16_t>(h)) {
                if (yCursor >= 0 && yCursor + 9 <= static_cast<int16_t>(h)) {
                    surface->fillRect(1, yCursor, w - 2, 9, tc.headerBg);
                }
                char dayBanner[32]{};
                formatEventDayHeader(ev.startEpoch, nowEpoch, w < 96, dayBanner, sizeof(dayBanner));
                drawClippedVerticalText5x7(surface, dayBanner, 3, yCursor + 1, 2, static_cast<int16_t>(w) - 2, 0, static_cast<int16_t>(h), tc.headerText);
            }
            yCursor += 11;
        }

        // Event row
        if (yCursor + 11 > 0 && yCursor < static_cast<int16_t>(h)) {
            uint16_t accent = resolveEventAccent(ev, tc);
            int16_t barTop = (yCursor < 0) ? 0 : yCursor;
            int16_t barBot = (yCursor + 10 > static_cast<int16_t>(h)) ? static_cast<int16_t>(h) : (yCursor + 10);
            if (barBot > barTop) {
                surface->fillRect(1, barTop, 2, barBot - barTop, accent);
            }

            int16_t cx = 5;
            if (m_funSprites && w >= 64 && yCursor >= 0 && yCursor + 12 <= static_cast<int16_t>(h)) {
                ClockSpriteBank::drawAgendaIcon(surface, ev.spriteId, cx, yCursor - 1, 12, m_animTick);
                cx += 13;
            }

            char tbuf[12]{};
            formatEventTimePill(ev.startEpoch, (ev.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, tbuf, sizeof(tbuf));
            if (yCursor >= 0 && yCursor + 6 <= static_cast<int16_t>(h)) {
                drawTinyTimeString3x5(surface, cx, yCursor + 2, tbuf, accent);
            }
            cx += 22;

            if (m_showBadges && w >= 120 && ev.badgeLabel[0] != '\0') {
                char bbuf[16]{};
                snprintf(bbuf, sizeof(bbuf), "[%.4s]", ev.badgeLabel);
                drawClippedVerticalText5x7(surface, bbuf, cx, yCursor + 1, cx, static_cast<int16_t>(w) - 2, 0, static_cast<int16_t>(h), accent);
                cx += static_cast<int16_t>(strlen(bbuf)) * 6 + 2;
            }

            drawClippedVerticalText5x7(surface, ev.title, cx, yCursor + 1, cx, static_cast<int16_t>(w) - 2, 0, static_cast<int16_t>(h), tc.titleText);
        }
        yCursor += 12;
    }
}

void AgendaEngine::renderMultiDayTicker(IDrawingSurface* surface, uint16_t w, uint16_t h, uint32_t nowEpoch) const {
    const ThemeColors tc = resolveThemeColors();
    if (tc.cardBg != 0) {
        surface->fillRect(0, 0, w, h, tc.cardBg);
    }

    const uint8_t limit = (m_activeCount < m_eventsShown) ? m_activeCount : m_eventsShown;
    const AgendaEventPod& leadEv = m_activeEvents[0];

    // Top bar: Calendar header + current day + active sprite
    surface->fillRect(0, 0, w, 12, tc.headerBg);
    surface->drawFastHLine(0, 12, w, tc.headerAccent);
    if (m_funSprites) {
        ClockSpriteBank::drawAgendaIcon(surface, leadEv.spriteId, 2, 0, 12, m_animTick);
    }
    char hdr[32]{};
    formatEventDayHeader(leadEv.startEpoch, nowEpoch, false, hdr, sizeof(hdr));
    drawClippedText5x7(surface, hdr, m_funSprites ? 16 : 3, 2, 2, static_cast<int16_t>(w) - 2, tc.headerText);

    // Continuous multi-day ticker stream below header
    int16_t tickerY = (h >= 48) ? static_cast<int16_t>((h + 12) / 2 - 4) : 18;
    for (int pass = 0; pass < 2; ++pass) {
        int16_t cx = static_cast<int16_t>(w / 3) - m_scrollOffsetPx + static_cast<int16_t>(pass * m_maxScrollPx);
        for (uint8_t i = 0; i < limit; ++i) {
            const AgendaEventPod& ev = m_activeEvents[i];
            uint16_t accent = resolveEventAccent(ev, tc);

            if (m_funSprites && cx + 12 > 0 && cx < static_cast<int16_t>(w)) {
                ClockSpriteBank::drawAgendaIcon(surface, ev.spriteId, cx, tickerY - 2, 12, m_animTick);
            }
            if (m_funSprites) cx += 14;

            char dayStr[20]{};
            formatEventDayHeader(ev.startEpoch, nowEpoch, true, dayStr, sizeof(dayStr));
            char timeStr[12]{};
            formatEventTimePill(ev.startEpoch, (ev.flags & AGENDA_FLAG_ALL_DAY) != 0, m_localTzOffsetSec, nowEpoch, timeStr, sizeof(timeStr));

            char metaPill[44]{};
            if (m_showBadges && ev.badgeLabel[0] != '\0') {
                snprintf(metaPill, sizeof(metaPill), "[%s %s %s]", dayStr, timeStr, ev.badgeLabel);
            } else {
                snprintf(metaPill, sizeof(metaPill), "[%s %s]", dayStr, timeStr);
            }
            drawClippedText5x7(surface, metaPill, cx, tickerY, 0, static_cast<int16_t>(w), accent);
            cx += static_cast<int16_t>(strlen(metaPill)) * 6 + 4;

            drawClippedText5x7(surface, ev.title, cx, tickerY, 0, static_cast<int16_t>(w), tc.titleText);
            cx += static_cast<int16_t>(strlen(ev.title)) * 6 + 18;
        }
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
    switch (m_viewMode) {
        case ViewMode::MultiDayScroll:
            renderMultiDayVerticalScroll(surface, w, h, now);
            return;
        case ViewMode::Ticker:
            renderMultiDayTicker(surface, w, h, now);
            return;
        case ViewMode::Cards:
            renderWideCompact(surface, w, h, now);
            return;
        case ViewMode::SplitDeck:
            if (w >= 96 && h >= 48) {
                renderWideHd(surface, w, h, now);
            } else {
                renderSquare(surface, w, h, now);
            }
            return;
        case ViewMode::Auto:
        default:
            break;
    }

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
    {"view_mode", ConfigType::ENUM, "Display Layout Mode", "Visual layout: Auto, Multi-Day Vertical Scroll, Sliding Cards, Split Calendar Deck, or Horizontal Ticker.", "auto", false, "", "", "", "auto,multiday_scroll,cards,split_deck,ticker", "", false, "", ValidationPolicy::FallbackDefault},
    {"theme", ConfigType::ENUM, "Visual Theme", "Color palette and border style for the agenda display.", "cyber_arcade", false, "", "", "", "cyber_arcade,amber_dmd,matrix_green,snes_rpg,minimal_clean", "", false, "", ValidationPolicy::FallbackDefault},
    {"scroll_speed", ConfigType::ENUM, "Scroll Speed", "Scrolling speed for multi-day feed, horizontal ticker, and long event titles.", "normal", false, "", "", "", "slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault},
    {"slide_duration_sec", ConfigType::INTEGER, "Slide / Page Duration (sec)", "Seconds per card or page when cycling through multi-day events.", "5", false, "3", "20", "1", "", "", false, "", ValidationPolicy::Clamp},
    {"cache_ttl_min", ConfigType::INTEGER, "SD Cache Duration (min)", "Duration in minutes to serve cached events from MicroSD before refreshing.", "60", false, "15", "1440", "5", "", "", false, "", ValidationPolicy::Clamp},
    {"horizon_days", ConfigType::INTEGER, "Lookahead Horizon (days)", "Number of upcoming days to scan for events.", "7", false, "1", "30", "1", "", "", false, "", ValidationPolicy::Clamp},
    {"events_shown", ConfigType::INTEGER, "Max Events per Cycle", "Maximum number of upcoming events displayed across the multi-day horizon.", "5", false, "1", "8", "1", "", "", false, "", ValidationPolicy::Clamp},
    {"show_relative_time", ConfigType::BOOLEAN, "Show Relative Countdown", "Display relative day/minute countdowns (e.g. IN 12M, J+3 / +3D).", "true", false},
    {"show_badges", ConfigType::BOOLEAN, "Show Calendar Badges", "Display calendar source badge pills ([Perso], [Pro]) next to events.", "true", false},
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
