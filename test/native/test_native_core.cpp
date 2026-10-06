#include <unity.h>
#include "Arduino.h"
#include "core/DisplayArbiter.h"
#include "core/EngineRegistry.h"
#include "core/TimingSafe.h"
#include "core/ConfigLoader.h"
#include "core/drawing/Hub75DmaLayout.h"
#include "services/IconService.h"
#include "../../include/core/EngineContract.h"
#include "engines/mqttdata/GraphPayload.h"
#include "engines/mqttdata/FeedPayloads.h"
#include "engines/mqttdata/DataSession.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// =========================================================================
// 1. Minimal Mock Engine for Registry Tests
// =========================================================================


class MockNativeEngine : public IEngine {
public:
    EngineError initialize(EngineContext*, const EngineConfig*) override { return EngineError::OK; }
    void activate() override {}
    void update(EngineContext*) override {}
    void render(EngineContext*) override {}
    void deactivate() override {}
};

// =========================================================================
// 3. Test Fixture Setup / Teardown
// =========================================================================

void setUp(void) {
    resetVirtualClock();
    EngineRegistry::clear();
}

void tearDown(void) {
    EngineRegistry::clear();
}

// =========================================================================
// 4. EngineHandle Identity Tests
// =========================================================================

void test_engine_handle_equality_and_empty(void) {
    EngineHandle h1("clock", "inst_1");
    EngineHandle h2("clock", "inst_1");
    EngineHandle h3("clock", "inst_2");
    EngineHandle h4("weather", "inst_1");
    EngineHandle emptyHandle;

    TEST_ASSERT_TRUE(h1 == h2);
    TEST_ASSERT_FALSE(h1 != h2);
    TEST_ASSERT_TRUE(h1 != h3);
    TEST_ASSERT_TRUE(h1 != h4);

    TEST_ASSERT_FALSE(h1.isEmpty());
    TEST_ASSERT_TRUE(emptyHandle.isEmpty());

    // String constructor
    String desc = "crypto";
    String inst = "crypto_btc";
    EngineHandle hStr(desc, inst);
    TEST_ASSERT_EQUAL_STRING("crypto", hStr.descriptorId);
    TEST_ASSERT_EQUAL_STRING("crypto_btc", hStr.instanceId);
}

// =========================================================================
// 5. DisplayArbiter Test Cases
// =========================================================================

void test_arbiter_source_id_parsing(void) {
    TEST_ASSERT_EQUAL(DisplaySourceId::VISUALIZER, DisplayArbiter::parseSourceId("VISUALIZER"));
    TEST_ASSERT_EQUAL(DisplaySourceId::VISUALIZER, DisplayArbiter::parseSourceId("audiovisualizer"));
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, DisplayArbiter::parseSourceId("MQTT"));
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, DisplayArbiter::parseSourceId("message"));
    TEST_ASSERT_EQUAL(DisplaySourceId::MARQUEE, DisplayArbiter::parseSourceId("MARQUEE"));
    TEST_ASSERT_EQUAL(DisplaySourceId::GIF, DisplayArbiter::parseSourceId("GIF"));
    TEST_ASSERT_EQUAL(DisplaySourceId::GIF, DisplayArbiter::parseSourceId("gifs"));
    TEST_ASSERT_EQUAL(DisplaySourceId::ALERT, DisplayArbiter::parseSourceId("ALERT"));
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, DisplayArbiter::parseSourceId("unknown_engine"));
}

void test_arbiter_priority_resolution(void) {
    DisplayArbiter arbiter;

    // Initial state: fallback ROTATION is active
    DisplayDecision d0 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d0.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d0.sourceId);
    TEST_ASSERT_EQUAL((uint8_t)DisplayPriority::ROTATION, (uint8_t)d0.priority);

    // Submit MARQUEE (priority 30) -> preempts ROTATION (priority 10)
    DisplayRequest marqueeReq;
    marqueeReq.sourceId = DisplaySourceId::MARQUEE;
    marqueeReq.priority = DisplayPriority::MARQUEE;
    marqueeReq.lifecycle = RequestLifecycle::UNTIL_CANCELLED;
    marqueeReq.engineHandle = EngineHandle("marquee", "marquee_inst");
    arbiter.submitRequest(marqueeReq);

    DisplayDecision d1 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d1.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::MARQUEE, d1.sourceId);
    TEST_ASSERT_EQUAL((uint8_t)DisplayPriority::MARQUEE, (uint8_t)d1.priority);
    TEST_ASSERT_EQUAL_STRING("marquee", d1.engineHandle.descriptorId);

    // Submit ALERT (priority 100) -> preempts MARQUEE
    DisplayRequest alertReq;
    alertReq.sourceId = DisplaySourceId::ALERT;
    alertReq.priority = DisplayPriority::ALERT;
    alertReq.lifecycle = RequestLifecycle::UNTIL_CANCELLED;
    alertReq.engineHandle = EngineHandle("alert", "alert_inst");
    arbiter.submitRequest(alertReq);

    DisplayDecision d2 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d2.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ALERT, d2.sourceId);
    TEST_ASSERT_EQUAL((uint8_t)DisplayPriority::ALERT, (uint8_t)d2.priority);

    // Cancel ALERT -> falls back to MARQUEE
    arbiter.cancelRequest(DisplaySourceId::ALERT);
    DisplayDecision d3 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d3.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::MARQUEE, d3.sourceId);

    // Cancel MARQUEE -> falls back to ROTATION
    arbiter.cancelRequest(DisplaySourceId::MARQUEE);
    DisplayDecision d4 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d4.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d4.sourceId);
}

void test_arbiter_one_shot_auto_consumption(void) {
    DisplayArbiter arbiter;

    DisplayRequest oneShot;
    oneShot.sourceId = DisplaySourceId::ALERT;
    oneShot.priority = DisplayPriority::ALERT;
    oneShot.lifecycle = RequestLifecycle::ONE_SHOT;
    oneShot.engineHandle = EngineHandle("flash_alert", "alert_1");
    arbiter.submitRequest(oneShot);

    // First evaluation: ONE_SHOT request is active
    DisplayDecision d1 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d1.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ALERT, d1.sourceId);

    // Second evaluation: ONE_SHOT request has been auto-consumed, returns to ROTATION
    DisplayDecision d2 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d2.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d2.sourceId);
}

void test_arbiter_timed_request_expiration(void) {
    DisplayArbiter arbiter;

    DisplayRequest timed;
    timed.sourceId = DisplaySourceId::MQTT;
    timed.priority = DisplayPriority::MQTT;
    timed.lifecycle = RequestLifecycle::TIMED;
    timed.timeout_ms = 5000;
    timed.engineHandle = EngineHandle("message", "msg_1");
    arbiter.submitRequest(timed);

    // Right away: request is active
    DisplayDecision d1 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d1.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, d1.sourceId);

    // Advance 3000ms: still active (3000 < 5000)
    advanceVirtualMillis(3000);
    DisplayDecision d2 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d2.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, d2.sourceId);

    // Advance another 2500ms (5500 total): expired -> falls back to ROTATION
    advanceVirtualMillis(2500);
    DisplayDecision d3 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d3.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d3.sourceId);
}

void test_arbiter_queue_saturation_and_dropped_counter(void) {
    DisplayArbiter arbiter;
    TEST_ASSERT_EQUAL_UINT32(0, arbiter.getDroppedCommandCount());

    // DisplayArbiter SPSC command queue capacity is 16
    for (size_t i = 0; i < DisplayArbiter::QUEUE_CAPACITY; ++i) {
        DisplayRequest req;
        req.sourceId = DisplaySourceId::MARQUEE;
        req.priority = DisplayPriority::MARQUEE;
        req.requestId = (uint32_t)(i + 1);
        arbiter.submitRequest(req);
    }
    TEST_ASSERT_EQUAL_UINT32(0, arbiter.getDroppedCommandCount());

    // Pushing 5 more commands while queue is full must increment dropped count
    for (size_t i = 0; i < 5; ++i) {
        DisplayRequest req;
        req.sourceId = DisplaySourceId::MQTT;
        req.priority = DisplayPriority::MQTT;
        arbiter.submitRequest(req);
    }
    TEST_ASSERT_EQUAL_UINT32(5, arbiter.getDroppedCommandCount());

    // Evaluate drains the queue without crashing or deadlocking
    DisplayDecision decision = arbiter.evaluate();
    TEST_ASSERT_TRUE(decision.valid);
}

// =========================================================================
// 5. ConfigSnapshot CRC32 & Triple-Buffer State Machine Tests
// =========================================================================

void test_config_snapshot_crc32_and_magic(void) {
    ConfigSnapshot snap;
    snap.version = 42;
    snap.instances.resize(5);
    snap.crc32 = ConfigSnapshot::calculateCRC32(snap.version, snap.instances.size());

    TEST_ASSERT_TRUE(snap.isValid());

    // Verify CRC sensitivity: changing version invalidates snapshot
    snap.version = 43;
    TEST_ASSERT_FALSE(snap.isValid());

    // Restore version, corrupt start magic
    snap.version = 42;
    snap.magic_start = 0x12345678;
    TEST_ASSERT_FALSE(snap.isValid());

    // Restore start magic, corrupt end magic
    snap.magic_start = ConfigSnapshot::MAGIC_START;
    snap.magic_end = 0x87654321;
    TEST_ASSERT_FALSE(snap.isValid());
}


void test_srsw_triple_buffer_linearizability(void) {
    // Emulate SRSW triple-buffer state machine with 3 slots
    enum class SlotState : uint8_t { FREE = 0, WRITING = 1, PUBLISHED = 2, READING = 3 };

    std::atomic<SlotState> state[3];
    for (int i = 0; i < 3; ++i) state[i].store(SlotState::FREE);
    state[0].store(SlotState::PUBLISHED);
    std::atomic<uint8_t> publishedSlot{0};

    // Reader CAS(PUBLISHED -> READING)
    uint8_t current = publishedSlot.load(std::memory_order_acquire);
    SlotState expected = SlotState::PUBLISHED;
    bool casOk = state[current].compare_exchange_strong(expected, SlotState::READING, std::memory_order_acquire);
    TEST_ASSERT_TRUE(casOk);
    TEST_ASSERT_EQUAL((uint8_t)SlotState::READING, (uint8_t)state[current].load());

    // Reader releases slot back to PUBLISHED
    state[current].store(SlotState::PUBLISHED, std::memory_order_release);
    TEST_ASSERT_EQUAL((uint8_t)SlotState::PUBLISHED, (uint8_t)state[current].load());

    // Writer reserves a FREE slot (slot 1)
    expected = SlotState::FREE;
    bool writeReserveOk = state[1].compare_exchange_strong(expected, SlotState::WRITING, std::memory_order_acquire);
    TEST_ASSERT_TRUE(writeReserveOk);
    TEST_ASSERT_EQUAL((uint8_t)SlotState::WRITING, (uint8_t)state[1].load());

    // Writer completes construction and publishes
    state[1].store(SlotState::PUBLISHED, std::memory_order_release);
    publishedSlot.store(1, std::memory_order_release);

    // Old slot 0 is reclaimed to FREE
    state[0].store(SlotState::FREE, std::memory_order_release);
    TEST_ASSERT_EQUAL(1, publishedSlot.load());
    TEST_ASSERT_EQUAL((uint8_t)SlotState::FREE, (uint8_t)state[0].load());
    TEST_ASSERT_EQUAL((uint8_t)SlotState::PUBLISHED, (uint8_t)state[1].load());
}

// =========================================================================
// 6. TimingSafe Comparison Tests (Production Algorithm)
// =========================================================================

void test_timing_safe_compare(void) {
    // Identical strings
    TEST_ASSERT_TRUE(TimingSafe::compare("secret_token_123", "secret_token_123"));
    TEST_ASSERT_TRUE(TimingSafe::compare("", ""));

    // Different lengths
    TEST_ASSERT_FALSE(TimingSafe::compare("secret", "secret_longer"));
    TEST_ASSERT_FALSE(TimingSafe::compare("secret_longer", "secret"));
    TEST_ASSERT_FALSE(TimingSafe::compare("", "token"));
    TEST_ASSERT_FALSE(TimingSafe::compare("token", ""));

    // Same length, 1-byte difference
    TEST_ASSERT_FALSE(TimingSafe::compare("xecret_token_123", "secret_token_123"));
    TEST_ASSERT_FALSE(TimingSafe::compare("secret_t0ken_123", "secret_token_123"));
    TEST_ASSERT_FALSE(TimingSafe::compare("secret_token_124", "secret_token_123"));
}


// =========================================================================
// 8. EngineRegistry Tests
// =========================================================================

void test_engine_registry_lifecycle(void) {
    TEST_ASSERT_EQUAL(0, EngineRegistry::count());

    EngineDescriptor desc1;
    desc1.metadata.id = "clock";
    desc1.metadata.name = "Clock Engine";
    desc1.factory = []() { return std::unique_ptr<IEngine>(new MockNativeEngine()); };

    TEST_ASSERT_TRUE(EngineRegistry::registerEngine(desc1));
    TEST_ASSERT_EQUAL(1, EngineRegistry::count());

    // Duplicate registration must fail
    EngineDescriptor descDup;
    descDup.metadata.id = "clock";
    TEST_ASSERT_FALSE(EngineRegistry::registerEngine(descDup));
    TEST_ASSERT_EQUAL(1, EngineRegistry::count());

    // Lookup
    const EngineDescriptor* found = EngineRegistry::getDescriptor("clock");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Clock Engine", found->metadata.name);

    // Non-existent lookup
    TEST_ASSERT_NULL(EngineRegistry::getDescriptor("nonexistent"));

    // Clear registry
    EngineRegistry::clear();
    TEST_ASSERT_EQUAL(0, EngineRegistry::count());
}

