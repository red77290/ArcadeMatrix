#include "DataSession.h"
#include <string.h>
#include <new>

namespace mqd {

Limits limitsFor(bool hasPsram) {
    if (hasPsram) return {8192, 288, 6, 49152};
    return {4096, 96, 4, 12288};
}

uint8_t parseTopics(const char* list, char out[][TOPIC_LEN], uint8_t max, uint8_t* tooLong) {
    uint8_t n = 0, skipped = 0;
    const char* p = list ? list : "";
    while (*p && n < max) {
        while (*p == ',' || *p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        const char* start = p;
        while (*p && *p != ',' && *p != '\n' && *p != '\r') p++;
        const char* end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        size_t len = (size_t)(end - start);
        if (len == 0) continue;
        if (len >= TOPIC_LEN) { skipped++; continue; }
        char t[TOPIC_LEN];
        memcpy(t, start, len);
        t[len] = '\0';
        bool dup = false;
        for (uint8_t i = 0; i < n; i++) dup = dup || strcmp(out[i], t) == 0;
        if (dup) continue;
        memcpy(out[n++], t, len + 1);
    }
    if (tooLong) *tooLong = skipped;
    return n;
}

uint8_t subPageCount(const View& v) {
    if (v.kind != feed::Kind::Weather || !v.weather.valid) return 1;
    uint8_t days = v.weather.dayCount < MAX_WEATHER_DAYS ? v.weather.dayCount : MAX_WEATHER_DAYS;
    uint8_t n = (uint8_t)((v.weather.hasCurrent ? 1 : 0) + days);
    return n ? n : 1;
}

void advance(Cycle& c, uint8_t topicCount, uint8_t currentSubPages, uint32_t nowMs) {
    c.sinceMs = nowMs;
    if (c.sub + 1 < currentSubPages) {
        c.sub++;
        return;
    }
    c.sub = 0;
    c.topic = topicCount ? (uint8_t)((c.topic + 1) % topicCount) : 0;
}

bool Session::create(const Limits& limits, const char topics[][TOPIC_LEN], uint8_t count, const Allocator& a) {
    destroy();
    if (count == 0 || !a.alloc || !a.release) return false;
    if (count > limits.maxTopics) count = limits.maxTopics;
    m_limits = limits;
    m_alloc = a;
    m_count = count;
    const size_t pool = graph::poolBytes(limits.points);

    void* pages = a.alloc(sizeof(Page) * count);
    m_pools = static_cast<float*>(a.alloc(pool * count));
    void* view = a.alloc(sizeof(View));
    m_viewPool = static_cast<float*>(a.alloc(pool));
    if (pages) m_pages = static_cast<Page*>(pages);
    if (view) m_view = static_cast<View*>(view);
    if (!m_pages || !m_pools || !m_view || !m_viewPool) {
        // Free what did arrive; nothing was constructed yet.
        if (pages) a.release(pages);
        if (m_pools) a.release(m_pools);
        if (view) a.release(view);
        if (m_viewPool) a.release(m_viewPool);
        m_pages = nullptr; m_pools = nullptr; m_view = nullptr; m_viewPool = nullptr; m_count = 0;
        return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        Page* p = new (&m_pages[i]) Page();
        memset(&p->data, 0, sizeof(View));
        strncpy(p->topic, topics[i], TOPIC_LEN - 1);
        p->topic[TOPIC_LEN - 1] = '\0';
        p->seq.store(0, std::memory_order_relaxed);
        p->cycleMs.store(DEFAULT_SECONDS * 1000u, std::memory_order_relaxed);
        p->data.kind = feed::Kind::None;
        p->data.seconds = DEFAULT_SECONDS;
        graph::attachPool(p->data.graph, m_pools + (size_t)i * graph::MAX_SERIES * limits.points, limits.points);
    }
    memset(m_view, 0, sizeof(View));
    m_view->kind = feed::Kind::None;
    m_view->seconds = DEFAULT_SECONDS;
    graph::attachPool(m_view->graph, m_viewPool, limits.points);
    return true;
}

void Session::destroy() {
    if (!m_pages && !m_pools && !m_view && !m_viewPool) return;
    for (uint8_t i = 0; m_pages && i < m_count; i++) m_pages[i].~Page();
    if (m_pages) m_alloc.release(m_pages);
    if (m_pools) m_alloc.release(m_pools);
    if (m_view) m_alloc.release(m_view);
    if (m_viewPool) m_alloc.release(m_viewPool);
    m_pages = nullptr; m_pools = nullptr; m_view = nullptr; m_viewPool = nullptr;
    m_count = 0;
}

const char* Session::topic(uint8_t i) const {
    return (m_pages && i < m_count) ? m_pages[i].topic : "";
}

bool Session::handle(const char* topic, char* payload, size_t length, JsonDocument& doc) {
    if (!m_pages || !topic) return false;
    Page* page = nullptr;
    for (uint8_t i = 0; i < m_count && !page; i++) {
        if (strcmp(m_pages[i].topic, topic) == 0) page = &m_pages[i];
    }
    if (!page) return false;

    // Empty: the retained message was deleted (no data). Too big or not JSON: Unsupported.
    feed::Kind kind = feed::Kind::None;
    uint16_t seconds = DEFAULT_SECONDS;
    doc.clear();
    if (length > 0) {
        kind = feed::Kind::Unsupported;
        if (length <= m_limits.payloadBytes && deserializeJson(doc, payload, length) == DeserializationError::Ok) {
            kind = feed::detectKind(doc);
            seconds = feed::pageSeconds(doc);
        }
    }

    // Seqlock write: odd while the page is being changed; readers retry on a later frame.
    page->seq.fetch_add(1, std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    View& d = page->data;
    bool ok = false;
    switch (kind) {
        case feed::Kind::Value:   ok = feed::parseValue(doc, d.values); break;
        case feed::Kind::Table:   ok = feed::parseTable(doc, d.values); break;
        case feed::Kind::Graph:   ok = graph::parseDocument(doc, d.graph); break;
        case feed::Kind::Weather: ok = feed::parseWeather(doc, d.weather); break;
        default: break;
    }
    if (kind != feed::Kind::None && kind != feed::Kind::Unsupported && !ok) kind = feed::Kind::Unsupported;
    d.kind = kind;
    d.seconds = seconds;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    page->seq.fetch_add(1, std::memory_order_acq_rel);
    page->cycleMs.store((uint32_t)seconds * 1000u * subPageCount(d), std::memory_order_release);
    return ok;
}

uint32_t Session::cycleMs() const {
    uint32_t total = 0;
    for (uint8_t i = 0; m_pages && i < m_count; i++) total += m_pages[i].cycleMs.load(std::memory_order_acquire);
    return total;
}

uint32_t Session::sequence(uint8_t i) const {
    return (m_pages && i < m_count) ? (m_pages[i].seq.load(std::memory_order_acquire) & ~1u) : 0;
}

bool Session::snapshot(uint8_t i) {
    if (!m_pages || !m_view || i >= m_count) return false;
    const Page& p = m_pages[i];
    const uint32_t before = p.seq.load(std::memory_order_acquire);
    if (before & 1u) return false;
    m_view->kind = p.data.kind;
    m_view->seconds = p.data.seconds;
    graph::copyGraph(m_view->graph, p.data.graph);
    memcpy(&m_view->values, &p.data.values, sizeof(feed::ValuesData));
    memcpy(&m_view->weather, &p.data.weather, sizeof(feed::WeatherFeed));
    std::atomic_thread_fence(std::memory_order_acquire);
    if (p.seq.load(std::memory_order_acquire) != before) {
        m_view->kind = feed::Kind::None;   // torn copy: drawn as nothing until the retry succeeds
        return false;
    }
    return true;
}

}  // namespace mqd
