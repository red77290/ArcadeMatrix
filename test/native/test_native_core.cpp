#include <unity.h>
#include "Arduino.h"
#include "core/DisplayArbiter.h"
#include "core/EngineRegistry.h"
#include "core/TimingSafe.h"
#include "core/ConfigLoader.h"
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

    return UNITY_END();
}