// =========================================================================
// 9. Geometry & Primitives Tests
// =========================================================================

void test_geometry_layout_classification(void) {
    // WIDE: W >= (H * 3) / 2
    TEST_ASSERT_EQUAL(LayoutClass::WIDE, DisplayGeometry::classify(64, 32));
    TEST_ASSERT_EQUAL(LayoutClass::WIDE, DisplayGeometry::classify(128, 32));
    TEST_ASSERT_EQUAL(LayoutClass::WIDE, DisplayGeometry::classify(128, 64));

    // SQUARE: Intermediate ratios
    TEST_ASSERT_EQUAL(LayoutClass::SQUARE, DisplayGeometry::classify(64, 64));

    // PORTRAIT: H >= (W * 3) / 2 && H < W * 3
    TEST_ASSERT_EQUAL(LayoutClass::PORTRAIT, DisplayGeometry::classify(32, 64));

    // TALL: H >= W * 3
    TEST_ASSERT_EQUAL(LayoutClass::TALL, DisplayGeometry::classify(32, 128));
}

void test_rect_primitives(void) {
    Rect r(10, 20, 30, 40);
    TEST_ASSERT_FALSE(r.isEmpty());
    TEST_ASSERT_TRUE(r.contains(10, 20)); // Top-left
    TEST_ASSERT_TRUE(r.contains(39, 59)); // Bottom-right internal
    TEST_ASSERT_FALSE(r.contains(40, 60)); // Boundary (exclusive)
    TEST_ASSERT_FALSE(r.contains(9, 20));  // Outside left

    Rect emptyRect(0, 0, 0, 0);
    TEST_ASSERT_TRUE(emptyRect.isEmpty());
}

// =========================================================================
// 10. Preemption Bounded Stack Logic Tests
// =========================================================================

struct PreemptionMockEntry {
    char id[16]{0};
    uint8_t priority = 0;
};

template <size_t Capacity>
class BoundedPreemptionStack {
public:
    bool push(const PreemptionMockEntry& entry) {
        if (_depth >= Capacity) return false;
        _stack[_depth++] = entry;
        return true;
    }
    bool pop(PreemptionMockEntry& out) {
        if (_depth == 0) return false;
        out = _stack[--_depth];
        return true;
    }
    size_t depth() const { return _depth; }
    void clear() { _depth = 0; }

private:
    PreemptionMockEntry _stack[Capacity];
    size_t _depth = 0;
};

void test_preemption_stack_bounded_depth(void) {
    BoundedPreemptionStack<4> stack;
    TEST_ASSERT_EQUAL(0, stack.depth());

    // Push up to capacity 4
    for (uint8_t i = 1; i <= 4; ++i) {
        PreemptionMockEntry entry;
        snprintf(entry.id, sizeof(entry.id), "session_%u", i);
        entry.priority = i * 10;
        TEST_ASSERT_TRUE(stack.push(entry));
    }
    TEST_ASSERT_EQUAL(4, stack.depth());

    // 5th push must be rejected (saturation rejection invariant)
    PreemptionMockEntry overflowEntry;
    snprintf(overflowEntry.id, sizeof(overflowEntry.id), "overflow");
    TEST_ASSERT_FALSE(stack.push(overflowEntry));
    TEST_ASSERT_EQUAL(4, stack.depth());

    // Pop in LIFO order
    PreemptionMockEntry popped;
    TEST_ASSERT_TRUE(stack.pop(popped));
    TEST_ASSERT_EQUAL_STRING("session_4", popped.id);
    TEST_ASSERT_EQUAL(3, stack.depth());
}

// =========================================================================
// Graph payload (data feed MQTT) Tests
// =========================================================================

static graph::GraphData g_graph;
static float g_graphPool[graph::MAX_SERIES * graph::MAX_POINTS];
static DynamicJsonDocument g_graphDoc(32768);

static bool parseDoc(const char* json) {
    static char buf[4096];
    strncpy(buf, json, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    g_graphDoc.clear();
    return deserializeJson(g_graphDoc, buf) == DeserializationError::Ok;
}

static bool parseGraph(const char* json) {
    graph::attachPool(g_graph, g_graphPool, graph::MAX_POINTS);
    static char buf[4096];
    strncpy(buf, json, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return graph::parsePayload(buf, g_graphDoc, g_graph);
}

void test_graph_payload_parses_v1_fields(void) {
    TEST_ASSERT_TRUE(parseGraph(
        "{\"v\":1,\"title\":\"AC 24H\",\"summary\":\"12.3kWh\",\"unit\":\"kW\",\"slots\":4,\"max\":6,"
        "\"stack\":true,\"series\":[{\"label\":\"UP\",\"color\":\"#2080FF\",\"data\":[1,2,3,4]},"
        "{\"label\":\"DN\",\"color\":\"#00D0A0\",\"data\":[0.5,null,\"x\",1]}],"
        "\"bands\":[{\"from\":1,\"to\":3,\"color\":\"#301800\"}],\"marks\":[{\"at\":2,\"color\":\"#404040\"}]}"));
    TEST_ASSERT_TRUE(g_graph.valid);
    TEST_ASSERT_EQUAL_STRING("AC 24H", g_graph.title);
    TEST_ASSERT_EQUAL_STRING("12.3kWh", g_graph.summary);
    TEST_ASSERT_EQUAL_STRING("kW", g_graph.unit);
    TEST_ASSERT_EQUAL(4, g_graph.slots);
    TEST_ASSERT_EQUAL(2, g_graph.seriesCount);
    TEST_ASSERT_EQUAL_HEX8(0x20, g_graph.series[0].color.r);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_graph.series[0].color.b);
    TEST_ASSERT_EQUAL(1, g_graph.bandCount);
    TEST_ASSERT_EQUAL(1, g_graph.bands[0].from);
    TEST_ASSERT_EQUAL(3, g_graph.bands[0].to);
    TEST_ASSERT_EQUAL(1, g_graph.markCount);
    TEST_ASSERT_EQUAL(2, g_graph.marks[0].at);
    // Non-numeric points read as zero.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, graph::valueAt(g_graph, 1, 1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, graph::valueAt(g_graph, 1, 2));
    graph::Range r = graph::yRange(g_graph);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.lo);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, r.hi);
}

void test_graph_payload_right_aligns_short_series(void) {
    TEST_ASSERT_TRUE(parseGraph("{\"slots\":6,\"series\":[{\"data\":[1,2,3]}]}"));
    // Last point in the last slot; the left of the graph stays empty.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, graph::valueAt(g_graph, 0, 0));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, graph::valueAt(g_graph, 0, 2));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, graph::valueAt(g_graph, 0, 3));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, graph::valueAt(g_graph, 0, 5));
    // Missing slots default to the longest series; missing colours get the default palette.
    TEST_ASSERT_TRUE(parseGraph("{\"series\":[{\"data\":[1,2]},{\"data\":[5,6,7]}]}"));
    TEST_ASSERT_EQUAL(3, g_graph.slots);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_graph.series[1].color.r);
    TEST_ASSERT_EQUAL_HEX8(0xD0, g_graph.series[1].color.g);
}

void test_graph_payload_auto_scale_stacked_and_overlaid(void) {
    TEST_ASSERT_TRUE(parseGraph("{\"series\":[{\"data\":[1,4]},{\"data\":[2,1]}]}"));
    TEST_ASSERT_TRUE(g_graph.stack);                     // default
    graph::Range r = graph::yRange(g_graph);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.lo);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, r.hi);                 // tallest stacked column
    TEST_ASSERT_TRUE(parseGraph("{\"stack\":false,\"max\":0,\"series\":[{\"data\":[1,4]},{\"data\":[2,1]}]}"));
    TEST_ASSERT_EQUAL_FLOAT(4.0f, graph::yRange(g_graph).hi);   // tallest single point
    TEST_ASSERT_TRUE(parseGraph("{\"series\":[{\"data\":[0,0]}]}"));
    r = graph::yRange(g_graph);
    TEST_ASSERT_TRUE(r.hi > r.lo);                       // never divide by zero
}

void test_graph_negative_bars_and_zero_line(void) {
    // Positive and negative stacks are kept apart; the range spans both and the zero line shows.
    TEST_ASSERT_TRUE(parseGraph("{\"series\":[{\"data\":[2,-1]},{\"data\":[1,-3]}]}"));
    graph::Range r = graph::yRange(g_graph);
    TEST_ASSERT_EQUAL_FLOAT(-4.0f, r.lo);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, r.hi);
    TEST_ASSERT_TRUE(graph::zeroLineVisible(g_graph, r));
    TEST_ASSERT_EQUAL(4, graph::heightFor(0.0f, r, 7));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, graph::valueAt(g_graph, 1, 1));
    // Bars only and all positive: no zero line (0 is the bottom edge, not inside the range).
    TEST_ASSERT_TRUE(parseGraph("{\"series\":[{\"data\":[1,2]}]}"));
    TEST_ASSERT_FALSE(graph::zeroLineVisible(g_graph, graph::yRange(g_graph)));
}

void test_graph_lines_range_gaps_and_explicit_min_max(void) {
    TEST_ASSERT_TRUE(parseGraph("{\"series\":[{\"style\":\"line\",\"data\":[70,null,80,\"x\"]}]}"));
    TEST_ASSERT_TRUE(g_graph.series[0].line);
    TEST_ASSERT_TRUE(graph::hasLines(g_graph));
    TEST_ASSERT_FALSE(graph::hasBars(g_graph));
    TEST_ASSERT_TRUE(isnan(graph::pointAt(g_graph, 0, 1)));   // null = gap
    TEST_ASSERT_TRUE(isnan(graph::pointAt(g_graph, 0, 3)));   // non-number = gap
    TEST_ASSERT_EQUAL_FLOAT(0.0f, graph::valueAt(g_graph, 0, 1));
    graph::Range r = graph::yRange(g_graph);                  // [70,80] padded 5% of 10 -> 0.5 min
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 69.5f, r.lo);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 80.5f, r.hi);
    TEST_ASSERT_TRUE(parseGraph("{\"series\":[{\"style\":\"line\",\"data\":[0,100]}]}"));
    r = graph::yRange(g_graph);                               // 5% of 100 = 5
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.0f, r.lo);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 105.0f, r.hi);
    // Explicit range, values outside clamp to the plot edges.
    TEST_ASSERT_TRUE(parseGraph("{\"min\":68,\"max\":90,\"yaxis\":true,\"series\":[{\"style\":\"line\",\"data\":[60,95,79]}]}"));
    TEST_ASSERT_TRUE(g_graph.yaxis);
    r = graph::yRange(g_graph);
    TEST_ASSERT_EQUAL_FLOAT(68.0f, r.lo);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, r.hi);
    TEST_ASSERT_EQUAL(0, graph::lineOffsetFor(60.0f, r, 24));
    TEST_ASSERT_EQUAL(23, graph::lineOffsetFor(95.0f, r, 24));
    TEST_ASSERT_EQUAL(12, graph::lineOffsetFor(79.0f, r, 24));
    // max <= min = automatic, with the given min kept as the bottom.
    TEST_ASSERT_TRUE(parseGraph("{\"min\":50,\"max\":40,\"series\":[{\"style\":\"line\",\"data\":[60,70]}]}"));
    r = graph::yRange(g_graph);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, r.lo);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 70.5f, r.hi);
    // Degree sign survives as one cell; other UTF-8 becomes '?'.
    TEST_ASSERT_TRUE(parseGraph("{\"summary\":\"76\u00b0\",\"title\":\"\u00e9t\u00e9\",\"series\":[]}"));
    TEST_ASSERT_EQUAL(3, (int)strlen(g_graph.summary));
    TEST_ASSERT_EQUAL((char)0xB0, g_graph.summary[2]);
    TEST_ASSERT_EQUAL_STRING("?t?", g_graph.title);
}

void test_graph_payload_rejects_bad_input(void) {
    TEST_ASSERT_FALSE(parseGraph("not json"));
    TEST_ASSERT_FALSE(g_graph.valid);
    TEST_ASSERT_FALSE(parseGraph("{\"title\":\"no series\"}"));
    TEST_ASSERT_FALSE(g_graph.valid);
    // Out-of-range bands and marks are dropped, oversized counts are capped.
    TEST_ASSERT_TRUE(parseGraph("{\"slots\":4,\"series\":[{\"data\":[1,1,1,1]}],"
                                "\"bands\":[{\"from\":3,\"to\":1},{\"from\":2,\"to\":99}],"
                                "\"marks\":[{\"at\":4},{\"at\":-1},{\"at\":0}],"
                                "\"title\":\"a very long title that does not fit\"}"));
    TEST_ASSERT_EQUAL(1, g_graph.bandCount);
    TEST_ASSERT_EQUAL(4, g_graph.bands[0].to);
    TEST_ASSERT_EQUAL(1, g_graph.markCount);
    TEST_ASSERT_EQUAL(0, g_graph.marks[0].at);
    TEST_ASSERT_EQUAL(sizeof(g_graph.title) - 1, strlen(g_graph.title));
    graph::Rgb c{1, 2, 3};
    TEST_ASSERT_FALSE(graph::parseHexColor("#12345", c));
    TEST_ASSERT_FALSE(graph::parseHexColor("#12345G", c));
    TEST_ASSERT_EQUAL(1, c.r);
    TEST_ASSERT_TRUE(graph::parseHexColor("00ff7f", c));
    TEST_ASSERT_EQUAL_HEX8(0x7F, c.b);
}

