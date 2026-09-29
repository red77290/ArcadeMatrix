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

    // CompatibilityEvaluator Tests
    RUN_TEST(test_compatibility_evaluator_hardware_gating);
    RUN_TEST(test_compatibility_evaluator_memory_and_fragmentation);
    RUN_TEST(test_compatibility_evaluator_presentation_budget_and_single_buffer);
    RUN_TEST(test_compatibility_evaluator_multi_issue_bitmask);
    RUN_TEST(test_e2e_gif_to_clock_transition_and_catalog_invariance);

    return UNITY_END();
}
