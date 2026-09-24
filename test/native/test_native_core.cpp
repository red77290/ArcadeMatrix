#include <unity.h>
#include "Arduino.h"
#include "core/DisplayArbiter.h"
#include "core/EngineRegistry.h"
#include "core/TimingSafe.h"
#include "core/ConfigLoader.h"
#include "../../include/core/EngineContract.h"

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

    return UNITY_END();
}