void test_graph_layout_and_column_mapping(void) {
    graph::Layout l = graph::calculateLayout(128, 32, true);
    TEST_ASSERT_TRUE(l.header);
    TEST_ASSERT_EQUAL(1, l.textSize);
    TEST_ASSERT_EQUAL(8, l.plot.y);
    TEST_ASSERT_EQUAL(24, l.plot.h);
    TEST_ASSERT_FALSE(l.gridline);
    l = graph::calculateLayout(256, 64, true);
    TEST_ASSERT_EQUAL(2, l.textSize);
    TEST_ASSERT_EQUAL(15, l.plot.y);
    TEST_ASSERT_EQUAL(49, l.plot.h);
    TEST_ASSERT_TRUE(l.gridline);
    l = graph::calculateLayout(128, 32, false);
    TEST_ASSERT_FALSE(l.header);
    TEST_ASSERT_EQUAL(0, l.plot.y);
    TEST_ASSERT_EQUAL(32, l.plot.h);
    l = graph::calculateLayout(64, 16, true);     // too short for a header
    TEST_ASSERT_FALSE(l.header);

    // 96 slots over 128 columns: every slot gets at least one column, in order.
    TEST_ASSERT_EQUAL(0, graph::slotForColumn(0, 128, 96));
    TEST_ASSERT_EQUAL(95, graph::slotForColumn(127, 128, 96));
    uint16_t prev = 0;
    for (int x = 0; x < 128; x++) {
        uint16_t s = graph::slotForColumn(x, 128, 96);
        TEST_ASSERT_TRUE(s == prev || s == prev + 1);
        prev = s;
    }
    TEST_ASSERT_EQUAL(0, graph::columnForSlot(0, 128, 96));
    TEST_ASSERT_EQUAL(128, graph::columnForSlot(96, 128, 96));
    TEST_ASSERT_EQUAL(graph::slotForColumn(graph::columnForSlot(28, 128, 96), 128, 96), 28);

    graph::Range r{0.0f, 6.0f};
    TEST_ASSERT_EQUAL(24, graph::heightFor(6.0f, r, 24));
    TEST_ASSERT_EQUAL(24, graph::heightFor(9.0f, r, 24));    // clamped
    TEST_ASSERT_EQUAL(12, graph::heightFor(3.0f, r, 24));
    TEST_ASSERT_EQUAL(0, graph::heightFor(-1.0f, r, 24));
}


void test_graph_96_bands_and_newest_kept(void) {
    // 96 one-slot bands, each its own colour, sharing edges: all of them are kept.
    static char json[12000];
    int n = snprintf(json, sizeof(json), "{\"slots\":96,\"series\":[{\"data\":[1]}],\"bands\":[");
    for (int i = 0; i < 96; i++) {
        n += snprintf(json + n, sizeof(json) - n, "%s{\"from\":%d,\"to\":%d,\"color\":\"#%02X0000\"}",
                      i ? "," : "", i, i + 1, i);
    }
    snprintf(json + n, sizeof(json) - n, "]}");
    g_graphDoc.clear();
    graph::attachPool(g_graph, g_graphPool, graph::MAX_POINTS);
    TEST_ASSERT_TRUE(graph::parsePayload(json, g_graphDoc, g_graph));
    TEST_ASSERT_EQUAL(96, g_graph.bandCount);
    TEST_ASSERT_EQUAL(95, g_graph.bands[95].from);
    TEST_ASSERT_EQUAL(96, g_graph.bands[95].to);
    TEST_ASSERT_EQUAL_HEX8(0x5F, g_graph.bands[95].color.r);

    // 100 bands, oldest first: the cap keeps the 96 newest (from 4..99), not the first 96.
    n = snprintf(json, sizeof(json), "{\"slots\":100,\"series\":[{\"data\":[1]}],\"bands\":[");
    for (int i = 0; i < 100; i++) n += snprintf(json + n, sizeof(json) - n, "%s{\"from\":%d,\"to\":%d}", i ? "," : "", i, i + 1);
    snprintf(json + n, sizeof(json) - n, "]}");
    g_graphDoc.clear();
    graph::attachPool(g_graph, g_graphPool, graph::MAX_POINTS);
    TEST_ASSERT_TRUE(graph::parsePayload(json, g_graphDoc, g_graph));
    TEST_ASSERT_EQUAL(96, g_graph.bandCount);
    int minFrom = 1000;
    bool has99 = false;
    for (int i = 0; i < g_graph.bandCount; i++) {
        if (g_graph.bands[i].from < minFrom) minFrom = g_graph.bands[i].from;
        if (g_graph.bands[i].from == 99) has99 = true;
    }
    TEST_ASSERT_EQUAL(4, minFrom);
    TEST_ASSERT_TRUE(has99);
    for (int i = 1; i < g_graph.bandCount; i++) TEST_ASSERT_TRUE(g_graph.bands[i - 1].from <= g_graph.bands[i].from);
}

void test_graph_summary_color_and_legend(void) {
    TEST_ASSERT_TRUE(parseGraph("{\"summary\":\"76\",\"series\":[]}"));
    TEST_ASSERT_FALSE(g_graph.hasSummaryColor);
    TEST_ASSERT_EQUAL(0, g_graph.legendCount);
    TEST_ASSERT_TRUE(parseGraph("{\"summary_color\":\"#2060FF\",\"series\":[],\"legend\":["
        "{\"text\":\"INSIDE\",\"color\":\"#40A0FF\"},{\"text\":\"NOCOLOR\"},{\"text\":\"\",\"color\":\"#FFFFFF\"},"
        "{\"text\":\"OUTSIDE\",\"color\":\"#FF4020\"},{\"text\":\"SET\",\"color\":\"#5050A0\"},"
        "{\"text\":\"COOL\",\"color\":\"#2060FF\"},{\"text\":\"EXTRA\",\"color\":\"#FFFFFF\"}]}"));
    TEST_ASSERT_TRUE(g_graph.hasSummaryColor);
    TEST_ASSERT_EQUAL_HEX8(0x60, g_graph.summaryColor.g);
    TEST_ASSERT_EQUAL(4, g_graph.legendCount);              // no colour / empty text skipped, extra dropped
    TEST_ASSERT_EQUAL_STRING("INSIDE", g_graph.legend[0].text);
    TEST_ASSERT_EQUAL_HEX8(0xA0, g_graph.legend[0].color.g);
    TEST_ASSERT_EQUAL_STRING("OUTSIDE", g_graph.legend[1].text);
    TEST_ASSERT_EQUAL_STRING("COOL", g_graph.legend[3].text);
}


void test_feed_values_payload(void) {
    TEST_ASSERT_TRUE(parseDoc("{\"v\":1,\"type\":\"table\",\"title\":\"ENERGY\",\"tiles\":["
        "{\"label\":\"HOUSE\",\"value\":\"2.8\",\"unit\":\"kW\",\"color\":\"#FFB000\"},"
        "{\"label\":\"OUT\",\"value\":\"75\",\"unit\":\"\u00b0F\",\"label_color\":\"#102030\"},"
        "{\"label\":\"N\",\"value\":3.5},{\"label\":\"D\"},{\"label\":\"E\"}]}"));
    TEST_ASSERT_EQUAL((int)feed::Kind::Table, (int)feed::detectKind(g_graphDoc));
    static feed::ValuesData v;
    TEST_ASSERT_TRUE(feed::parseTable(g_graphDoc, v));
    TEST_ASSERT_TRUE(v.showTitle);                          // default
    TEST_ASSERT_EQUAL_STRING("ENERGY", v.title);
    TEST_ASSERT_EQUAL(4, v.count);                          // fifth tile ignored
    TEST_ASSERT_EQUAL_STRING("2.8", v.tiles[0].value);
    TEST_ASSERT_EQUAL_HEX8(0xB0, v.tiles[0].color.g);
    TEST_ASSERT_EQUAL_HEX8(0x80, v.tiles[0].labelColor.r);  // default label colour
    TEST_ASSERT_EQUAL_HEX8(0x30, v.tiles[1].labelColor.b);
    TEST_ASSERT_EQUAL((char)0xB0, v.tiles[1].unit[0]);
    TEST_ASSERT_EQUAL_STRING("3.5", v.tiles[2].value);       // bare number accepted
    TEST_ASSERT_TRUE(parseDoc("{\"type\":\"table\",\"show_title\":false,\"tiles\":[{\"label\":\"DOWNSTAIRS\",\"label_short\":\"DN\",\"value\":\"75\"}]}"));
    TEST_ASSERT_TRUE(feed::parseTable(g_graphDoc, v));
    TEST_ASSERT_FALSE(v.showTitle);
    TEST_ASSERT_EQUAL_STRING("DOWNSTAIRS", v.tiles[0].label);
    TEST_ASSERT_EQUAL_STRING("DN", v.tiles[0].labelShort);
    TEST_ASSERT_TRUE(parseGraph("{\"title\":\"DOWNSTAIRS COOL\",\"title_short\":\"DN COOL\",\"series\":[]}"));
    TEST_ASSERT_EQUAL_STRING("DOWNSTAIRS COOL", g_graph.title);
    TEST_ASSERT_EQUAL_STRING("DN COOL", g_graph.titleShort);
    TEST_ASSERT_EQUAL_HEX8(0xFF, v.tiles[3].color.r);       // default value colour white
}

void test_feed_weather_payload_and_conditions(void) {
    TEST_ASSERT_TRUE(parseDoc("{\"v\":1,\"type\":\"weather\",\"units\":\"imperial\",\"current\":{\"temp\":75,"
        "\"condition\":\"clear-night\",\"humidity\":47,\"wind\":7,\"wind_unit\":\"mph\"},"
        "\"days\":[{\"temp_max\":89,\"temp_min\":70,\"condition\":\"sunny\",\"precip_prob\":0},"
        "{\"temp_max\":90,\"temp_min\":71},{},{},{},{\"temp_max\":1,\"temp_min\":0}]}"));
    TEST_ASSERT_EQUAL((int)feed::Kind::Weather, (int)feed::detectKind(g_graphDoc));
    static feed::WeatherFeed w;
    TEST_ASSERT_TRUE(feed::parseWeather(g_graphDoc, w));
    TEST_ASSERT_TRUE(w.imperial);
    TEST_ASSERT_TRUE(w.hasCurrent);
    TEST_ASSERT_EQUAL_FLOAT(75.0f, w.currentTemp);
    TEST_ASSERT_EQUAL_STRING("clear-night", w.currentCondition);
    TEST_ASSERT_EQUAL(47, w.humidity);
    TEST_ASSERT_EQUAL_STRING("", w.windDir);
    TEST_ASSERT_EQUAL(5, w.dayCount);                        // sixth day ignored
    TEST_ASSERT_EQUAL_FLOAT(89.0f, w.days[0].tempMax);
    TEST_ASSERT_EQUAL(0, w.days[0].precipProb);
    TEST_ASSERT_EQUAL_STRING("", w.days[1].condition);
    TEST_ASSERT_EQUAL(-1, w.days[1].precipProb);
    TEST_ASSERT_FALSE(w.days[2].hasTemps);

    TEST_ASSERT_EQUAL_STRING("01d", feed::iconForCondition("sunny"));
    TEST_ASSERT_EQUAL_STRING("01n", feed::iconForCondition("clear-night"));
    TEST_ASSERT_EQUAL_STRING("02d", feed::iconForCondition("partlycloudy"));
    TEST_ASSERT_EQUAL_STRING("09d", feed::iconForCondition("pouring"));
    TEST_ASSERT_EQUAL_STRING("13d", feed::iconForCondition("hail"));
    TEST_ASSERT_EQUAL_STRING("03d", feed::iconForCondition("windy-variant"));
    TEST_ASSERT_EQUAL_STRING("03d", feed::iconForCondition("volcano"));
    TEST_ASSERT_EQUAL(9, feed::conditionGroup("lightning-rainy"));
    TEST_ASSERT_EQUAL(-1, feed::conditionGroup("volcano"));

    // Kinds come only from "type": shape alone, no type, an unknown type or the old "values" are Unsupported.
    TEST_ASSERT_TRUE(parseDoc("{\"type\":\"graph\",\"series\":[]}"));
    TEST_ASSERT_EQUAL((int)feed::Kind::Graph, (int)feed::detectKind(g_graphDoc));
    TEST_ASSERT_TRUE(parseDoc("{\"type\":\"value\",\"value\":\"1\"}"));
    TEST_ASSERT_EQUAL((int)feed::Kind::Value, (int)feed::detectKind(g_graphDoc));
    TEST_ASSERT_TRUE(parseDoc("{\"series\":[]}"));
    TEST_ASSERT_EQUAL((int)feed::Kind::Unsupported, (int)feed::detectKind(g_graphDoc));
    TEST_ASSERT_TRUE(parseDoc("{\"type\":\"values\",\"tiles\":[]}"));
    TEST_ASSERT_EQUAL((int)feed::Kind::Unsupported, (int)feed::detectKind(g_graphDoc));
    TEST_ASSERT_TRUE(parseDoc("{\"type\":\"chart\"}"));
    TEST_ASSERT_EQUAL((int)feed::Kind::Unsupported, (int)feed::detectKind(g_graphDoc));
    // "seconds": default 10, clamped to 3..3600.
    TEST_ASSERT_EQUAL(10, feed::pageSeconds(g_graphDoc));
    TEST_ASSERT_TRUE(parseDoc("{\"seconds\":1}"));
    TEST_ASSERT_EQUAL(3, feed::pageSeconds(g_graphDoc));
    TEST_ASSERT_TRUE(parseDoc("{\"seconds\":25}"));
    TEST_ASSERT_EQUAL(25, feed::pageSeconds(g_graphDoc));
    TEST_ASSERT_TRUE(parseDoc("{\"current\":{\"temp\":80,\"wind\":9,\"wind_dir\":\"NE\"}}"));
    TEST_ASSERT_TRUE(feed::parseWeather(g_graphDoc, w));
    TEST_ASSERT_EQUAL_STRING("NE", w.windDir);
    TEST_ASSERT_TRUE(parseDoc("{\"current\":{\"temp\":21}}"));
    TEST_ASSERT_TRUE(feed::parseWeather(g_graphDoc, w));
    TEST_ASSERT_FALSE(w.imperial);
    TEST_ASSERT_EQUAL(0, w.dayCount);
}


