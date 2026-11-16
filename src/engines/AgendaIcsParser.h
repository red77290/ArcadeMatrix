#pragma once

#include <Arduino.h>
#if defined(ESP32)
#include <Stream.h>
#else
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() { return -1; }
    virtual void flush() {}
    virtual size_t write(uint8_t) { return 0; }
    virtual size_t write(const uint8_t*, size_t) { return 0; }
};
#endif
#include <stdint.h>
#include <stddef.h>
#include "clocks/ClockSpriteBank.h"

static constexpr uint32_t AGENDA_CACHE_MAGIC = 0x41474E44UL; // "AGND"
static constexpr uint8_t  AGENDA_CACHE_VERSION = 1U;
static constexpr size_t   AGENDA_MAX_CACHED_EVENTS = 12U;

enum AgendaEventFlags : uint8_t {
    AGENDA_FLAG_ALL_DAY    = (1U << 0),
    AGENDA_FLAG_RECURRING  = (1U << 1),
    AGENDA_FLAG_SOURCE_IDX = (1U << 2)
};

// Fixed 24-byte header (Little-Endian on ESP32 and RPi ARM)
struct __attribute__((packed)) AgendaCacheHeader {
    uint32_t magic = AGENDA_CACHE_MAGIC; // 0x41474E44 ("AGND")
    uint8_t  version = AGENDA_CACHE_VERSION;
    uint8_t  recordCount = 0;            // 0..12 events retained and sorted by startEpoch ascending
    uint16_t reserved = 0;               // Alignment
    uint32_t configCrc32 = 0;            // CRC32(ics_url_1 + "|" + ics_url_2 + "|" + horizon_days)
    uint32_t lastFetchEpoch = 0;         // UTC Unix timestamp of last successful fetch
    uint32_t payloadCrc32 = 0;           // CRC32 of the recordCount entries (anti-corruption integrity)
    uint32_t schemaFlags = 0;            // Reserved for future evolution
};

static_assert(sizeof(AgendaCacheHeader) == 24, "AgendaCacheHeader must be 24 bytes");

// Fixed 96-byte POD record (12 * 96 = 1,152 bytes max on disk)
struct __attribute__((packed)) AgendaEventPod {
    uint32_t startEpoch = 0;             // UTC start timestamp
    uint32_t endEpoch = 0;               // UTC end timestamp
    uint16_t calendarColor565 = 0x07FF;  // RGB565 color assigned to calendar source (0 or 1)
    uint8_t  flags = 0;                  // bit0: allDay, bit1: recurring, bit2: sourceIdx (0 or 1)
    uint8_t  spriteId = 0;               // Pre-resolved icon/sprite ID during .ics parsing
    char     title[64]{};                // Normalized title (\n/\, unescaped)
    char     badgeLabel[20]{};           // Short calendar name or room/location badge
};

static_assert(sizeof(AgendaEventPod) == 96, "AgendaEventPod must be 96 bytes");

struct AgendaSourceMeta {
    uint16_t color565 = 0x07FF;
    uint8_t  sourceIdx = 0;
    char     defaultBadge[20] = "Perso";
};

class AgendaSpriteResolver {
public:
    static uint8_t resolveSpriteId(const char* rawUtf8Summary);
};

class AgendaIcsParser {
public:
    static uint32_t computeCrc32(const void* data, size_t length, uint32_t seed = 0U);
    static uint32_t computeConfigCrc32(const char* url1, const char* url2, int horizonDays);

    static void normalizeIcsUrl(const char* rawUrl, char* outUrl, size_t outSize);
    static void normalizeIcsText(const char* rawUtf8, char* outAscii, size_t outSize);

    /**
     * @brief Parses a streaming RFC 5545 iCalendar (.ics) feed with zero heap allocations.
     * Unfolds 75-octet folded lines, evaluates RRULE recurrences in O(1), and insertion-sorts
     * upcoming events in [nowEpoch, nowEpoch + horizonDays * 86400] into topEvents[0..maxEvents-1].
     */
    static size_t parseStream(
        Stream& stream,
        uint32_t nowEpoch,
        int horizonDays,
        int32_t localTzOffsetSec,
        const AgendaSourceMeta& sourceMeta,
        AgendaEventPod* topEvents,
        size_t currentCount,
        size_t maxEvents = AGENDA_MAX_CACHED_EVENTS
    );

    static size_t parseBuffer(
        const char* icsBuffer,
        size_t bufferLen,
        uint32_t nowEpoch,
        int horizonDays,
        int32_t localTzOffsetSec,
        const AgendaSourceMeta& sourceMeta,
        AgendaEventPod* topEvents,
        size_t currentCount,
        size_t maxEvents = AGENDA_MAX_CACHED_EVENTS
    );

    static void insertEventSorted(
        const AgendaEventPod& ev,
        AgendaEventPod* topEvents,
        size_t& currentCount,
        size_t maxEvents
    );
};
