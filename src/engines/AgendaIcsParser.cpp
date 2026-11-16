#include "AgendaIcsParser.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#if defined(HARDWARE_PROFILE_ESP32_DEV)
#pragma GCC optimize("Os")
#endif

namespace {

// Civil date to Unix days (Howard Hinnant's O(1) algorithm, zero timezone side effects)
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= (m <= 2) ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
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

bool isLeapYear(int y) {
    return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

unsigned daysInMonth(int y, unsigned m) {
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && isLeapYear(y)) return 29;
    if (m >= 1 && m <= 12) return kDays[m - 1];
    return 30;
}

// 0 = Sunday .. 6 = Saturday
unsigned weekdayFromDays(int64_t z) {
    int64_t r = (z + 4) % 7;
    if (r < 0) r += 7;
    return static_cast<unsigned>(r);
}

uint32_t toEpochUtc(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss, bool isUtc, int32_t localTzOffsetSec) {
    int64_t days = daysFromCivil(y, m, d);
    int64_t secs = days * 86400LL + static_cast<int64_t>(hh) * 3600LL + static_cast<int64_t>(mm) * 60LL + static_cast<int64_t>(ss);
    if (!isUtc) {
        secs -= static_cast<int64_t>(localTzOffsetSec);
    }
    if (secs < 0) return 0;
    return static_cast<uint32_t>(secs);
}

bool parseIcsDateTimeValue(const char* val, bool forceDateOnly, int32_t localTzOffsetSec,
                           uint32_t& outEpoch, bool& outAllDay, int& outYear, unsigned& outMonth, unsigned& outDay,
                           unsigned& outHour, unsigned& outMin, unsigned& outSec, bool& outIsUtc) {
    if (!val) return false;
    while (*val == ' ' || *val == '\t') val++;
    size_t len = strlen(val);
    if (len < 8) return false;
    for (int i = 0; i < 8; ++i) {
        if (!isdigit(static_cast<unsigned char>(val[i]))) return false;
    }

    outYear = (val[0] - '0') * 1000 + (val[1] - '0') * 100 + (val[2] - '0') * 10 + (val[3] - '0');
    outMonth = static_cast<unsigned>((val[4] - '0') * 10 + (val[5] - '0'));
    outDay = static_cast<unsigned>((val[6] - '0') * 10 + (val[7] - '0'));
    if (outMonth < 1 || outMonth > 12 || outDay < 1 || outDay > 31) return false;

    if (forceDateOnly || val[8] != 'T' || len < 15) {
        outAllDay = true;
        outHour = 0;
        outMin = 0;
        outSec = 0;
        outIsUtc = false;
        outEpoch = toEpochUtc(outYear, outMonth, outDay, 0, 0, 0, false, localTzOffsetSec);
        return true;
    }

    for (int i = 9; i < 15; ++i) {
        if (!isdigit(static_cast<unsigned char>(val[i]))) return false;
    }
    outAllDay = false;
    outHour = static_cast<unsigned>((val[9] - '0') * 10 + (val[10] - '0'));
    outMin  = static_cast<unsigned>((val[11] - '0') * 10 + (val[12] - '0'));
    outSec  = static_cast<unsigned>((val[13] - '0') * 10 + (val[14] - '0'));
    outIsUtc = (val[15] == 'Z' || val[15] == 'z');
    outEpoch = toEpochUtc(outYear, outMonth, outDay, outHour, outMin, outSec, outIsUtc, localTzOffsetSec);
    return true;
}

uint32_t parseIcsDurationSeconds(const char* val) {
    if (!val) return 0;
    while (*val == ' ') val++;
    if (*val != 'P' && *val != 'p') return 0;
    val++;
    uint32_t totalSec = 0;
    uint32_t curNum = 0;
    bool inTime = false;
    while (*val) {
        char c = *val++;
        if (c == 'T' || c == 't') {
            inTime = true;
            curNum = 0;
        } else if (isdigit(static_cast<unsigned char>(c))) {
            curNum = curNum * 10U + static_cast<uint32_t>(c - '0');
        } else if (c == 'W' || c == 'w') {
            totalSec += curNum * 7U * 86400U;
            curNum = 0;
        } else if (c == 'D' || c == 'd') {
            totalSec += curNum * 86400U;
            curNum = 0;
        } else if (inTime && (c == 'H' || c == 'h')) {
            totalSec += curNum * 3600U;
            curNum = 0;
        } else if (inTime && (c == 'M' || c == 'm')) {
            totalSec += curNum * 60U;
            curNum = 0;
        } else if (inTime && (c == 'S' || c == 's')) {
            totalSec += curNum;
            curNum = 0;
        }
    }
    return totalSec;
}

enum class RRuleFreq : uint8_t {
    None = 0,
    Daily,
    Weekly,
    Monthly,
    Yearly
};

struct RRuleSpec {
    RRuleFreq freq = RRuleFreq::None;
    uint16_t interval = 1;
    uint16_t count = 0;
    uint32_t untilEpoch = 0;
    uint8_t byDayMask = 0; // bit0=SU .. bit6=SA
};

void parseRRule(const char* val, int32_t localTzOffsetSec, RRuleSpec& out) {
    out = RRuleSpec{};
    if (!val || *val == '\0') return;

    const char* p = val;
    while (*p) {
        const char* semi = strchr(p, ';');
        size_t tokLen = semi ? static_cast<size_t>(semi - p) : strlen(p);

        if (tokLen > 5 && strncasecmp(p, "FREQ=", 5) == 0) {
            const char* fv = p + 5;
            if (strncasecmp(fv, "YEARLY", 6) == 0) out.freq = RRuleFreq::Yearly;
            else if (strncasecmp(fv, "MONTHLY", 7) == 0) out.freq = RRuleFreq::Monthly;
            else if (strncasecmp(fv, "WEEKLY", 6) == 0) out.freq = RRuleFreq::Weekly;
            else if (strncasecmp(fv, "DAILY", 5) == 0) out.freq = RRuleFreq::Daily;
        } else if (tokLen > 9 && strncasecmp(p, "INTERVAL=", 9) == 0) {
            int iv = atoi(p + 9);
            if (iv > 0 && iv <= 1000) out.interval = static_cast<uint16_t>(iv);
        } else if (tokLen > 6 && strncasecmp(p, "COUNT=", 6) == 0) {
            int cnt = atoi(p + 6);
            if (cnt > 0 && cnt <= 10000) out.count = static_cast<uint16_t>(cnt);
        } else if (tokLen > 6 && strncasecmp(p, "UNTIL=", 6) == 0) {
            char ubuf[24]{};
            size_t ulen = (tokLen - 6 < sizeof(ubuf) - 1) ? (tokLen - 6) : (sizeof(ubuf) - 1);
            memcpy(ubuf, p + 6, ulen);
            ubuf[ulen] = '\0';
            uint32_t uEpoch = 0;
            bool uAllDay = false, uUtc = false;
            int uY = 0; unsigned uM = 0, uD = 0, uH = 0, uMin = 0, uS = 0;
            if (parseIcsDateTimeValue(ubuf, false, localTzOffsetSec, uEpoch, uAllDay, uY, uM, uD, uH, uMin, uS, uUtc)) {
                if (uAllDay) uEpoch += 86399U;
                out.untilEpoch = uEpoch;
            }
        } else if (tokLen > 6 && strncasecmp(p, "BYDAY=", 6) == 0) {
            const char* bd = p + 6;
            const char* bdEnd = p + tokLen;
            while (bd < bdEnd) {
                while (bd < bdEnd && (*bd == ',' || *bd == ' ' || *bd == '+' || *bd == '-' || isdigit(static_cast<unsigned char>(*bd)))) {
                    bd++;
                }
                if (bd + 2 <= bdEnd) {
                    if (strncasecmp(bd, "SU", 2) == 0) out.byDayMask |= (1U << 0);
                    else if (strncasecmp(bd, "MO", 2) == 0) out.byDayMask |= (1U << 1);
                    else if (strncasecmp(bd, "TU", 2) == 0) out.byDayMask |= (1U << 2);
                    else if (strncasecmp(bd, "WE", 2) == 0) out.byDayMask |= (1U << 3);
                    else if (strncasecmp(bd, "TH", 2) == 0) out.byDayMask |= (1U << 4);
                    else if (strncasecmp(bd, "FR", 2) == 0) out.byDayMask |= (1U << 5);
                    else if (strncasecmp(bd, "SA", 2) == 0) out.byDayMask |= (1U << 6);
                    bd += 2;
                } else {
                    break;
                }
            }
        }

        if (!semi) break;
        p = semi + 1;
    }
}

struct VEventWorkingState {
    bool inEvent = false;
    bool cancelled = false;
    bool hasStart = false;
    bool hasEnd = false;
    bool allDay = false;
    bool isUtc = false;
    int startY = 0;
    unsigned startM = 1, startD = 1, startH = 0, startMin = 0, startS = 0;
    uint32_t startEpoch = 0;
    uint32_t endEpoch = 0;
    uint32_t durationSec = 0;
    uint8_t spriteId = ClockSpriteBank::SPRITE_CALENDAR_DEFAULT;
    char title[64]{};
    char location[20]{};
    char rruleRaw[128]{};
    uint32_t exdateYmd[8]{};
    uint8_t exdateCount = 0;