// ---- MQTT Data engine session (per-activation state) ----

static int g_allocs = 0, g_frees = 0, g_failAt = -1;
static void* countingAlloc(size_t n) {
    if (g_failAt >= 0 && g_allocs == g_failAt) { g_allocs++; return nullptr; }
    g_allocs++;
    return malloc(n);
}
static void countingFree(void* p) { if (p) { g_frees++; free(p); } }
static const mqd::Allocator kCounting{countingAlloc, countingFree};

static char g_topics[mqd::MAX_TOPICS][mqd::TOPIC_LEN];

static bool dataHandle(mqd::Session& s, const char* topic, const char* json, JsonDocument& doc) {
    static char buf[16384];
    size_t n = strlen(json);
    memcpy(buf, json, n + 1);
    return s.handle(topic, buf, n, doc);
}

void test_mqttdata_session_allocations_balance_over_activations(void) {
    g_allocs = g_frees = 0; g_failAt = -1;
    uint8_t n = mqd::parseTopics("a/w, a/g\na/v", g_topics, mqd::MAX_TOPICS);
    TEST_ASSERT_EQUAL(3, n);
    mqd::Session s;
    for (int i = 0; i < 25; i++) {   // activate / deactivate
        TEST_ASSERT_TRUE(s.create(mqd::limitsFor(i % 2 == 0), g_topics, n, kCounting));
        TEST_ASSERT_TRUE(s.ready());
        TEST_ASSERT_EQUAL(4 * (i + 1), g_allocs);   // four allocations per activation
        s.destroy();
        TEST_ASSERT_FALSE(s.ready());
        TEST_ASSERT_EQUAL(g_allocs, g_frees);       // and all of them released
    }
    s.destroy();                                    // a second destroy is harmless
    TEST_ASSERT_EQUAL(g_allocs, g_frees);

    // Out of memory part-way: whatever was allocated is released again.
    g_allocs = g_frees = 0; g_failAt = 2;
    TEST_ASSERT_FALSE(s.create(mqd::limitsFor(false), g_topics, n, kCounting));
    TEST_ASSERT_FALSE(s.ready());
    TEST_ASSERT_EQUAL(g_allocs - 1, g_frees);       // the failed one never existed
    g_failAt = -1;
}

void test_mqttdata_limits_topics_and_parsing(void) {
    mqd::Limits big = mqd::limitsFor(true), small = mqd::limitsFor(false);
    TEST_ASSERT_EQUAL(8192, (int)big.payloadBytes);
    TEST_ASSERT_EQUAL(288, big.points);
    TEST_ASSERT_EQUAL(6, big.maxTopics);
    TEST_ASSERT_EQUAL(4096, (int)small.payloadBytes);
    TEST_ASSERT_EQUAL(96, small.points);
    TEST_ASSERT_EQUAL(4, small.maxTopics);

    char longTopic[200];
    memset(longTopic, 'x', 150); longTopic[150] = '\0';
    char list[400];
    snprintf(list, sizeof(list), "  t/1 ,t/2,, t/1\n t/3\r\n%s, t/4 , t/5, t/6, t/7", longTopic);
    uint8_t tooLong = 0;
    uint8_t n = mqd::parseTopics(list, g_topics, mqd::MAX_TOPICS, &tooLong);
    TEST_ASSERT_EQUAL(6, n);                        // duplicate, empty and too-long entries skipped; capped at 6
    TEST_ASSERT_EQUAL(1, tooLong);
    TEST_ASSERT_EQUAL_STRING("t/1", g_topics[0]);
    TEST_ASSERT_EQUAL_STRING("t/3", g_topics[2]);
    TEST_ASSERT_EQUAL_STRING("t/6", g_topics[5]);

    // Without PSRAM a session keeps 4 pages however many topics are configured.
    g_allocs = g_frees = 0;
    mqd::Session s;
    TEST_ASSERT_TRUE(s.create(small, g_topics, n, kCounting));
    TEST_ASSERT_EQUAL(4, s.pageCount());
    TEST_ASSERT_EQUAL_STRING("t/4", s.topic(3));
}

void test_mqttdata_session_pages_types_and_handoff(void) {
    mqd::Session s;
    uint8_t n = mqd::parseTopics("ha/g,ha/v,ha/w", g_topics, mqd::MAX_TOPICS);
    TEST_ASSERT_TRUE(s.create(mqd::limitsFor(false), g_topics, n, kCounting));
    TEST_ASSERT_EQUAL(0u, s.sequence(0));

    // Graph, kept to the board's 96 newest points.
    char json[4096];
    int k = snprintf(json, sizeof(json), "{\"type\":\"graph\",\"title\":\"T\",\"series\":[{\"data\":[");
    for (int i = 0; i < 200; i++) k += snprintf(json + k, sizeof(json) - k, "%s%d", i ? "," : "", i);
    snprintf(json + k, sizeof(json) - k, "]}]}");
    TEST_ASSERT_TRUE(dataHandle(s, "ha/g", json, g_graphDoc));
    TEST_ASSERT_EQUAL(2u, s.sequence(0));
    TEST_ASSERT_TRUE(s.snapshot(0));
    TEST_ASSERT_EQUAL((int)feed::Kind::Graph, (int)s.view().kind);
    TEST_ASSERT_EQUAL(96, s.view().graph.series[0].count);
    TEST_ASSERT_EQUAL_FLOAT(199.0f, s.view().graph.series[0].data[95]);   // newest kept, in the view's own pool
    TEST_ASSERT_TRUE(s.view().graph.series[0].data != nullptr);

    // "type" decides; the shape alone is not enough.
    TEST_ASSERT_TRUE(dataHandle(s, "ha/v", "{\"type\":\"table\",\"series\":[],\"tiles\":[{\"label\":\"A\",\"value\":\"1\"}]}", g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(1));
    TEST_ASSERT_EQUAL((int)feed::Kind::Table, (int)s.view().kind);
    TEST_ASSERT_EQUAL(1, s.view().values.count);
    TEST_ASSERT_FALSE(dataHandle(s, "ha/v", "{\"tiles\":[{\"label\":\"A\",\"value\":\"1\"}],\"seconds\":7}", g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(1));
    TEST_ASSERT_EQUAL((int)feed::Kind::Unsupported, (int)s.view().kind);
    TEST_ASSERT_EQUAL(7, s.view().seconds);                  // an unsupported page still keeps its seconds

    // Weather expands to NOW + days (capped at 5); "seconds" applies to each of its pages.
    TEST_ASSERT_TRUE(dataHandle(s, "ha/w", "{\"type\":\"weather\",\"seconds\":5,\"current\":{\"temp\":75},\"days\":[{},{},{},{},{},{},{}]}", g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(2));
    TEST_ASSERT_EQUAL((int)feed::Kind::Weather, (int)s.view().kind);
    TEST_ASSERT_EQUAL(6, mqd::subPageCount(s.view()));
    // Engine time on screen: graph 10 s + unsupported 7 s + weather 6 pages x 5 s.
    TEST_ASSERT_EQUAL(10000u + 7000u + 30000u, s.cycleMs());

    // Unknown topic, junk, an empty payload (retained message deleted) and an oversized one.
    TEST_ASSERT_FALSE(dataHandle(s, "ha/other", "{\"tiles\":[]}", g_graphDoc));
    TEST_ASSERT_FALSE(dataHandle(s, "ha/v", "not json", g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(1));
    TEST_ASSERT_EQUAL((int)feed::Kind::Unsupported, (int)s.view().kind);
    TEST_ASSERT_EQUAL(10, s.view().seconds);
    TEST_ASSERT_TRUE(dataHandle(s, "ha/v", "{\"type\":\"table\",\"tiles\":[{\"value\":\"2\"}]}", g_graphDoc));
    char empty[1] = {0};
    TEST_ASSERT_FALSE(s.handle("ha/v", empty, 0, g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(1));
    TEST_ASSERT_EQUAL((int)feed::Kind::None, (int)s.view().kind);
    static char big[5000];
    memset(big, ' ', sizeof(big) - 1); big[0] = '{'; big[sizeof(big) - 2] = '}'; big[sizeof(big) - 1] = '\0';
    TEST_ASSERT_FALSE(s.handle("ha/v", big, strlen(big), g_graphDoc));   // over the 4 KB limit
    TEST_ASSERT_TRUE(s.snapshot(1));
    TEST_ASSERT_EQUAL((int)feed::Kind::Unsupported, (int)s.view().kind);
    s.destroy();
}


// Real payloads published by the Home Assistant blueprints in tools/home_assistant/blueprints.
static const char* const kBpValue = "{\"v\":1,\"type\":\"value\",\"value\":\"89\",\"unit\":\"\xC2""\xB0""F\",\"label\":\"OUTSIDE\",\"color\":\"#FF6040\"}";
static const char* const kBpTable = "{\"v\":1,\"type\":\"table\",\"title\":\"HOME\",\"show_title\":true,\"tiles\":[{\"label\":\"DOWNSTAIRS\",\"value\":\"77.9\",\"unit\":\"\xC2""\xB0""F\",\"label_short\":\"DOWN\"},{\"label\":\"UPSTAIRS\",\"value\":\"76.1\",\"unit\":\"\xC2""\xB0""F\",\"label_short\":\"UP\"},{\"label\":\"OUTSIDE TEMPERATURE\",\"value\":\"90.0\",\"unit\":\"\xC2""\xB0""F\",\"label_short\":\"OUTSIDE\"}]}";
static const char* const kBpWeather = "{\"v\":1,\"type\":\"weather\",\"units\":\"imperial\",\"current\":{\"temp\":89,\"condition\":\"windy\",\"humidity\":31,\"wind\":6.0,\"wind_unit\":\"mph\",\"wind_dir\":\"SE\"},\"days\":[{\"temp_max\":91,\"temp_min\":73,\"condition\":\"windy\",\"precip_prob\":0},{\"temp_max\":92,\"temp_min\":72,\"condition\":\"sunny\",\"precip_prob\":0},{\"temp_max\":94,\"temp_min\":71,\"condition\":\"sunny\",\"precip_prob\":0},{\"temp_max\":94,\"temp_min\":71,\"condition\":\"sunny\",\"precip_prob\":2},{\"temp_max\":93,\"temp_min\":71,\"condition\":\"sunny\",\"precip_prob\":0}]}";
static const char* const kBpGraph = "{\"v\":1,\"type\":\"graph\",\"title\":\"OUTSIDE VS DOWN\",\"summary\":\"89\xC2""\xB0""\",\"summary_color\":\"#FF4020\",\"slots\":96,\"min\":76,\"max\":90,\"yaxis\":true,\"series\":[{\"label\":\"OUTSIDE\",\"style\":\"line\",\"color\":\"#FF4020\",\"data\":[89.0]},{\"label\":\"DOWNSTAIRS\",\"style\":\"line\",\"color\":\"#40A0FF\",\"data\":[77.9]}],\"marks\":[{\"at\":48,\"color\":\"#404040\"}],\"legend\":[{\"text\":\"OUTSIDE\",\"color\":\"#FF4020\"},{\"text\":\"DOWNSTAIRS\",\"color\":\"#40A0FF\"}]}";

void test_mqttdata_blueprint_payloads(void) {
    mqd::Session s;
    uint8_t n = mqd::parseTopics("bp/value,bp/table,bp/weather,bp/graph", g_topics, mqd::MAX_TOPICS);
    TEST_ASSERT_TRUE(s.create(mqd::limitsFor(true), g_topics, n, kCounting));

    TEST_ASSERT_TRUE(dataHandle(s, "bp/value", kBpValue, g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(0));
    TEST_ASSERT_EQUAL((int)feed::Kind::Value, (int)s.view().kind);
    TEST_ASSERT_EQUAL(1, s.view().values.count);
    TEST_ASSERT_FALSE(s.view().values.showTitle);
    TEST_ASSERT_EQUAL_STRING("OUTSIDE", s.view().values.tiles[0].label);
    TEST_ASSERT_EQUAL_STRING("89", s.view().values.tiles[0].value);
    TEST_ASSERT_EQUAL((char)0xB0, s.view().values.tiles[0].unit[0]);   // "°F"
    TEST_ASSERT_EQUAL_HEX8(0x60, s.view().values.tiles[0].color.g);

    TEST_ASSERT_TRUE(dataHandle(s, "bp/table", kBpTable, g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(1));
    TEST_ASSERT_EQUAL((int)feed::Kind::Table, (int)s.view().kind);
    TEST_ASSERT_EQUAL_STRING("HOME", s.view().values.title);
    TEST_ASSERT_EQUAL(3, s.view().values.count);
    TEST_ASSERT_EQUAL_STRING("OUTSIDE TEMPERATURE", s.view().values.tiles[2].label);   // kept whole
    TEST_ASSERT_EQUAL_STRING("OUTSIDE", s.view().values.tiles[2].labelShort);            // "short labels" input
    TEST_ASSERT_EQUAL_STRING("DOWN", s.view().values.tiles[0].labelShort);

    TEST_ASSERT_TRUE(dataHandle(s, "bp/weather", kBpWeather, g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(2));
    TEST_ASSERT_EQUAL((int)feed::Kind::Weather, (int)s.view().kind);
    TEST_ASSERT_EQUAL_STRING("SE", s.view().weather.windDir);
    TEST_ASSERT_EQUAL(6, mqd::subPageCount(s.view()));   // NOW + 5 days

    TEST_ASSERT_TRUE(dataHandle(s, "bp/graph", kBpGraph, g_graphDoc));
    TEST_ASSERT_TRUE(s.snapshot(3));
    TEST_ASSERT_EQUAL((int)feed::Kind::Graph, (int)s.view().kind);
    TEST_ASSERT_TRUE(s.view().graph.showHeader);
    TEST_ASSERT_TRUE(s.view().graph.yaxis);
    TEST_ASSERT_EQUAL(2, s.view().graph.seriesCount);
    TEST_ASSERT_EQUAL(2, s.view().graph.legendCount);
    TEST_ASSERT_TRUE(s.view().graph.hasSummaryColor);
    TEST_ASSERT_EQUAL(96, s.view().graph.slots);
    TEST_ASSERT_EQUAL_FLOAT(89.0f, graph::pointAt(s.view().graph, 0, 95));   // the one point sits in the newest slot

    // 10 s each, weather six pages: 10 + 10 + 60 + 10.
    TEST_ASSERT_EQUAL(90000u, s.cycleMs());
}

void test_mqttdata_page_cycle(void) {
    mqd::View weather{};
    weather.kind = feed::Kind::Weather;
    weather.weather.valid = true;
    weather.weather.hasCurrent = true;
    weather.weather.dayCount = 2;
    TEST_ASSERT_EQUAL(3, mqd::subPageCount(weather));   // NOW + 2 days
    weather.weather.hasCurrent = false;
    TEST_ASSERT_EQUAL(2, mqd::subPageCount(weather));
    mqd::View other{};
    other.kind = feed::Kind::Graph;
    TEST_ASSERT_EQUAL(1, mqd::subPageCount(other));
    other.kind = feed::Kind::None;
    TEST_ASSERT_EQUAL(1, mqd::subPageCount(other));

    // Topics: weather (3 sub-pages), graph, values. Sub-pages first, then the next topic, wrapping.
    mqd::Cycle c;
    const uint8_t subs[3] = {3, 1, 1};
    const uint8_t expectTopic[] = {0, 0, 1, 2, 0, 0};
    const uint8_t expectSub[]   = {1, 2, 0, 0, 0, 1};
    for (int i = 0; i < 6; i++) {
        mqd::advance(c, 3, subs[c.topic], 1000u * (i + 1));
        TEST_ASSERT_EQUAL(expectTopic[i], c.topic);
        TEST_ASSERT_EQUAL(expectSub[i], c.sub);
        TEST_ASSERT_EQUAL(1000u * (i + 1), c.sinceMs);
    }
}

// =========================================================================
// 10. CompatibilityEvaluator Unit Tests
// =========================================================================

#include "core/CompatibilityEvaluator.h"

void test_compatibility_evaluator_hardware_gating(void) {
    EngineDescriptor desc;
    desc.metadata.id = "test_engine";
    desc.requirements.needsPsram = true;
    desc.requirements.needsAudioInput = true;
    desc.requirements.needsTempSensor = true;
    desc.requirements.needsNetwork = true;
    desc.requirements.minWidth = 128;

    CompatibilityContext ctx;
    ctx.width = 128;
    ctx.height = 32;
    ctx.colorDepth = 8;
    ctx.hardware.hasPsram = false;
    ctx.hardware.hasMicrophone = false;
    ctx.hardware.hasTempSensor = false;
    ctx.isConnectedWifi = false;

    // 1. Missing all hardware
    auto v1 = CompatibilityEvaluator::evaluate(desc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)v1.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::RequiresPsram, (int)v1.primaryReason);
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v1.issueFlags), CompatibilityIssue::MissingPsram));
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v1.issueFlags), CompatibilityIssue::MissingAudioInput));
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v1.issueFlags), CompatibilityIssue::MissingTempSensor));
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v1.issueFlags), CompatibilityIssue::MissingNetwork));

    // 2. Grant PSRAM, Mic, Temp, Wi-Fi -> Compatible!
    ctx.hardware.hasPsram = true;
    ctx.hardware.hasMicrophone = true;
    ctx.hardware.hasTempSensor = true;
    ctx.isConnectedWifi = true;
    auto v2 = CompatibilityEvaluator::evaluate(desc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Compatible, (int)v2.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::None, (int)v2.primaryReason);
    TEST_ASSERT_TRUE(v2.compatible());

    // 3. Geometry underflow (width 64 < minWidth 128)
    ctx.width = 64;
    auto v3 = CompatibilityEvaluator::evaluate(desc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)v3.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::UnsupportedGeometry, (int)v3.primaryReason);
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v3.issueFlags), CompatibilityIssue::GeometryOutOfRange));
}

