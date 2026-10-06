#pragma once
#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <ArduinoJson.h>
#include "GraphPayload.h"
#include "FeedPayloads.h"

/**
 * Per-activation state of the MQTT Data engine, free of Arduino and FreeRTOS so the host tests cover
 * it: the pages (one per configured topic), the hand-off between the MQTT task (writer) and the
 * render loop (reader), the page cycle and the engine's total time on screen.
 *
 * Everything is allocated by create() when the engine becomes active and released by destroy() when
 * it leaves the rotation; nothing stays resident in between. Sizes come from Limits, which depend
 * on whether the board has PSRAM.
 *
 * Hand-off: each page is a seqlock. The writer makes the sequence odd, parses straight into the page,
 * then makes it even again; a reader copies the page and keeps the copy only if the sequence was even
 * and unchanged across the copy. No lock on either side, and no allocation after create().
 */
namespace mqd {

static constexpr uint8_t MAX_TOPICS = 6;
static constexpr size_t TOPIC_LEN = 96;
static constexpr uint8_t MAX_WEATHER_DAYS = 5;

/// Memory budget of one activation.
struct Limits {
    size_t payloadBytes;   ///< largest payload accepted (the MQTT client's buffer adds headroom)
    uint16_t points;       ///< graph points kept per series (newest win)
    uint8_t maxTopics;     ///< pages per instance
    size_t jsonBytes;      ///< ArduinoJson pool for parsing one payload
};

/// With PSRAM: 8 KB payloads, 288 points, 6 topics. Without (classic ESP32): 4 KB, 96 points, 4 topics.
Limits limitsFor(bool hasPsram);

/// Splits a topic list ("a, b" or one per line) into `out`, trimming blanks and skipping empty and
/// repeated entries. Returns how many were kept (at most `max`); `tooLong` counts entries dropped
/// for not fitting TOPIC_LEN.
uint8_t parseTopics(const char* list, char out[][TOPIC_LEN], uint8_t max, uint8_t* tooLong = nullptr);

struct Allocator {
    void* (*alloc)(size_t bytes);
    void (*release)(void* p);
};

/// A page's parsed payload, as the renderers draw it.
struct View {
    feed::Kind kind;
    uint16_t seconds;          ///< time on this page (weather: on each of its pages); 10 until known
    graph::GraphData graph;
    feed::ValuesData values;
    feed::WeatherFeed weather;
};

/// Sub-pages a page shows: a weather payload expands to its NOW page (when it has a live reading)
/// plus one page per forecast day (up to MAX_WEATHER_DAYS); everything else is one page.
uint8_t subPageCount(const View& v);

/// Seconds a page without a payload yet counts for (docs/MQTT_DATA_CONTRACT.md, "Time on screen").
static constexpr uint16_t DEFAULT_SECONDS = 10;

/// Where the page cycle is: which topic, which of its sub-pages, and since when.
struct Cycle {
    uint8_t topic = 0;
    uint8_t sub = 0;
    uint32_t sinceMs = 0;
};

/// Next sub-page of the current topic, else the first sub-page of the next topic (wrapping).
void advance(Cycle& c, uint8_t topicCount, uint8_t currentSubPages, uint32_t nowMs);

class Session {
public:
    Session() = default;
    ~Session() { destroy(); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// Allocates the pages, their graph point pools and the render view. False (with everything
    /// released again) if memory ran out. Four allocations per activation.
    bool create(const Limits& limits, const char topics[][TOPIC_LEN], uint8_t count, const Allocator& a);
    void destroy();
    bool ready() const { return m_pages != nullptr; }

    uint8_t pageCount() const { return m_count; }
    const char* topic(uint8_t i) const;
    const Limits& limits() const { return m_limits; }

    // ---- writer (the MQTT task) ----
    /**
     * A message arrived: parse it into its topic's page. `payload` is modified (parsed in place).
     * An empty payload empties the page (no data). Invalid JSON, a missing or unknown "type", or
     * content the type cannot draw make the page Unsupported. Returns true when the page can be drawn.
     */
    bool handle(const char* topic, char* payload, size_t length, JsonDocument& doc);

    // ---- reader (the render loop) ----
    /// Time one full cycle of the pages takes: each page's seconds times its sub-pages, a page without
    /// a payload counting DEFAULT_SECONDS. Lock-free; changes as payloads arrive.
    uint32_t cycleMs() const;

    /// Even sequence of page `i` (0 = never written). Changes on every message for that page.
    uint32_t sequence(uint8_t i) const;
    /// Copies page `i` into the session's render view. False while the page is being written.
    bool snapshot(uint8_t i);
    const View& view() const { return *m_view; }

private:
    struct Page {
        char topic[TOPIC_LEN];
        std::atomic<uint32_t> seq;
        std::atomic<uint32_t> cycleMs;   ///< this page's share of a full cycle (seconds x sub-pages)
        View data;
    };

    Limits m_limits{};
    Allocator m_alloc{};
    Page* m_pages = nullptr;
    float* m_pools = nullptr;      ///< count * poolBytes(points), one pool per page
    View* m_view = nullptr;
    float* m_viewPool = nullptr;
    uint8_t m_count = 0;
};

}  // namespace mqd