    void reset() {
        *this = VEventWorkingState{};
    }

    bool isExcludedDate(int y, unsigned m, unsigned d) const {
        uint32_t ymd = static_cast<uint32_t>(y) * 10000U + m * 100U + d;
        for (uint8_t i = 0; i < exdateCount; ++i) {
            if (exdateYmd[i] == ymd) return true;
        }
        return false;
    }
};

void finalizeAndInsertVEvent(
    VEventWorkingState& st,
    uint32_t nowEpoch,
    uint32_t horizonEndEpoch,
    int32_t localTzOffsetSec,
    const AgendaSourceMeta& sourceMeta,
    AgendaEventPod* topEvents,
    size_t& currentCount,
    size_t maxEvents
) {
    if (!st.hasStart || st.cancelled || st.title[0] == '\0') return;

    uint32_t dur = st.durationSec;
    if (st.hasEnd && st.endEpoch > st.startEpoch) {
        dur = st.endEpoch - st.startEpoch;
    }
    if (dur == 0) {
        dur = st.allDay ? 86400U : 3600U;
    }

    auto emitCandidate = [&](uint32_t candStart, bool recurring) {
        uint32_t candEnd = candStart + dur;
        if (candEnd <= nowEpoch || candStart > horizonEndEpoch) return;

        AgendaEventPod pod{};
        pod.startEpoch = candStart;
        pod.endEpoch = candEnd;
        pod.calendarColor565 = sourceMeta.color565;
        pod.flags = 0;
        if (st.allDay) pod.flags |= AGENDA_FLAG_ALL_DAY;
        if (recurring) pod.flags |= AGENDA_FLAG_RECURRING;
        if (sourceMeta.sourceIdx != 0) pod.flags |= AGENDA_FLAG_SOURCE_IDX;
        pod.spriteId = st.spriteId;
        strncpy(pod.title, st.title, sizeof(pod.title) - 1);
        pod.title[sizeof(pod.title) - 1] = '\0';

        if (st.location[0] != '\0') {
            strncpy(pod.badgeLabel, st.location, sizeof(pod.badgeLabel) - 1);
        } else {
            strncpy(pod.badgeLabel, sourceMeta.defaultBadge, sizeof(pod.badgeLabel) - 1);
        }
        pod.badgeLabel[sizeof(pod.badgeLabel) - 1] = '\0';

        AgendaIcsParser::insertEventSorted(pod, topEvents, currentCount, maxEvents);
    };

    if (st.rruleRaw[0] == '\0') {
        if (!st.isExcludedDate(st.startY, st.startM, st.startD)) {
            emitCandidate(st.startEpoch, false);
        }
        return;
    }

    RRuleSpec rr;
    parseRRule(st.rruleRaw, localTzOffsetSec, rr);
    if (rr.freq == RRuleFreq::None) {
        emitCandidate(st.startEpoch, false);
        return;
    }

    // Evaluate local date at nowEpoch to bound O(1) recurrence search
    int64_t nowLocalSec = static_cast<int64_t>(nowEpoch) + (st.isUtc ? 0LL : static_cast<int64_t>(localTzOffsetSec));
    int64_t nowDays = (nowLocalSec >= 0) ? (nowLocalSec / 86400LL) : ((nowLocalSec - 86399LL) / 86400LL);
    int nowY = 1970; unsigned nowM = 1, nowD = 1;
    civilFromDays(nowDays, nowY, nowM, nowD);

    int64_t startDays = daysFromCivil(st.startY, st.startM, st.startD);

    if (rr.freq == RRuleFreq::Yearly) {
        for (int y = nowY; y <= nowY + 1; ++y) {
            if (y < st.startY) continue;
            int diffY = y - st.startY;
            if ((diffY % rr.interval) != 0) continue;
            if (rr.count > 0 && static_cast<uint32_t>(diffY / rr.interval) >= rr.count) continue;
            unsigned d = st.startD;
            if (st.startM == 2 && d == 29 && !isLeapYear(y)) d = 28;
            if (st.isExcludedDate(y, st.startM, d)) continue;
            uint32_t occStart = toEpochUtc(y, st.startM, d, st.startH, st.startMin, st.startS, st.isUtc, localTzOffsetSec);
            if (rr.untilEpoch > 0 && occStart > rr.untilEpoch) continue;
            emitCandidate(occStart, true);
        }
    } else if (rr.freq == RRuleFreq::Monthly) {
        int startMonths = st.startY * 12 + static_cast<int>(st.startM) - 1;
        int curMonths = nowY * 12 + static_cast<int>(nowM) - 1;
        for (int mIdx = curMonths; mIdx <= curMonths + 2; ++mIdx) {
            if (mIdx < startMonths) continue;
            int diffM = mIdx - startMonths;
            if ((diffM % rr.interval) != 0) continue;
            if (rr.count > 0 && static_cast<uint32_t>(diffM / rr.interval) >= rr.count) continue;
            int y = mIdx / 12;
            unsigned m = static_cast<unsigned>((mIdx % 12) + 1);
            unsigned maxD = daysInMonth(y, m);
            if (st.startD > maxD) continue;
            if (st.isExcludedDate(y, m, st.startD)) continue;
            uint32_t occStart = toEpochUtc(y, m, st.startD, st.startH, st.startMin, st.startS, st.isUtc, localTzOffsetSec);
            if (rr.untilEpoch > 0 && occStart > rr.untilEpoch) continue;
            emitCandidate(occStart, true);
        }
    } else if (rr.freq == RRuleFreq::Daily) {
        int64_t firstDay = (nowDays > startDays) ? nowDays : startDays;
        for (int64_t day = firstDay - 1; day <= nowDays + 31; ++day) {
            if (day < startDays) continue;
            int64_t diffD = day - startDays;
            if ((diffD % rr.interval) != 0) continue;
            if (rr.count > 0 && static_cast<uint32_t>(diffD / rr.interval) >= rr.count) break;
            int y = 0; unsigned m = 1, d = 1;
            civilFromDays(day, y, m, d);
            if (st.isExcludedDate(y, m, d)) continue;
            uint32_t occStart = toEpochUtc(y, m, d, st.startH, st.startMin, st.startS, st.isUtc, localTzOffsetSec);
            if (rr.untilEpoch > 0 && occStart > rr.untilEpoch) break;
            emitCandidate(occStart, true);
            if (occStart > horizonEndEpoch) break;
        }
    } else if (rr.freq == RRuleFreq::Weekly) {
        uint8_t mask = rr.byDayMask;
        if (mask == 0) {
            mask = static_cast<uint8_t>(1U << weekdayFromDays(startDays));
        }
        // Align week origin to the Sunday of startDays' week
        int64_t startWeekSun = startDays - static_cast<int64_t>(weekdayFromDays(startDays));
        int64_t firstDay = (nowDays > startDays) ? nowDays : startDays;
        uint32_t emittedCount = 0;
        for (int64_t day = firstDay - 1; day <= nowDays + 31; ++day) {
            if (day < startDays) continue;
            unsigned wd = weekdayFromDays(day);
            if (((mask >> wd) & 1U) == 0U) continue;
            int64_t candWeekSun = day - static_cast<int64_t>(wd);
            int64_t diffWeeks = (candWeekSun - startWeekSun) / 7LL;
            if (diffWeeks < 0 || (diffWeeks % rr.interval) != 0) continue;
            int y = 0; unsigned m = 1, d = 1;
            civilFromDays(day, y, m, d);
            if (st.isExcludedDate(y, m, d)) continue;
            uint32_t occStart = toEpochUtc(y, m, d, st.startH, st.startMin, st.startS, st.isUtc, localTzOffsetSec);
            if (rr.untilEpoch > 0 && occStart > rr.untilEpoch) break;
            emitCandidate(occStart, true);
            emittedCount++;
            if (rr.count > 0 && emittedCount >= rr.count) break;
            if (occStart > horizonEndEpoch) break;
        }
    }
}

void processLogicalIcsLine(
    const char* line,
    int32_t localTzOffsetSec,
    uint32_t nowEpoch,
    uint32_t horizonEndEpoch,
    const AgendaSourceMeta& sourceMeta,
    VEventWorkingState& st,
    AgendaEventPod* topEvents,
    size_t& currentCount,
    size_t maxEvents
) {
    if (!line || *line == '\0') return;

    if (strcasecmp(line, "BEGIN:VEVENT") == 0) {
        st.reset();
        st.inEvent = true;
        return;
    }
    if (strcasecmp(line, "END:VEVENT") == 0) {
        if (st.inEvent) {
            finalizeAndInsertVEvent(st, nowEpoch, horizonEndEpoch, localTzOffsetSec, sourceMeta, topEvents, currentCount, maxEvents);
        }
        st.reset();
        return;
    }
    if (!st.inEvent) return;

    const char* colon = strchr(line, ':');
    if (!colon) return;
    const char* val = colon + 1;
    size_t keyLen = static_cast<size_t>(colon - line);

    if (keyLen >= 6 && strncasecmp(line, "STATUS", 6) == 0) {
        if (strncasecmp(val, "CANCELLED", 9) == 0) {
            st.cancelled = true;
        }
    } else if (keyLen >= 7 && strncasecmp(line, "SUMMARY", 7) == 0 && (line[7] == ':' || line[7] == ';')) {
        st.spriteId = AgendaSpriteResolver::resolveSpriteId(val);
        AgendaIcsParser::normalizeIcsText(val, st.title, sizeof(st.title));
    } else if (keyLen >= 8 && strncasecmp(line, "LOCATION", 8) == 0 && (line[8] == ':' || line[8] == ';')) {
        AgendaIcsParser::normalizeIcsText(val, st.location, sizeof(st.location));
    } else if (keyLen >= 7 && strncasecmp(line, "DTSTART", 7) == 0 && (line[7] == ':' || line[7] == ';')) {
        bool forceDate = false;
        for (const char* p = line + 7; p < colon; ++p) {
            if (strncasecmp(p, "VALUE=DATE", 10) == 0 && strncasecmp(p, "VALUE=DATE-TIME", 15) != 0) {
                forceDate = true;
                break;
            }
        }
        st.hasStart = parseIcsDateTimeValue(val, forceDate, localTzOffsetSec,
                                            st.startEpoch, st.allDay, st.startY, st.startM, st.startD,
                                            st.startH, st.startMin, st.startS, st.isUtc);
    } else if (keyLen >= 5 && strncasecmp(line, "DTEND", 5) == 0 && (line[5] == ':' || line[5] == ';')) {
        bool forceDate = false;
        for (const char* p = line + 5; p < colon; ++p) {
            if (strncasecmp(p, "VALUE=DATE", 10) == 0 && strncasecmp(p, "VALUE=DATE-TIME", 15) != 0) {
                forceDate = true;
                break;
            }
        }
        bool endAllDay = false, endUtc = false;
        int eY = 0; unsigned eM = 0, eD = 0, eH = 0, eMin = 0, eS = 0;
        st.hasEnd = parseIcsDateTimeValue(val, forceDate, localTzOffsetSec,
                                          st.endEpoch, endAllDay, eY, eM, eD, eH, eMin, eS, endUtc);
    } else if (keyLen >= 8 && strncasecmp(line, "DURATION", 8) == 0 && (line[8] == ':' || line[8] == ';')) {
        st.durationSec = parseIcsDurationSeconds(val);
    } else if (keyLen >= 5 && strncasecmp(line, "RRULE", 5) == 0 && (line[5] == ':' || line[5] == ';')) {
        strncpy(st.rruleRaw, val, sizeof(st.rruleRaw) - 1);
        st.rruleRaw[sizeof(st.rruleRaw) - 1] = '\0';
    } else if (keyLen >= 6 && strncasecmp(line, "EXDATE", 6) == 0 && (line[6] == ':' || line[6] == ';')) {
        const char* p = val;
        while (*p && st.exdateCount < 8) {
            while (*p == ',' || *p == ' ') p++;
            if (strlen(p) >= 8 && isdigit(static_cast<unsigned char>(p[0]))) {
                uint32_t y = static_cast<uint32_t>((p[0] - '0') * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0'));
                uint32_t m = static_cast<uint32_t>((p[4] - '0') * 10 + (p[5] - '0'));
                uint32_t d = static_cast<uint32_t>((p[6] - '0') * 10 + (p[7] - '0'));
                st.exdateYmd[st.exdateCount++] = y * 10000U + m * 100U + d;
            }
            const char* comma = strchr(p, ',');
            if (!comma) break;
            p = comma + 1;
        }
    }
}

bool containsWordOrSubstr(const char* lowerAscii, const char* needle) {
    return strstr(lowerAscii, needle) != nullptr;
}

} // namespace

uint8_t AgendaSpriteResolver::resolveSpriteId(const char* rawUtf8Summary) {
    if (!rawUtf8Summary || *rawUtf8Summary == '\0') {
        return ClockSpriteBank::SPRITE_CALENDAR_DEFAULT;
    }

    // 1. Check UTF-8 emoji sequences before ASCII folding
    if (strstr(rawUtf8Summary, "\xF0\x9F\x8E\x82") || // 🎂
        strstr(rawUtf8Summary, "\xF0\x9F\x8E\x89")) { // 🎉
        return ClockSpriteBank::SPRITE_BIRTHDAY_CAKE;
    }
    if (strstr(rawUtf8Summary, "\xF0\x9F\x92\xBB")) { // 💻
        return ClockSpriteBank::SPRITE_CYBER_CALL;
    }
    if (strstr(rawUtf8Summary, "\xF0\x9F\xA9\xBA") || // 🩺
        strstr(rawUtf8Summary, "\xE2\x9A\x95")) {     // ⚕
        return ClockSpriteBank::SPRITE_MEDICAL_HEART;
    }
    if (strstr(rawUtf8Summary, "\xF0\x9F\x8D\x95") || // 🍕
        strstr(rawUtf8Summary, "\xF0\x9F\x8D\xBB") || // 🍻
        strstr(rawUtf8Summary, "\xF0\x9F\x8D\x94")) { // 🍔
        return ClockSpriteBank::SPRITE_FOOD_PIZZA;
    }
    if (strstr(rawUtf8Summary, "\xE2\x9A\xBD") ||     // ⚽
        strstr(rawUtf8Summary, "\xF0\x9F\x8F\x8B")) { // 🏋
        return ClockSpriteBank::SPRITE_SPORT_SONIC;
    }
    if (strstr(rawUtf8Summary, "\xE2\x9C\x88") ||     // ✈
        strstr(rawUtf8Summary, "\xF0\x9F\x9A\x86")) { // 🚆
        return ClockSpriteBank::SPRITE_TRAVEL_PLANE;
    }
    if (strstr(rawUtf8Summary, "\xF0\x9F\x8E\xAE")) { // 🎮
        return ClockSpriteBank::SPRITE_GAMING_PACMAN;
    }

    // 2. Normalize to lowercase ASCII without accents for keyword matching
    char clean[96]{};
    AgendaIcsParser::normalizeIcsText(rawUtf8Summary, clean, sizeof(clean));
    for (size_t i = 0; clean[i] != '\0'; ++i) {
        clean[i] = static_cast<char>(tolower(static_cast<unsigned char>(clean[i])));
    }

    if (containsWordOrSubstr(clean, "anniv") ||
        containsWordOrSubstr(clean, "birthday") ||
        containsWordOrSubstr(clean, "cumple")) {
        return ClockSpriteBank::SPRITE_BIRTHDAY_CAKE;
    }
    if (containsWordOrSubstr(clean, "call") ||
        containsWordOrSubstr(clean, "reunion") ||
        containsWordOrSubstr(clean, "meeting") ||
        containsWordOrSubstr(clean, "teams") ||
        containsWordOrSubstr(clean, "zoom") ||
        containsWordOrSubstr(clean, "daily") ||
        containsWordOrSubstr(clean, "standup")) {
        return ClockSpriteBank::SPRITE_CYBER_CALL;
    }
    if (containsWordOrSubstr(clean, "medecin") ||
        containsWordOrSubstr(clean, "dentiste") ||
        containsWordOrSubstr(clean, "docteur") ||
        containsWordOrSubstr(clean, "medical") ||
        containsWordOrSubstr(clean, "veto") ||
        containsWordOrSubstr(clean, "doctor")) {
        return ClockSpriteBank::SPRITE_MEDICAL_HEART;
    }
    if (containsWordOrSubstr(clean, "resto") ||
        containsWordOrSubstr(clean, "diner") ||
        containsWordOrSubstr(clean, "dej") ||
        containsWordOrSubstr(clean, "apero") ||
        containsWordOrSubstr(clean, "pizza") ||
        containsWordOrSubstr(clean, "burger") ||
        containsWordOrSubstr(clean, "lunch")) {
        return ClockSpriteBank::SPRITE_FOOD_PIZZA;
    }
    if (containsWordOrSubstr(clean, "sport") ||
        containsWordOrSubstr(clean, "foot") ||
        containsWordOrSubstr(clean, "tennis") ||
        containsWordOrSubstr(clean, "padel") ||
        containsWordOrSubstr(clean, "salle") ||
        containsWordOrSubstr(clean, "gym") ||
        containsWordOrSubstr(clean, "run")) {
        return ClockSpriteBank::SPRITE_SPORT_SONIC;
    }
    if (containsWordOrSubstr(clean, "train") ||
        containsWordOrSubstr(clean, "sncf") ||
        containsWordOrSubstr(clean, "vol ") ||
        containsWordOrSubstr(clean, "avion") ||
        containsWordOrSubstr(clean, "vacances") ||
        containsWordOrSubstr(clean, "flight")) {
        return ClockSpriteBank::SPRITE_TRAVEL_PLANE;
    }
    if (containsWordOrSubstr(clean, "jeu") ||
        containsWordOrSubstr(clean, "arcade") ||
        containsWordOrSubstr(clean, "lan") ||
        containsWordOrSubstr(clean, "gaming") ||
        containsWordOrSubstr(clean, "stream")) {
        return ClockSpriteBank::SPRITE_GAMING_PACMAN;
    }

    return ClockSpriteBank::SPRITE_CALENDAR_DEFAULT;
}

uint32_t AgendaIcsParser::computeCrc32(const void* data, size_t length, uint32_t seed) {
    uint32_t crc = ~seed;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

uint32_t AgendaIcsParser::computeConfigCrc32(const char* url1, const char* url2, int horizonDays) {
    char sep = '|';
    uint32_t crc = 0;
    if (url1) crc = computeCrc32(url1, strlen(url1), crc);
    crc = computeCrc32(&sep, 1, crc);
    if (url2) crc = computeCrc32(url2, strlen(url2), crc);
    crc = computeCrc32(&sep, 1, crc);
    char hbuf[16]{};
    snprintf(hbuf, sizeof(hbuf), "%d", horizonDays);
    crc = computeCrc32(hbuf, strlen(hbuf), crc);
    return crc;
}

void AgendaIcsParser::normalizeIcsUrl(const char* rawUrl, char* outUrl, size_t outSize) {
    if (!outUrl || outSize == 0) return;
    outUrl[0] = '\0';
    if (!rawUrl) return;
    while (*rawUrl == ' ' || *rawUrl == '\t') rawUrl++;
    if (strncasecmp(rawUrl, "webcal://", 9) == 0) {
        snprintf(outUrl, outSize, "https://%s", rawUrl + 9);
    } else if (strncasecmp(rawUrl, "webcals://", 10) == 0) {
        snprintf(outUrl, outSize, "https://%s", rawUrl + 10);
    } else {
        strncpy(outUrl, rawUrl, outSize - 1);
        outUrl[outSize - 1] = '\0';
    }
}

void AgendaIcsParser::normalizeIcsText(const char* rawUtf8, char* outAscii, size_t outSize) {
    if (!outAscii || outSize == 0) return;
    outAscii[0] = '\0';
    if (!rawUtf8) return;

    size_t w = 0;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(rawUtf8);
    while (*s != '\0' && w + 1 < outSize) {
        // RFC 5545 backslash escapes (\n, \N, comma, semicolon, backslash)
        if (*s == '\\' && s[1] != '\0') {
            char next = static_cast<char>(s[1]);
            if (next == 'n' || next == 'N') {
                if (w > 0 && outAscii[w - 1] != ' ') outAscii[w++] = ' ';
            } else {
                outAscii[w++] = next;
            }
            s += 2;
            continue;
        }

        // UTF-8 Latin-1 Supplement (0xC3 0x80..0xBF) -> ASCII folding for 5x7 matrix font
        if (s[0] == 0xC3 && s[1] != 0) {
            uint8_t c2 = s[1];
            char mapped = 0;
            if (c2 >= 0x80 && c2 <= 0x85) mapped = 'A';
            else if (c2 == 0x87) mapped = 'C';
            else if (c2 >= 0x88 && c2 <= 0x8B) mapped = 'E';
            else if (c2 >= 0x8C && c2 <= 0x8F) mapped = 'I';
            else if (c2 == 0x91) mapped = 'N';
            else if (c2 >= 0x92 && c2 <= 0x96) mapped = 'O';
            else if (c2 >= 0x99 && c2 <= 0x9C) mapped = 'U';
            else if (c2 >= 0xA0 && c2 <= 0xA5) mapped = 'a';
            else if (c2 == 0xA7) mapped = 'c';
            else if (c2 >= 0xA8 && c2 <= 0xAB) mapped = 'e';
            else if (c2 >= 0xAC && c2 <= 0xAF) mapped = 'i';
            else if (c2 == 0xB1) mapped = 'n';
            else if (c2 >= 0xB2 && c2 <= 0xB6) mapped = 'o';
            else if (c2 >= 0xB9 && c2 <= 0xBC) mapped = 'u';
            else if (c2 == 0xBD || c2 == 0xBF) mapped = 'y';
            if (mapped != 0) {
                outAscii[w++] = mapped;
            }
            s += 2;
            continue;
        }

        // Skip other multibyte UTF-8 sequences (emojis, smart quotes) cleanly
        if (s[0] >= 0x80) {
            if ((s[0] & 0xE0) == 0xC0 && s[1] != 0) {
                s += 2;
            } else if ((s[0] & 0xF0) == 0xE0 && s[1] != 0 && s[2] != 0) {
                if (s[0] == 0xE2 && s[1] == 0x80 && (s[2] == 0x98 || s[2] == 0x99)) {
                    outAscii[w++] = '\'';
                } else if (s[0] == 0xE2 && s[1] == 0x80 && (s[2] == 0x93 || s[2] == 0x94)) {
                    outAscii[w++] = '-';
                }
                s += 3;
            } else if ((s[0] & 0xF8) == 0xF0 && s[1] != 0 && s[2] != 0 && s[3] != 0) {
                s += 4;
            } else {
                s++;
            }
            continue;
        }

        if (s[0] >= 32 && s[0] < 127) {
            outAscii[w++] = static_cast<char>(s[0]);
        }
        s++;
    }

    // Trim leading/trailing spaces
    while (w > 0 && outAscii[w - 1] == ' ') {
        w--;
    }
    outAscii[w] = '\0';
    size_t lead = 0;
    while (outAscii[lead] == ' ') lead++;
    if (lead > 0) {
        memmove(outAscii, outAscii + lead, w - lead + 1);
    }
}

void AgendaIcsParser::insertEventSorted(
    const AgendaEventPod& ev,
    AgendaEventPod* topEvents,
    size_t& currentCount,
    size_t maxEvents
) {
    if (!topEvents || maxEvents == 0) return;

    // Deduplicate identical (startEpoch, title)
    for (size_t i = 0; i < currentCount; ++i) {
        if (topEvents[i].startEpoch == ev.startEpoch &&
            strncmp(topEvents[i].title, ev.title, sizeof(ev.title)) == 0) {
            return;
        }
    }

    size_t insertPos = currentCount;
    for (size_t i = 0; i < currentCount; ++i) {
        if (ev.startEpoch < topEvents[i].startEpoch ||
            (ev.startEpoch == topEvents[i].startEpoch && ev.endEpoch < topEvents[i].endEpoch)) {
            insertPos = i;
            break;
        }
    }

    if (insertPos >= maxEvents) {
        return;
    }

    if (currentCount < maxEvents) {
        currentCount++;
    }
    for (size_t i = currentCount - 1; i > insertPos; --i) {
        topEvents[i] = topEvents[i - 1];
    }
    topEvents[insertPos] = ev;
}

size_t AgendaIcsParser::parseStream(
    Stream& stream,
    uint32_t nowEpoch,
    int horizonDays,
    int32_t localTzOffsetSec,
    const AgendaSourceMeta& sourceMeta,
    AgendaEventPod* topEvents,
    size_t currentCount,
    size_t maxEvents
) {
    if (horizonDays < 1) horizonDays = 1;
    if (horizonDays > 30) horizonDays = 30;
    const uint32_t horizonEndEpoch = nowEpoch + static_cast<uint32_t>(horizonDays) * 86400U;

    char logicalLine[192]{};
    size_t logicalLen = 0;
    bool hasLogicalLine = false;

    char rawLine[192]{};
    size_t rawLen = 0;

    VEventWorkingState st;

    auto flushPhysicalLine = [&]() {
        rawLine[rawLen] = '\0';
        // RFC 5545 unfolding: lines beginning with SPACE or HTAB continue the previous line
        if (rawLen > 0 && (rawLine[0] == ' ' || rawLine[0] == '\t')) {
            if (hasLogicalLine && logicalLen + 1 < sizeof(logicalLine)) {
                size_t copyLen = rawLen - 1;
                if (logicalLen + copyLen >= sizeof(logicalLine)) {
                    copyLen = sizeof(logicalLine) - 1 - logicalLen;
                }
                memcpy(logicalLine + logicalLen, rawLine + 1, copyLen);
                logicalLen += copyLen;
                logicalLine[logicalLen] = '\0';
            }
        } else {
            if (hasLogicalLine) {
                processLogicalIcsLine(logicalLine, localTzOffsetSec, nowEpoch, horizonEndEpoch,
                                      sourceMeta, st, topEvents, currentCount, maxEvents);
            }
            size_t copyLen = (rawLen < sizeof(logicalLine) - 1) ? rawLen : (sizeof(logicalLine) - 1);
            memcpy(logicalLine, rawLine, copyLen);
            logicalLine[copyLen] = '\0';
            logicalLen = copyLen;
            hasLogicalLine = (rawLen > 0);
        }
        rawLen = 0;
    };

    while (stream.available() > 0) {
        int c = stream.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            flushPhysicalLine();
        } else if (rawLen + 1 < sizeof(rawLine)) {
            rawLine[rawLen++] = static_cast<char>(c);
        }
    }
    if (rawLen > 0) {
        flushPhysicalLine();
    }
    if (hasLogicalLine) {
        processLogicalIcsLine(logicalLine, localTzOffsetSec, nowEpoch, horizonEndEpoch,
                              sourceMeta, st, topEvents, currentCount, maxEvents);
    }

    return currentCount;
}

namespace {
class BufferStream : public Stream {
public:
    BufferStream(const char* buf, size_t len) : m_buf(buf), m_len(len), m_pos(0) {}
    int available() override { return (m_pos < m_len) ? static_cast<int>(m_len - m_pos) : 0; }
    int read() override { return (m_pos < m_len) ? static_cast<unsigned char>(m_buf[m_pos++]) : -1; }
    int peek() override { return (m_pos < m_len) ? static_cast<unsigned char>(m_buf[m_pos]) : -1; }
    void flush() override {}
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }
private:
    const char* m_buf;
    size_t m_len;
    size_t m_pos;
};
} // namespace

size_t AgendaIcsParser::parseBuffer(
    const char* icsBuffer,
    size_t bufferLen,
    uint32_t nowEpoch,
    int horizonDays,
    int32_t localTzOffsetSec,
    const AgendaSourceMeta& sourceMeta,
    AgendaEventPod* topEvents,
    size_t currentCount,
    size_t maxEvents
) {
    if (!icsBuffer || bufferLen == 0) return currentCount;
    BufferStream bs(icsBuffer, bufferLen);
    return parseStream(bs, nowEpoch, horizonDays, localTzOffsetSec, sourceMeta, topEvents, currentCount, maxEvents);
}