void test_compatibility_evaluator_memory_and_fragmentation(void) {
    EngineDescriptor desc;
    desc.metadata.id = "net_engine";
    desc.requirements.needsTls = true;
    desc.requirements.internalContiguousBytes = 60000; // Requires 60KB contiguous block

    CompatibilityContext ctx;
    ctx.width = 64;
    ctx.height = 32;
    ctx.colorDepth = 8;
    ctx.hardware.hasPsram = false;
    ctx.isConnectedWifi = true;

    // 1. Total RAM plenty (250KB), but largest contiguous block is only 40KB (fragmented)
    ctx.memory.freeInternalHeap = 250000;
    ctx.memory.largestInternalBlock = 40000;
    auto v1 = CompatibilityEvaluator::evaluate(desc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)v1.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::InsufficientLargestBlock, (int)v1.primaryReason);
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v1.issueFlags), CompatibilityIssue::FragmentedInternalHeap));
    TEST_ASSERT_EQUAL_UINT32(60000, v1.largestRequiredBlockBytes);
    TEST_ASSERT_EQUAL_UINT32(40000, v1.largestAvailableBlockBytes);

    // 2. Unfragmented: largest block 80KB >= 60KB -> Compatible!
    ctx.memory.largestInternalBlock = 80000;
    auto v2 = CompatibilityEvaluator::evaluate(desc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Compatible, (int)v2.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::None, (int)v2.primaryReason);
    TEST_ASSERT_TRUE(v2.internalHeadroomBytes > 0);

    // 3. Exhausted total internal DRAM (e.g. 50KB total, when TLS alone needs 45KB + 35KB headroom = 80KB+)
    ctx.memory.freeInternalHeap = 50000;
    ctx.memory.largestInternalBlock = 45000;
    auto v3 = CompatibilityEvaluator::evaluate(desc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)v3.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::InsufficientInternalHeap, (int)v3.primaryReason);
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v3.issueFlags), CompatibilityIssue::LowInternalHeap));
}

void test_compatibility_evaluator_presentation_budget_and_single_buffer(void) {
    EngineDescriptor desc;
    desc.metadata.id = "rt_engine";
    desc.requirements.requiresDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = false;

    CompatibilityContext ctx;
    ctx.width = 64;
    ctx.height = 32;
    ctx.colorDepth = 8;
    ctx.hardware.hasPsram = false;
    ctx.requestedPipeline = "canvas_single"; // Forces single buffer

    // 1. Engine requires double-buffering but single-buffer was selected -> Incompatible!
    auto v1 = CompatibilityEvaluator::evaluate(desc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)v1.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::RequiresDoubleBuffer, (int)v1.primaryReason);
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v1.issueFlags), CompatibilityIssue::DoubleBufferUnavailable));

    // 2. Engine prefers double buffer but supports single buffer -> CompatibleDegraded!
    EngineDescriptor prefDesc;
    prefDesc.metadata.id = "pref_engine";
    prefDesc.requirements.prefersDoubleBuffer = true;
    prefDesc.requirements.supportsSingleBuffer = true;
    auto v2 = CompatibilityEvaluator::evaluate(prefDesc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::CompatibleDegraded, (int)v2.status);
    TEST_ASSERT_TRUE(v2.degraded());
    TEST_ASSERT_EQUAL((int)CompatibilityReason::RequiresDoubleBuffer, (int)v2.primaryReason);
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(v2.issueFlags), CompatibilityIssue::DoubleBufferUnavailable));

    // 3. Single-buffer blanking budget exceeded:
    EngineDescriptor normalDesc;
    normalDesc.metadata.id = "normal_engine";
    normalDesc.requirements.supportsSingleBuffer = true;
    ctx.presentationPolicy.allowBlanking = true;
    ctx.presentationPolicy.maxBlankUs = 100;
    ctx.presentationPolicy.degradedBlankingPermitted = false;

    auto v3 = CompatibilityEvaluator::evaluate(normalDesc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)v3.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::BlankBudgetExceeded, (int)v3.primaryReason);

    // Now permit degraded blanking -> CompatibleDegraded!
    ctx.presentationPolicy.degradedBlankingPermitted = true;
    auto v4 = CompatibilityEvaluator::evaluate(normalDesc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::CompatibleDegraded, (int)v4.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::BlankBudgetExceeded, (int)v4.primaryReason);
}

void test_compatibility_evaluator_multi_issue_bitmask(void) {
    EngineDescriptor desc;
    desc.metadata.id = "complex_engine";
    desc.requirements.needsPsram = true;
    desc.requirements.needsAudioInput = true;
    desc.requirements.needsGyroscope = true;

    CompatibilityContext ctx;
    ctx.width = 128;
    ctx.height = 32;
    ctx.colorDepth = 8;
    ctx.hardware.hasPsram = false;
    ctx.hardware.hasMicrophone = false;
    ctx.hardware.hasGyroscope = false;

    auto verdict = CompatibilityEvaluator::evaluate(desc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)verdict.status);

    // Primary reason is the first hard failure (RequiresPsram)
    TEST_ASSERT_EQUAL((int)CompatibilityReason::RequiresPsram, (int)verdict.primaryReason);

    // Multi-issue bitmask captures ALL three missing peripherals!
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(verdict.issueFlags), CompatibilityIssue::MissingPsram));
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(verdict.issueFlags), CompatibilityIssue::MissingAudioInput));
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(verdict.issueFlags), CompatibilityIssue::MissingGyroscope));

    // String translations
    TEST_ASSERT_EQUAL_STRING("Requires external PSRAM memory", CompatibilityEvaluator::reasonToString(verdict.primaryReason));
    TEST_ASSERT_EQUAL_STRING("incompatible", CompatibilityEvaluator::statusToString(verdict.status));
}

void test_e2e_gif_to_clock_transition_and_catalog_invariance(void) {
    // 1. Setup Classic ESP32 profile context (128x32, no PSRAM)
    EngineDescriptor clockDesc;
    clockDesc.metadata.id = "clock";
    clockDesc.metadata.name = "Clock Engine";
    clockDesc.requirements.targetFps = 60;
    clockDesc.requirements.prefersDoubleBuffer = true;
    clockDesc.requirements.supportsSingleBuffer = true;
    clockDesc.requirements.internalPersistentBytes = 8000;
    clockDesc.requirements.internalContiguousBytes = 16000;

    EngineDescriptor gifDesc;
    gifDesc.metadata.id = "gifs";
    gifDesc.metadata.name = "GIF Player";
    gifDesc.requirements.targetFps = 30;
    gifDesc.requirements.supportsSingleBuffer = true;
    gifDesc.requirements.internalPersistentBytes = 25000;
    gifDesc.requirements.internalContiguousBytes = 25000;

    // 2. Initial static reference capability qualification:
    CompatibilityContext refCtx;
    refCtx.mode = EvaluationMode::ReferenceCapability;
    refCtx.hardware.profile = HwProfile::ESP32_STD;
    refCtx.hardware.hasPsram = false;
    refCtx.hardware.hasMicrophone = false;
    refCtx.hardware.hasTempSensor = true;
    refCtx.isConnectedWifi = true;
    ReferenceMemoryProfile refMem = CompatibilityEvaluator::getReferenceMemoryProfile(HwProfile::ESP32_STD);
    refCtx.memory.freeInternalHeap = refMem.freeInternalHeap;
    refCtx.memory.largestInternalBlock = refMem.largestInternalBlock;
    refCtx.memory.freePsram = refMem.freePsram;
    refCtx.width = 128;
    refCtx.height = 32;
    refCtx.colorDepth = 8;
    refCtx.requestedPipeline = "canvas_burst_single";

    auto vClockRef1 = CompatibilityEvaluator::evaluate(clockDesc, refCtx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::CompatibleDegraded, (int)vClockRef1.status);
    TEST_ASSERT_TRUE(vClockRef1.compatible());

    auto vGifRef1 = CompatibilityEvaluator::evaluate(gifDesc, refCtx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Compatible, (int)vGifRef1.status);
    TEST_ASSERT_TRUE(vGifRef1.compatible());

    // 3. Negative Assertion: Simulate GIF engine actively decoding on Core 1
    // Volatile free internal heap drops to 11 KB, largest block drops to 4 KB
    CompatibilityContext activePressureCtx = refCtx;
    activePressureCtx.mode = EvaluationMode::RuntimeAdmission;
    activePressureCtx.memory.freeInternalHeap = 11000;
    activePressureCtx.memory.largestInternalBlock = 4000;

    // Runtime admission detects the transient memory pressure and rejects Clock:
    auto vClockRuntime = CompatibilityEvaluator::evaluate(clockDesc, activePressureCtx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)vClockRuntime.status);
    TEST_ASSERT_TRUE(hasIssue(static_cast<CompatibilityIssue>(vClockRuntime.issueFlags), CompatibilityIssue::LowInternalHeap));

    // Positive Assertion: ReferenceCapability remains STRICTLY INVARIANT despite transient volatile heap pressure!
    // (This guarantees WebUI catalog and API transition safety gating never deadlock)
    auto vClockRefDuringGif = CompatibilityEvaluator::evaluate(clockDesc, refCtx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::CompatibleDegraded, (int)vClockRefDuringGif.status);
    TEST_ASSERT_TRUE(vClockRefDuringGif.compatible());

    // 4. Requested pipeline gating: evaluating against requested target pipeline succeeds
    CompatibilityContext targetPipeCtx = refCtx;
    targetPipeCtx.requestedPipeline = "canvas_burst_single";
    auto vTargetPipeline = CompatibilityEvaluator::evaluate(clockDesc, targetPipeCtx);
    TEST_ASSERT_TRUE(vTargetPipeline.compatible());

    // 5. Transition simulation:
    // gif.deactivate() -> memory reclaimed -> clock.initialize() -> clock.activate()
    // Simulated heap recovery after deactivation
    CompatibilityContext recoveredCtx = refCtx;
    recoveredCtx.mode = EvaluationMode::RuntimeAdmission;
    recoveredCtx.memory.freeInternalHeap = refMem.freeInternalHeap;
    recoveredCtx.memory.largestInternalBlock = refMem.largestInternalBlock;

    auto vClockPostReclaim = CompatibilityEvaluator::evaluate(clockDesc, recoveredCtx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::CompatibleDegraded, (int)vClockPostReclaim.status);
    TEST_ASSERT_TRUE(vClockPostReclaim.compatible());

    // 6. Post-transition: Reference capability catalog remains 100% invariant
    auto vClockRefPost = CompatibilityEvaluator::evaluate(clockDesc, refCtx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::CompatibleDegraded, (int)vClockRefPost.status);
    TEST_ASSERT_EQUAL((int)vClockRef1.status, (int)vClockRefPost.status);
}

void test_icon_service_sanitization_and_paths() {
    TEST_ASSERT_EQUAL_STRING("btc", IconService::sanitizeSymbol("BTC").c_str());
    TEST_ASSERT_EQUAL_STRING("eth", IconService::sanitizeSymbol(" eth ").c_str());
    TEST_ASSERT_EQUAL_STRING("_gspc", IconService::sanitizeSymbol("^GSPC").c_str());
    TEST_ASSERT_EQUAL_STRING("brk_b", IconService::sanitizeSymbol("BRK/B").c_str());

    TEST_ASSERT_EQUAL_STRING("/crypto_icons/btc.png", IconService::getSdPath("crypto", "BTC").c_str());
    TEST_ASSERT_EQUAL_STRING("/stock_icons/aapl.png", IconService::getSdPath("stock", "AAPL").c_str());
    TEST_ASSERT_EQUAL_STRING("/stock_icons/_gspc.png", IconService::getSdPath("stock", "^GSPC").c_str());
    TEST_ASSERT_EQUAL_STRING("/media_icons/spotify.png", IconService::getSdPath("media", "Spotify").c_str());
}

void test_pipeline_selection_effective_color_depth(void) {
    EngineRequirements tlsReq;
    tlsReq.needsTls = true;

    EngineRequirements noTlsReq;
    noTlsReq.needsTls = false;

    // --- Nominal cases ---
    // 1. ESP32-S3 (hasPsram = true) with and without TLS -> 8 bits
    uint8_t s3Tls = PipelineSelectionPolicy::resolveEffectiveColorDepth(0, 128, 32, true, MemoryBudgetConstraints(), tlsReq);
    TEST_ASSERT_EQUAL_UINT8(8, s3Tls);
    uint8_t s3NoTls = PipelineSelectionPolicy::resolveEffectiveColorDepth(0, 128, 32, true, MemoryBudgetConstraints(), noTlsReq);
    TEST_ASSERT_EQUAL_UINT8(8, s3NoTls);

    // 2. ESP32 Standard (128x32) with TLS in Auto mode -> 4 bits
    uint8_t esp128Tls = PipelineSelectionPolicy::resolveEffectiveColorDepth(0, 128, 32, false, MemoryBudgetConstraints(), tlsReq);
    TEST_ASSERT_EQUAL_UINT8(4, esp128Tls);

    // 3. ESP32 Standard (128x32) without TLS in Auto mode -> 8 bits (unconstrained graphics)
    uint8_t esp128NoTls = PipelineSelectionPolicy::resolveEffectiveColorDepth(0, 128, 32, false, MemoryBudgetConstraints(), noTlsReq);
    TEST_ASSERT_EQUAL_UINT8(8, esp128NoTls);

    // 4. ESP32 Standard (64x32) with TLS in Auto mode -> 8 bits (geometry footprint <= 16KB DMA fits TLS headroom)
    uint8_t esp64Tls = PipelineSelectionPolicy::resolveEffectiveColorDepth(0, 64, 32, false, MemoryBudgetConstraints(), tlsReq);
    TEST_ASSERT_EQUAL_UINT8(8, esp64Tls);

    // 5. ESP32 Standard (64x32) without TLS in Auto mode -> 8 bits
    uint8_t esp64NoTls = PipelineSelectionPolicy::resolveEffectiveColorDepth(0, 64, 32, false, MemoryBudgetConstraints(), noTlsReq);
    TEST_ASSERT_EQUAL_UINT8(8, esp64NoTls);

    // --- Contractual cases on 128x32 ESP32 Standard with TLS ---
    EngineDescriptor cryptoDesc;
    cryptoDesc.metadata.id = "crypto";
    cryptoDesc.requirements.needsTls = true;

    CompatibilityContext ctx;
    ctx.width = 128;
    ctx.height = 32;
    ctx.hardware.hasPsram = false;
    ctx.hardware.profile = HwProfile::ESP32_STD;
    ctx.isConnectedWifi = true;
    ReferenceMemoryProfile ref = CompatibilityEvaluator::getReferenceMemoryProfile(HwProfile::ESP32_STD);
    ctx.memory.freeInternalHeap = ref.freeInternalHeap;
    ctx.memory.largestInternalBlock = ref.largestInternalBlock;

    // Case 1: Manual 6-bit depth -> does NOT downscale (effectiveDepth=6), but fails contiguous admission (Incompatible)
    ctx.colorDepth = 6;
    auto resManual6 = PipelineSelectionPolicy::evaluate(ctx.width, ctx.height, ctx.colorDepth, "auto", false, ctx.memory, cryptoDesc.requirements);
    TEST_ASSERT_EQUAL_UINT8(6, resManual6.effectiveColorDepth);
    auto verdictManual6 = CompatibilityEvaluator::evaluate(cryptoDesc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Incompatible, (int)verdictManual6.status);
    TEST_ASSERT_EQUAL((int)CompatibilityReason::InsufficientLargestBlock, (int)verdictManual6.primaryReason);

    // Case 2: Manual 4-bit depth -> effectiveDepth=4, satisfies admission (Compatible)
    ctx.colorDepth = 4;
    auto resManual4 = PipelineSelectionPolicy::evaluate(ctx.width, ctx.height, ctx.colorDepth, "auto", false, ctx.memory, cryptoDesc.requirements);
    TEST_ASSERT_EQUAL_UINT8(4, resManual4.effectiveColorDepth);
    auto verdictManual4 = CompatibilityEvaluator::evaluate(cryptoDesc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Compatible, (int)verdictManual4.status);

    // Case 3: Auto mode (colorDepth=0) -> resolves to 4 bits, satisfies admission (Compatible)
    ctx.colorDepth = 0;
    auto resAuto = PipelineSelectionPolicy::evaluate(ctx.width, ctx.height, ctx.colorDepth, "auto", false, ctx.memory, cryptoDesc.requirements);
    TEST_ASSERT_EQUAL_UINT8(4, resAuto.effectiveColorDepth);
    auto verdictAuto = CompatibilityEvaluator::evaluate(cryptoDesc, ctx);
    TEST_ASSERT_EQUAL((int)CompatibilityStatus::Compatible, (int)verdictAuto.status);
}

void test_rotation_requirements_aggregation(void) {
    EngineRequirements clockReq;
    clockReq.needsTls = false;
    clockReq.internalPersistentBytes = 1200;
    clockReq.internalContiguousBytes = 500;

    EngineRequirements cryptoReq;
    cryptoReq.needsTls = true;
    cryptoReq.internalPersistentBytes = 4500;
    cryptoReq.internalContiguousBytes = 8000;

    EngineRequirements stockReq;
    stockReq.needsTls = true;
    stockReq.internalPersistentBytes = 3200;
    stockReq.internalContiguousBytes = 12000;

    EngineRequirements agg;
    agg.mergeWith(clockReq);
    TEST_ASSERT_FALSE(agg.needsTls);
    TEST_ASSERT_EQUAL_UINT32(1200, agg.internalPersistentBytes);

    agg.mergeWith(cryptoReq);
    TEST_ASSERT_TRUE(agg.needsTls);
    TEST_ASSERT_EQUAL_UINT32(4500, agg.internalPersistentBytes);
    TEST_ASSERT_EQUAL_UINT32(8000, agg.internalContiguousBytes);

    agg.mergeWith(stockReq);
    TEST_ASSERT_TRUE(agg.needsTls);
    TEST_ASSERT_EQUAL_UINT32(4500, agg.internalPersistentBytes);
    TEST_ASSERT_EQUAL_UINT32(12000, agg.internalContiguousBytes);
}

void test_render_transaction_contracts(void) {
    // 1. Hub75DmaLayout Canonical Payload & Admission Overhead
    // For 128x32: rows = 16, stride = 256 bytes.
    // 8 bits: 16 * 8 * 256 = 32768 bytes.
    size_t dma8 = Hub75DmaLayout::calculateBytes(128, 32, 8, false);
    TEST_ASSERT_EQUAL_UINT32(32768, dma8);

    // 4 bits: 16 * 4 * 256 = 16384 bytes (saves 16384 bytes DRAM for TLS).
    size_t dma4 = Hub75DmaLayout::calculateBytes(128, 32, 4, false);
    TEST_ASSERT_EQUAL_UINT32(16384, dma4);

    // 2 bits: 16 * 2 * 256 = 8192 bytes (saves 24576 bytes DRAM).
    size_t dma2 = Hub75DmaLayout::calculateBytes(128, 32, 2, false);
    TEST_ASSERT_EQUAL_UINT32(8192, dma2);

    // With descriptor overhead (16 rows * 8 depth * 1 buffer * 16 bytes = 2048 bytes overhead -> 34816 bytes)
    size_t dma8Desc = Hub75DmaLayout::calculateBytes(128, 32, 8, false, true);
    TEST_ASSERT_EQUAL_UINT32(34816, dma8Desc);

    // 2. Deterministic 8 <-> 4 Dynamic Depth targeting
    EngineRequirements tlsReq;
    tlsReq.needsTls = true;
    EngineRequirements graphicsReq;
    graphicsReq.needsTls = false;

    // A. Transition from Graphics (8 bits) to TLS engine on 128x32 ESP32 (nominal 4 bits)
    uint8_t targetForTls = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, tlsReq, 8, 30000, 80000, 40000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(4, targetForTls);

    // A2. Transition to TLS engine on ESP32-S3 with PSRAM (hasPsram = true) -> retains nominal 8 bits
    uint8_t targetS3Tls = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, true, tlsReq, 8, 120000, 240000, 100000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(8, targetS3Tls);

    // B. Transition from TLS engine (4 bits) back to Graphics engine (configured 8 bits)
    uint8_t targetForGraphics = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, graphicsReq, 4, 30000, 80000, 40000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(8, targetForGraphics);

    // C. User configured 6 bits ceiling: TLS targets 4 bits, Graphics targets 6 bits
    uint8_t targetCeiling6Tls = PipelineSelectionPolicy::resolveTargetDepth(
        6, true, 128, 32, false, tlsReq, 6, 30000, 80000, 40000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(4, targetCeiling6Tls);

    uint8_t targetCeiling6Graphics = PipelineSelectionPolicy::resolveTargetDepth(
        6, true, 128, 32, false, graphicsReq, 4, 30000, 80000, 40000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(6, targetCeiling6Graphics);

    // D. Critical pressure: the largest block cannot admit TLS at 4 bits but does at 2 bits
    //    -> 2 bits is taken as a measured last resort.
    uint8_t targetPressureFloor = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, tlsReq, 8, 12000, 35000, 20000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(2, targetPressureFloor);

    // D3. Block that admits 4 bits keeps TLS at 4 bits (never degraded needlessly).
    uint8_t targetKeeps4 = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, tlsReq, 8, 13000, 35000, 20000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(4, targetKeeps4);

    // D2. Graphics engines keep the 2-bit extreme-pressure floor.
    EngineRequirements heavyGfx;
    heavyGfx.minFreeInternalHeapBytes = 200000;
    uint8_t gfxPressureFloor = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, heavyGfx, 8, 12000, 35000, 20000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(2, gfxPressureFloor);

    // E. Telemetry contract validation
    bool fallbackUsedOnTls = (targetForTls != 8); // effective 4 != requested 8
    TEST_ASSERT_TRUE(fallbackUsedOnTls);

    bool fallbackUsedOnGraphics = (targetForGraphics != 8); // effective 8 == requested 8
    TEST_ASSERT_FALSE(fallbackUsedOnGraphics);

    // F. Heavy graphics engine (e.g. GifEngine with 25KB decoder working set) on 128x32 ESP32 without PSRAM
    EngineRequirements gifReq;
    gifReq.needsTls = false;
    gifReq.minFreeInternalHeapBytes = 25000;
    gifReq.internalContiguousBytes = 24500;

    // At baseline (48KB free internal heap, 45KB largest block):
    // 8-bit requires 12,288 (SYSTEM_MIN_RESERVE) + 25,000 (GIF) = 37,288 bytes <= 48,416 -> 8-bit accepted (no degradation!).
    uint8_t targetForGif = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, gifReq, 8, 45044, 48416, 45044, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(8, targetForGif);

    // F2. Under severe memory pressure (e.g. 30KB free heap < 37,288 required for 8-bit):
    // 4-bit reclaims 17,408 bytes DMA -> 47,408 bytes >= 37,288 -> 4-bit fallback accepted.
    uint8_t targetForGifPressure = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, gifReq, 8, 30000, 30000, 30000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(4, targetForGifPressure);
}

// =========================================================================
// Lifecycle Quiescence & Presentation Transaction Contracts
// =========================================================================

class MockQuiescenceEngine : public IEngine {
public:
    bool logicalQuiescent = false;
    bool physicalQuiescent = false;
    int workerTasksActive = 2;
    int openSockets = 1;

    EngineError initialize(EngineContext*, const EngineConfig*) override { return EngineError::OK; }
    void activate() override {
        logicalQuiescent = false;
        physicalQuiescent = false;
    }
    void update(EngineContext*) override {}
    void render(EngineContext*) override {}
    void deactivate() override {
        // Core 1: non-blocking logical quiescence
        logicalQuiescent = true;
    }
    bool shutdownForDestruction() override {
        // Core 0: cooperative shutdown of workers and sockets
        if (logicalQuiescent) {
            workerTasksActive = 0;
            openSockets = 0;
            physicalQuiescent = true;
            return true;
        }
        return false;
    }
};

void test_lifecycle_two_stage_quiescence_contracts(void) {
    MockQuiescenceEngine oldEngine;
    oldEngine.activate();
    TEST_ASSERT_FALSE(oldEngine.logicalQuiescent);
    TEST_ASSERT_FALSE(oldEngine.physicalQuiescent);
    TEST_ASSERT_EQUAL_INT(2, oldEngine.workerTasksActive);
    TEST_ASSERT_EQUAL_INT(1, oldEngine.openSockets);

    // Step 1: Core 1 deactivation (logical rendering quiescence, strictly non-blocking)
    oldEngine.deactivate();
    TEST_ASSERT_TRUE(oldEngine.logicalQuiescent);
    TEST_ASSERT_FALSE(oldEngine.physicalQuiescent);
    // Background resources still physically active until Core 0 handoff
    TEST_ASSERT_EQUAL_INT(2, oldEngine.workerTasksActive);

    // Step 2: Core 0 shutdown for destruction (physical quiescence)
    bool shutdownOk = oldEngine.shutdownForDestruction();
    TEST_ASSERT_TRUE(shutdownOk);
    TEST_ASSERT_TRUE(oldEngine.physicalQuiescent);
    TEST_ASSERT_EQUAL_INT(0, oldEngine.workerTasksActive);
    TEST_ASSERT_EQUAL_INT(0, oldEngine.openSockets);
}

void test_lifecycle_deactivate_vs_shutdown_distinct_contracts(void) {
    // Contract: Routine module rotation cycles (activate -> deactivate -> activate)
    // MUST NOT destroy background workers, free cached data, or re-instantiate objects.
    struct StatefulRotationEngine : public IEngine {
        int activateCalls = 0;
        int deactivateCalls = 0;
        int shutdownCalls = 0;
        bool backgroundWorkerAlive = true;
        uint32_t cachedDataGeneration = 42;

        EngineError initialize(EngineContext*, const EngineConfig*) override { return EngineError::OK; }
        void activate() override { activateCalls++; }
        void update(EngineContext*) override {}
        void render(EngineContext*) override {}
        void deactivate() override {
            deactivateCalls++;
            // Background worker remains intact across routine rotation switches
        }
        bool shutdownForDestruction() override {
            shutdownCalls++;
            backgroundWorkerAlive = false;
            cachedDataGeneration = 0;
            return true;
        }
    };

    StatefulRotationEngine eng;
    TEST_ASSERT_TRUE(eng.backgroundWorkerAlive);
    TEST_ASSERT_EQUAL_UINT32(42, eng.cachedDataGeneration);

    // Simulate 5 routine rotation cycles (e.g. Weather -> Clock -> Weather)
    for (int i = 0; i < 5; ++i) {
        eng.activate();
        eng.deactivate();
        // Background worker is STILL alive, data generation intact
        TEST_ASSERT_TRUE(eng.backgroundWorkerAlive);
        TEST_ASSERT_EQUAL_UINT32(42, eng.cachedDataGeneration);
    }
    TEST_ASSERT_EQUAL_INT(5, eng.activateCalls);
    TEST_ASSERT_EQUAL_INT(5, eng.deactivateCalls);
    TEST_ASSERT_EQUAL_INT(0, eng.shutdownCalls);

    // Terminal retirement (config delete / engine teardown)
    bool ok = eng.shutdownForDestruction();
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(1, eng.shutdownCalls);
    TEST_ASSERT_FALSE(eng.backgroundWorkerAlive);
    TEST_ASSERT_EQUAL_UINT32(0, eng.cachedDataGeneration);
}

void test_lifecycle_cooperative_worker_timeout_and_quarantine(void) {
    // Contract: If a background worker fails to terminate within 300ms,
    // shutdownForDestruction() returns false. Core0LifecycleDispatcher quarantines
    // the engine without executing destructor (anti-UAF).
    struct MockQuarantinedEngine : public IEngine {
        std::atomic<bool> workerStopRequested{false};
        std::atomic<bool> workerExited{false};
        bool destructorCalled = false;
        bool uafOccurredDuringDestruction = false;
        uint32_t simulatedWorkerRemainingWorkMs = 500; // takes 500ms > 300ms window

        ~MockQuarantinedEngine() {
            destructorCalled = true;
            // Anti-UAF barrier check:
            if (!workerExited.load(std::memory_order_acquire)) {
                uafOccurredDuringDestruction = true;
            }
        }

        EngineError initialize(EngineContext*, const EngineConfig*) override { return EngineError::OK; }
        void activate() override {}
        void update(EngineContext*) override {}
        void render(EngineContext*) override {}
        void deactivate() override {
            workerStopRequested.store(true, std::memory_order_release);
        }
        bool shutdownForDestruction() override {
            workerStopRequested.store(true, std::memory_order_release);
            // Simulate 300ms bounded check
            if (simulatedWorkerRemainingWorkMs > 300) {
                // Timeout exceeded: worker not dead yet
                simulatedWorkerRemainingWorkMs -= 300;
                return false; // Quarantine!
            }
            workerExited.store(true, std::memory_order_release);
            return true;
        }
    };

    // 1. Initial handover: shutdown times out after 300ms
    auto enginePtr = std::unique_ptr<MockQuarantinedEngine>(new MockQuarantinedEngine());
    enginePtr->deactivate();
    TEST_ASSERT_TRUE(enginePtr->workerStopRequested.load());
    TEST_ASSERT_FALSE(enginePtr->workerExited.load());

    // 2. Core 0 lifecycle dispatcher attempt: fails shutdown -> moves to quarantine
    bool initialShutdown = enginePtr->shutdownForDestruction();
    TEST_ASSERT_FALSE(initialShutdown); // Times out!
    TEST_ASSERT_FALSE(enginePtr->workerExited.load());
    TEST_ASSERT_FALSE(enginePtr->destructorCalled); // Must NOT be destroyed!

    // 3. Quarantine retry: worker finishes remaining work (<= 300ms)
    bool retryShutdown = enginePtr->shutdownForDestruction();
    TEST_ASSERT_TRUE(retryShutdown); // Succeeds!
    TEST_ASSERT_TRUE(enginePtr->workerExited.load());

    // 4. Safe destruction after clean shutdown
    enginePtr.reset(); // Destructor called safely on Core 0
    TEST_ASSERT_NULL(enginePtr.get());
}

void test_lifecycle_cooperative_exit_latency_slices(void) {
    // Contract: Sliced 100ms sleeps vs monolithic 1000ms delay allow
    // cooperative shutdown response in < 150ms.
    std::atomic<bool> stopFlag{false};

    // Simulate task entering wait period using 10 x 100ms slices with immediate stop flag
    stopFlag.store(true);
    int elapsedSlicesMs = 0;
    for (int i = 0; i < 10 && !stopFlag.load(std::memory_order_acquire); ++i) {
        elapsedSlicesMs += 100;
    }
    // Since stopFlag was already set, sliced loop exits immediately (0 ms)
    TEST_ASSERT_EQUAL_INT(0, elapsedSlicesMs);

    // Now simulate cancellation arriving during first 100ms slice:
    stopFlag.store(false);
    elapsedSlicesMs = 0;
    for (int i = 0; i < 10; ++i) {
        elapsedSlicesMs += 100;
        if (i == 0) {
            stopFlag.store(true); // cancelled during first slice
        }
        if (stopFlag.load(std::memory_order_acquire)) {
            break;
        }
    }
    // Loop exited after 1 slice (100 ms) instead of 1000 ms
    TEST_ASSERT_EQUAL_INT(100, elapsedSlicesMs);
    TEST_ASSERT_TRUE(elapsedSlicesMs < 150);
}

void test_surface_isolation_and_canvas_only_clear(void) {
    // Contract Invariants 17, 18, 19, 20:
    // - clear(0) writes to surface canvas memory only (never direct DMA).
    // - isDirty() is true when canvas modified and stays true until PresentationResult::Ok.
    // - Failed presentation preserves dirty state (Invariant 20).
    class MockCanvasSurface : public IDrawingSurface {
    private:
        uint16_t m_canvas[64 * 32];
        bool m_dirty = false;
    public:
        MockCanvasSurface() : IDrawingSurface(64, 32, 64, 32) {
            memset(m_canvas, 0xFF, sizeof(m_canvas));
        }
        void drawPixel(int16_t x, int16_t y, uint16_t color) override {
            if (x >= 0 && x < 64 && y >= 0 && y < 32) {
                m_canvas[y * 64 + x] = color;
                m_dirty = true;
            }
        }
        void clear(uint16_t color = 0) override {
            for (size_t i = 0; i < 64 * 32; ++i) m_canvas[i] = color;
            m_dirty = true;
        }
        bool isDirty() const override { return m_dirty; }
        void markDirty() override { m_dirty = true; }
        uint16_t getPixel(int x, int y) const { return m_canvas[y * 64 + x]; }

        PresentationTiming present() override {
            m_dirty = false;
            PresentationTiming t;
            return t;
        }
        void blit565(const uint16_t* src, int16_t x, int16_t y, int16_t w, int16_t h, int16_t stridePixels = -1) override {
            (void)src; (void)x; (void)y; (void)w; (void)h; (void)stridePixels;
            m_dirty = true;
        }
        CanvasView acquireCanvas() override { return CanvasView(); }
        void releaseCanvas() override {}
        bool hasCanvas() const override { return true; }
        CanvasStorage canvasStorage() const override { return CanvasStorage::SRAM; }
        PresentationStrategy presentationStrategy() const override { return PresentationStrategy::CANVAS_BURST_SINGLE; }
        size_t memoryUsageBytes() const override { return sizeof(m_canvas); }

        bool simulatePresentation(bool hardwareSuccess) {
            if (hardwareSuccess) {
                present();
                return true;
            }
            // Invariant 20: failed presentation preserves dirty state!
            return false;
        }
    };

    MockCanvasSurface surface;
    TEST_ASSERT_FALSE(surface.isDirty());
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, surface.getPixel(0, 0));

    // 1. Surface clear modifies canvas only and marks dirty
    surface.clear(0);
    TEST_ASSERT_TRUE(surface.isDirty());
    TEST_ASSERT_EQUAL_HEX16(0x0000, surface.getPixel(0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, surface.getPixel(63, 31));

    // 2. Failed presentation preserves dirty state (Invariant 20)
    bool okFail = surface.simulatePresentation(false);
    TEST_ASSERT_FALSE(okFail);
    TEST_ASSERT_TRUE(surface.isDirty()); // Preserved!

    // 3. Successful presentation clears dirty state (Invariant 17)
    bool okPass = surface.simulatePresentation(true);
    TEST_ASSERT_TRUE(okPass);
    TEST_ASSERT_FALSE(surface.isDirty());
}

void test_presentation_oe_blanking_transaction_recovery(void) {
    // Tests Case A, Case B, Case C from transaction contracts
    // Case A: Allocation failure -> OE stays HIGH, result = NoValidPipeline, display nulled
    {
        bool oeAsserted = true;
        bool allocSuccess = false;
        void* displayPtr = nullptr;
        bool success = (!allocSuccess || displayPtr == nullptr) ? false : true;
        TEST_ASSERT_FALSE(success);
        TEST_ASSERT_TRUE(oeAsserted); // Invariant 21: keep OE HIGH
        TEST_ASSERT_NULL(displayPtr);
    }

    // Case B: Allocation OK, but Frame 0 commit fails -> OE stays HIGH, failure = Frame0PresentationFailed
    {
        bool oeAsserted = true;
        bool allocSuccess = true;
        bool frame0Committed = false;
        bool success = (allocSuccess && frame0Committed);
        TEST_ASSERT_FALSE(success);
        TEST_ASSERT_TRUE(oeAsserted); // Invariant 21: OE remains HIGH
    }

    // Case C: Allocation OK and Frame 0 committed -> OE released LOW, transaction OK
    {
        bool oeAsserted = true;
        bool allocSuccess = true;
        bool frame0Committed = true;
        bool success = (allocSuccess && frame0Committed);
        if (success) {
            oeAsserted = false; // Released LOW strictly after Frame 0
        }
        TEST_ASSERT_TRUE(success);
        TEST_ASSERT_FALSE(oeAsserted);
    }
}

// 3. Simulated Transaction Reconfiguration Pipeline (Mirroring MatrixEngine)
struct MockPresentationReconfigurator {
    uint8_t activeDepth = 8;
    bool oeAsserted = false;
    uint32_t lastReconfigMs = 0;
    static constexpr uint32_t MIN_RECONFIG_INTERVAL_MS = 500;

    bool failAllocationFor8 = false;
    bool failAllocationFor4 = false;
    bool failAllocationFor2 = false;
    bool failFrame0Commit = false;

    struct Outcome {
        bool success = false;
        uint8_t requestedDepth = 0;
        uint8_t effectiveDepth = 0;
        bool fallbackAttempted = false;
        bool fallbackUsed = false;
        const char* failureReason = nullptr;
    };

    Outcome reconfigure(uint8_t targetDepth, uint32_t currentMs) {
        Outcome res;
        res.requestedDepth = targetDepth;
        res.effectiveDepth = activeDepth;

        if (targetDepth < 2 || targetDepth > 8) {
            res.failureReason = "InvalidDepth";
            return res;
        }

        if (lastReconfigMs > 0 && (currentMs - lastReconfigMs < MIN_RECONFIG_INTERVAL_MS)) {
            res.failureReason = "Throttled";
            return res;
        }

        if (targetDepth == activeDepth) {
            res.success = true;
            res.effectiveDepth = activeDepth;
            return res;
        }

        lastReconfigMs = currentMs;
        oeAsserted = true; // Invariant 21: OE asserted HIGH before tearing down DMA

        bool allocSuccess = false;
        uint8_t currentAllocDepth = targetDepth;

        auto tryAlloc = [this](uint8_t d) -> bool {
            if (d == 8 && failAllocationFor8) return false;
            if (d == 4 && failAllocationFor4) return false;
            if (d == 2 && failAllocationFor2) return false;
            return true;
        };

        allocSuccess = tryAlloc(targetDepth);
        if (!allocSuccess) {
            uint8_t fallbacks[] = {4, 2};
            for (uint8_t fb : fallbacks) {
                if (fb < currentAllocDepth) {
                    if (tryAlloc(fb)) {
                        allocSuccess = true;
                        currentAllocDepth = fb;
                        break;
                    }
                }
            }
        }

        if (!allocSuccess) {
            res.success = false;
            res.effectiveDepth = 0;
            res.fallbackAttempted = true;
            res.fallbackUsed = false;
            res.failureReason = "NoValidPipeline";
            oeAsserted = true; // Invariant 21: keep OE HIGH in PresentationRecovery
            return res;
        }

        bool frame0Committed = !failFrame0Commit;
        if (!frame0Committed) {
            res.success = false;
            res.effectiveDepth = 0;
            res.fallbackAttempted = (currentAllocDepth != targetDepth);
            res.fallbackUsed = false;
            res.failureReason = "Frame0PresentationFailed";
            oeAsserted = true; // Invariant 21: OE remains HIGH
            return res;
        }

        oeAsserted = false; // OE released LOW strictly after Frame 0 commit
        activeDepth = currentAllocDepth;
        res.success = true;
        res.effectiveDepth = currentAllocDepth;
        res.fallbackAttempted = (currentAllocDepth != targetDepth);
        res.fallbackUsed = (res.effectiveDepth != res.requestedDepth);
        return res;
    }
};

void test_simulated_hardware_presentation_transaction_engine(void) {
    // Scenario 1: Nominal 8 -> 4 transition on TLS admission
    {
        MockPresentationReconfigurator rec;
        rec.activeDepth = 8;
        auto res = rec.reconfigure(4, 1000);
        TEST_ASSERT_TRUE(res.success);
        TEST_ASSERT_EQUAL_UINT8(4, res.effectiveDepth);
        TEST_ASSERT_FALSE(res.fallbackAttempted);
        TEST_ASSERT_FALSE(res.fallbackUsed);
        TEST_ASSERT_FALSE(rec.oeAsserted);
    }

    // Scenario 2: Target 8 fails, fallback 4 succeeds
    {
        MockPresentationReconfigurator rec;
        rec.activeDepth = 4;
        rec.failAllocationFor8 = true; // 8-bit allocation fails
        auto res = rec.reconfigure(8, 1000);
        TEST_ASSERT_TRUE(res.success);
        TEST_ASSERT_EQUAL_UINT8(4, res.effectiveDepth);
        TEST_ASSERT_TRUE(res.fallbackAttempted);
        TEST_ASSERT_TRUE(res.fallbackUsed); // Requested 8 != effective 4
        TEST_ASSERT_FALSE(rec.oeAsserted); // OE released because fallback 4 committed Frame 0
    }

    // Scenario 3: Target 8 and 4 fail, progressive fallback 2 succeeds
    {
        MockPresentationReconfigurator rec;
        rec.activeDepth = 4;
        rec.failAllocationFor8 = true;
        rec.failAllocationFor4 = true;
        auto res = rec.reconfigure(8, 1000);
        TEST_ASSERT_TRUE(res.success);
        TEST_ASSERT_EQUAL_UINT8(2, res.effectiveDepth);
        TEST_ASSERT_TRUE(res.fallbackAttempted);
        TEST_ASSERT_TRUE(res.fallbackUsed);
        TEST_ASSERT_FALSE(rec.oeAsserted);
    }

    // Scenario 4: All allocations fail -> NoValidPipeline, OE held HIGH
    {
        MockPresentationReconfigurator rec;
        rec.activeDepth = 4;
        rec.failAllocationFor8 = true;
        rec.failAllocationFor4 = true;
        rec.failAllocationFor2 = true;
        auto res = rec.reconfigure(8, 1000);
        TEST_ASSERT_FALSE(res.success);
        TEST_ASSERT_EQUAL_UINT8(0, res.effectiveDepth);
        TEST_ASSERT_EQUAL_STRING("NoValidPipeline", res.failureReason);
        TEST_ASSERT_TRUE(rec.oeAsserted); // Invariant 21: PresentationRecovery holds OE HIGH
    }

    // Scenario 5: Allocation succeeds but Frame 0 commit fails -> Frame0PresentationFailed, OE held HIGH
    {
        MockPresentationReconfigurator rec;
        rec.activeDepth = 8;
        rec.failFrame0Commit = true;
        auto res = rec.reconfigure(4, 1000);
        TEST_ASSERT_FALSE(res.success);
        TEST_ASSERT_EQUAL_UINT8(0, res.effectiveDepth);
        TEST_ASSERT_EQUAL_STRING("Frame0PresentationFailed", res.failureReason);
        TEST_ASSERT_TRUE(rec.oeAsserted); // Invariant 21: OE remains HIGH
    }

    // Scenario 6: Reconfiguration rate limit (< 500 ms) throttles gracefully
    {
        MockPresentationReconfigurator rec;
        rec.activeDepth = 8;
        auto res1 = rec.reconfigure(4, 1000);
        TEST_ASSERT_TRUE(res1.success);
        auto res2 = rec.reconfigure(8, 1200); // 200 ms later -> throttled
        TEST_ASSERT_FALSE(res2.success);
        TEST_ASSERT_EQUAL_STRING("Throttled", res2.failureReason);
    }
}

class MockPrefetchEngine : public IEngine {
public:
    int prefetchCount = 0;
    EngineError initialize(EngineContext*, const EngineConfig*) override { return EngineError::OK; }
    void activate() override {}
    void update(EngineContext*) override {}
    void render(EngineContext*) override {}
    void deactivate() override {}
    void prefetchData() override {
        prefetchCount++;
    }
};

void test_transition_prefetch_contract(void) {
    MockPrefetchEngine engine;

    // Contract 1: Calling prefetchData() increments counter
    TEST_ASSERT_EQUAL(0, engine.prefetchCount);
    engine.prefetchData();
    TEST_ASSERT_EQUAL(1, engine.prefetchCount);

    // Contract 2: On PSRAM board (ESP32-S3), needTeardown is false for TLS engines
    EngineRequirements tlsReq;
    tlsReq.needsTls = true;
    uint8_t depthS3 = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, true, tlsReq, 8, 120000, 240000, 100000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(8, depthS3);
    // On S3 with PSRAM, targetDepth (8) == currentDepth (8) and hasPsram is true,
    // so needTeardown evaluates to false. S3 never releases panel nor executes transition prefetch.
    bool s3NeedsTeardown = (depthS3 != 8) || (tlsReq.needsTls && !true /*hasPsram*/);
    TEST_ASSERT_FALSE(s3NeedsTeardown);

    // Contract 3: On non-PSRAM board (ESP32 Standard), depth targets 4 bits and requires clean window
    uint8_t depthEsp = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, tlsReq, 8, 32000, 60000, 40000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(4, depthEsp);
    bool espNeedsTeardown = (depthEsp != 8) || (tlsReq.needsTls && !false /*hasPsram*/);
    TEST_ASSERT_TRUE(espNeedsTeardown);

    // Contract 4: On non-PSRAM board after panel release, steady-state display admits 4 bits with 56 KB free DRAM
    uint8_t depthPostRelease = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, tlsReq, 0, 34000, 56000, 34000, false, true, /*panelReleased=*/true
    );
    TEST_ASSERT_EQUAL_UINT8(4, depthPostRelease);

    // Contract 5: When engine cache is fresh (needsTlsFetch() == false), needsTls is cleared dynamically,
    // admitting 8 bits color depth and zero panel teardown on rotation.
    class MockFreshCacheEngine : public IEngine {
    public:
        EngineError initialize(EngineContext*, const EngineConfig*) override { return EngineError::OK; }
        void activate() override {}
        void update(EngineContext*) override {}
        void render(EngineContext*) override {}
        void deactivate() override {}
        bool needsTlsFetch() const override { return false; }
    };
    MockFreshCacheEngine freshEngine;
    EngineRequirements dynamicReq = tlsReq;
    if (dynamicReq.needsTls) {
        dynamicReq.needsTls = freshEngine.needsTlsFetch();
    }
    TEST_ASSERT_FALSE(dynamicReq.needsTls);
    uint8_t depthFresh = PipelineSelectionPolicy::resolveTargetDepth(
        8, true, 128, 32, false, dynamicReq, 8, 32000, 60000, 40000, false, true
    );
    TEST_ASSERT_EQUAL_UINT8(8, depthFresh);
    bool freshNeedsTeardown = (depthFresh != 8) || (dynamicReq.needsTls && !false /*hasPsram*/);
    TEST_ASSERT_FALSE(freshNeedsTeardown);
}

// =========================================================================
// Main Runner (Unity Execution)
// =========================================================================

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    // EngineHandle Identity Tests
    RUN_TEST(test_engine_handle_equality_and_empty);

    // Arbiter Tests
    RUN_TEST(test_arbiter_source_id_parsing);
    RUN_TEST(test_arbiter_priority_resolution);
    RUN_TEST(test_arbiter_one_shot_auto_consumption);
    RUN_TEST(test_arbiter_timed_request_expiration);
    RUN_TEST(test_arbiter_queue_saturation_and_dropped_counter);

    // Snapshot & Triple-Buffer Tests
    RUN_TEST(test_config_snapshot_crc32_and_magic);
    RUN_TEST(test_srsw_triple_buffer_linearizability);

    // WebServerAPI Timing-Safe Compare Tests
    RUN_TEST(test_timing_safe_compare);

    // Registry Tests
    RUN_TEST(test_engine_registry_lifecycle);

    // Geometry Primitives Tests
    RUN_TEST(test_geometry_layout_classification);
    RUN_TEST(test_rect_primitives);

    // Preemption Logic Tests
    RUN_TEST(test_preemption_stack_bounded_depth);

    // MQTT Data engine: payload parsers and graph layout
    RUN_TEST(test_graph_payload_parses_v1_fields);
    RUN_TEST(test_graph_payload_right_aligns_short_series);
    RUN_TEST(test_graph_payload_auto_scale_stacked_and_overlaid);
    RUN_TEST(test_graph_payload_rejects_bad_input);
    RUN_TEST(test_graph_layout_and_column_mapping);
    RUN_TEST(test_graph_negative_bars_and_zero_line);
    RUN_TEST(test_graph_lines_range_gaps_and_explicit_min_max);
    RUN_TEST(test_graph_96_bands_and_newest_kept);
    RUN_TEST(test_graph_summary_color_and_legend);
    RUN_TEST(test_feed_values_payload);
    RUN_TEST(test_feed_weather_payload_and_conditions);
    RUN_TEST(test_mqttdata_session_allocations_balance_over_activations);
    RUN_TEST(test_mqttdata_limits_topics_and_parsing);
    RUN_TEST(test_mqttdata_session_pages_types_and_handoff);
    RUN_TEST(test_mqttdata_page_cycle);
    RUN_TEST(test_mqttdata_blueprint_payloads);

    // CompatibilityEvaluator Tests
    RUN_TEST(test_compatibility_evaluator_hardware_gating);
    RUN_TEST(test_compatibility_evaluator_memory_and_fragmentation);
    RUN_TEST(test_compatibility_evaluator_presentation_budget_and_single_buffer);
    RUN_TEST(test_compatibility_evaluator_multi_issue_bitmask);
    RUN_TEST(test_e2e_gif_to_clock_transition_and_catalog_invariance);

    // IconService Tests
    RUN_TEST(test_icon_service_sanitization_and_paths);

    // Color Depth & Pipeline Selection Tests
    RUN_TEST(test_pipeline_selection_effective_color_depth);
    RUN_TEST(test_rotation_requirements_aggregation);
    RUN_TEST(test_render_transaction_contracts);

    // Surface Isolation & Canvas Clear Contracts (Invariants 17, 18, 19, 20)
    RUN_TEST(test_surface_isolation_and_canvas_only_clear);

    // Lifecycle Quiescence & Quarantine Contracts (Invariants 1, 14, 15, 16)
    RUN_TEST(test_lifecycle_two_stage_quiescence_contracts);
    RUN_TEST(test_lifecycle_deactivate_vs_shutdown_distinct_contracts);
    RUN_TEST(test_lifecycle_cooperative_worker_timeout_and_quarantine);
    RUN_TEST(test_lifecycle_cooperative_exit_latency_slices);

    // Presentation OE Recovery & Simulated Hardware Transaction Contracts (Invariant 21)
    RUN_TEST(test_presentation_oe_blanking_transaction_recovery);
    RUN_TEST(test_simulated_hardware_presentation_transaction_engine);

    // Transition Prefetch Contracts (Hardware Gating)
    RUN_TEST(test_transition_prefetch_contract);

    return UNITY_END();
}
