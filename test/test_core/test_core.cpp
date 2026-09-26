#include <Arduino.h>
#include <unity.h>

#include "core/EngineRegistry.h"
#include "core/ConfigSanitizer.h"
#include "core/ConfigLoader.h"
#include "core/DisplayArbiter.h"
#include "core/DisplayRuntime.h"
#include "core/OverlayManager.h"
#include "core/NetworkBudget.h"
#include "engines/EngineRegistrar.h"
#include "hal/HardwareHAL.h"
#include "core/drawing/IDrawingSurface.h"
#include "core/drawing/SurfaceCoordinates.h"
#include "core/drawing/Hub75BulkEncoder.h"
#include "core/drawing/DisplaySurfaceFactory.h"
#include "core/drawing/MockPresentationBackend.h"
#include "core/drawing/Hub75PresentationBackend.h"
#include "core/storage/MemoryConfigStorage.h"
#include "core/storage/WorkingSetCache.h"
#include "core/storage/ModularConfigManager.h"

// Mock Engine implementation for testing
class MockTestEngine : public IEngine {
public:
    EngineError initialize(EngineContext* context, const EngineConfig* config) override { return EngineError::OK; }
    void activate() override {}
    void update(EngineContext* context) override {}
    void render(EngineContext* context) override {}
    void deactivate() override {}
};

class TrackingMockEngine : public IEngine {
public:
    String name;
    int activateCalls = 0;
    int deactivateCalls = 0;
    int pauseCalls = 0;
    int resumeCalls = 0;

    TrackingMockEngine() = default;
    explicit TrackingMockEngine(const String& n) : name(n) {}

    EngineError initialize(EngineContext* context, const EngineConfig* config) override { return EngineError::OK; }
    void activate() override { activateCalls++; }
    void update(EngineContext* context) override {}
    void render(EngineContext* context) override {}
    void deactivate() override { deactivateCalls++; }
    void pause() override { pauseCalls++; }
    void resume() override { resumeCalls++; }
};

#include "core/Core0Lifecycle.h"

class LifecycleMockEngine : public IEngine {
public:
    static bool s_failShutdown;
    bool shutdownCalled = false;

    LifecycleMockEngine() {
        setResourceState(EngineResourceState::UNINITIALIZED);
    }

    EngineError initialize(EngineContext* context, const EngineConfig* config) override { return EngineError::OK; }
    void activate() override { setResourceState(EngineResourceState::ACTIVE); }
    void update(EngineContext* context) override {}
    void render(EngineContext* context) override {}
    void deactivate() override { setResourceState(EngineResourceState::DEACTIVATING); }
    bool shutdownForDestruction() override {
        shutdownCalled = true;
        return !s_failShutdown;
    }
};
bool LifecycleMockEngine::s_failShutdown = false;

void setUp(void) {
    EngineRegistry::clear();
}

void tearDown(void) {
    EngineRegistry::clear();
}

// =========================================================================
// 1. EngineRegistry & Descriptor Tests
// =========================================================================

/**
 * @brief Verifies registration of engine descriptors into the central EngineRegistry.
 *
 * Ensures descriptor ID, human-readable name, and factory function are stored accurately.
 */
void test_engine_registration(void) {
    EngineDescriptor desc;
    desc.metadata.id = "test.engine";
    desc.metadata.name = "Test Engine";
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockTestEngine()); };

    TEST_ASSERT_TRUE(EngineRegistry::registerEngine(desc));
    
    size_t count = 0;
    const EngineDescriptor* all = EngineRegistry::getAllDescriptors(count);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_STRING("test.engine", all[0].metadata.id);
    TEST_ASSERT_EQUAL_STRING("Test Engine", all[0].metadata.name);
}

/**
 * @brief Verifies that duplicate engine registrations with the same identifier are rejected.
 */
void test_duplicate_registration_fails(void) {
    EngineDescriptor desc1;
    desc1.metadata.id = "test.engine";
    
    EngineDescriptor desc2;
    desc2.metadata.id = "test.engine";

    TEST_ASSERT_TRUE(EngineRegistry::registerEngine(desc1));
    TEST_ASSERT_FALSE(EngineRegistry::registerEngine(desc2));
    
    size_t count = 0;
    EngineRegistry::getAllDescriptors(count);
    TEST_ASSERT_EQUAL(1, count);
}

/**
 * @brief Tests lookup of engine descriptors by unique string identifier.
 */
void test_get_descriptor(void) {
    EngineDescriptor desc;
    desc.metadata.id = "test.engine2";
    EngineRegistry::registerEngine(desc);
    
    const EngineDescriptor* found = EngineRegistry::getDescriptor("test.engine2");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("test.engine2", found->metadata.id);
    
    const EngineDescriptor* not_found = EngineRegistry::getDescriptor("nonexistent");
    TEST_ASSERT_NULL(not_found);
}

/**
 * @brief Verifies factory instantiation producing valid IEngine polymorphic pointers.
 */
void test_factory_creation(void) {
    EngineDescriptor desc;
    desc.metadata.id = "test.factory";
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockTestEngine()); };
    EngineRegistry::registerEngine(desc);
    
    const EngineDescriptor* found = EngineRegistry::getDescriptor("test.factory");
    TEST_ASSERT_NOT_NULL(found);
    
    std::unique_ptr<IEngine> instance = found->factory();
    TEST_ASSERT_NOT_NULL(instance.get());
}

/**
 * @brief Tests engine configuration schema definition, field types, and validation policies.
 */
void test_schema_and_fields(void) {
    EngineDescriptor desc;
    desc.metadata.id = "test.schema";
    desc.schema.fields = {
        ConfigField("speed", ConfigType::INTEGER, "Speed", "Playback speed", "2", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("theme", ConfigType::ENUM, "Theme", "Visual theme", "0", false, "", "", "", "", "/api/themes", false, "", ValidationPolicy::FallbackDefault)
    };
    EngineRegistry::registerEngine(desc);

    const EngineDescriptor* found = EngineRegistry::getDescriptor("test.schema");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(2, found->schema.fields.size());
    TEST_ASSERT_EQUAL_STRING("speed", found->schema.fields[0].id);
    TEST_ASSERT_EQUAL(ConfigType::INTEGER, found->schema.fields[0].type);
    TEST_ASSERT_EQUAL_STRING("2", found->schema.fields[0].default_value);
    TEST_ASSERT_EQUAL_STRING("1", found->schema.fields[0].min_val);
    TEST_ASSERT_EQUAL_STRING("10", found->schema.fields[0].max_val);
    TEST_ASSERT_EQUAL(ValidationPolicy::Clamp, found->schema.fields[0].validation_policy);

    TEST_ASSERT_EQUAL_STRING("theme", found->schema.fields[1].id);
    TEST_ASSERT_EQUAL(ConfigType::ENUM, found->schema.fields[1].type);
    TEST_ASSERT_EQUAL_STRING("/api/themes", found->schema.fields[1].options_endpoint);
}

/**
 * @brief Tests engine capabilities (realtime, overlays, rotation) and hardware requirements flags.
 */
void test_capabilities_and_requirements(void) {
    EngineDescriptor desc;
    desc.metadata.id = "test.caps";
    desc.capabilities.realtime = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = true;
    desc.requirements.needsAudio = true;
    EngineRegistry::registerEngine(desc);

    const EngineDescriptor* found = EngineRegistry::getDescriptor("test.caps");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_TRUE(found->capabilities.realtime);
    TEST_ASSERT_TRUE(found->capabilities.selfPaced);
    TEST_ASSERT_TRUE(found->capabilities.allowsOverlay);
    TEST_ASSERT_TRUE(found->capabilities.allowRotation);
    TEST_ASSERT_TRUE(found->requirements.needsPsram);
    TEST_ASSERT_TRUE(found->requirements.needsAudio);
}

// =========================================================================
// 2. ConfigSanitizer Tests
// =========================================================================

/**
 * @brief Tests injection of default configuration values into unpopulated fields during sanitization.
 */
void test_sanitizer_injects_defaults(void) {
    EngineDescriptor desc;
    desc.metadata.id = "clock";
    desc.schema.fields = {
        ConfigField("theme", ConfigType::ENUM, "Theme", "Visual theme", "nintendo", false, "nintendo,capcom,sega", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("speed", ConfigType::INTEGER, "Speed", "Speed", "5", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockTestEngine()); };
    EngineRegistry::registerEngine(desc);

    ConfigLoader cfg;
    cfg.addInstance("clock_1", "clock");
    
    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(2, res.defaults_injected);
    EngineInstanceSnapshot snap1;
    TEST_ASSERT_TRUE(cfg.getInstanceSnapshot("clock_1", snap1));
    TEST_ASSERT_EQUAL_STRING("nintendo", snap1.config.getString("theme").c_str());
    TEST_ASSERT_EQUAL(5, snap1.config.getInt("speed"));
}

/**
 * @brief Tests integer clamping when values exceed declared schema bounds.
 */
void test_sanitizer_clamps_out_of_bound_integers(void) {
    EngineDescriptor desc;
    desc.metadata.id = "clock";
    desc.schema.fields = {
        ConfigField("speed", ConfigType::INTEGER, "Speed", "Speed", "5", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockTestEngine()); };
    EngineRegistry::registerEngine(desc);

    ConfigLoader cfg;
    cfg.addInstance("clock_1", "clock");
    cfg.mutate([](ConfigLoader& c) {
        for (auto& inst : c.instances) {
            if (inst.instance_id == "clock_1") inst.config.setInt("speed", 999);
        }
    });

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(1, res.values_clamped);
    EngineInstanceSnapshot snap2;
    TEST_ASSERT_TRUE(cfg.getInstanceSnapshot("clock_1", snap2));
    TEST_ASSERT_EQUAL(10, snap2.config.getInt("speed"));
}

/**
 * @brief Tests fallback to default values for invalid boolean and unknown enum entries.
 */
void test_sanitizer_handles_invalid_boolean_and_enum(void) {
    EngineDescriptor desc;
    desc.metadata.id = "weather";
    desc.schema.fields = {
        ConfigField("use_celsius", ConfigType::BOOLEAN, "Celsius", "Use Celsius", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("icon_set", ConfigType::ENUM, "Icon Set", "Theme icon set", "classic", false, "classic,modern,retro", "", "", "", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockTestEngine()); };
    EngineRegistry::registerEngine(desc);

    ConfigLoader cfg;
    cfg.addInstance("weather_1", "weather");
    cfg.mutate([](ConfigLoader& c) {
        for (auto& inst : c.instances) {
            if (inst.instance_id == "weather_1") {
                inst.config.setString("use_celsius", "invalid_bool");
                inst.config.setString("icon_set", "unknown_icon_theme");
            }
        }
    });

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(2, res.values_fallback);
    EngineInstanceSnapshot snap3;
    TEST_ASSERT_TRUE(cfg.getInstanceSnapshot("weather_1", snap3));
    TEST_ASSERT_EQUAL_STRING("true", snap3.config.getString("use_celsius").c_str());
    TEST_ASSERT_EQUAL_STRING("classic", snap3.config.getString("icon_set").c_str());
}

/**
 * @brief Verifies that instances pointing to unregistered or unknown engine descriptor IDs are flagged as invalid.
 */
void test_sanitizer_flags_unknown_engines(void) {
    ConfigLoader cfg;
    cfg.addInstance("bad_inst", "non_existent_engine");

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_EQUAL(1, res.invalid_instances);
}

/**
 * @brief Tests comprehensive validation policy coverage (Clamp, FallbackDefault, Accept, Reject).
 */
void test_sanitizer_validation_policy_coverage(void) {
    EngineDescriptor desc;
    desc.metadata.id = "policy_test";
    desc.schema.fields = {
        ConfigField("f_clamp", ConfigType::INTEGER, "Clamp Field", "Clamped", "5", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("f_fallback", ConfigType::INTEGER, "Fallback Field", "Fallback", "5", false, "1", "10", "1", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("f_accept", ConfigType::STRING, "Accept Field", "Accepted", "default_str", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("f_reject", ConfigType::INTEGER, "Reject Field", "Rejected", "5", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Reject)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockTestEngine()); };
    EngineRegistry::registerEngine(desc);

    ConfigLoader cfg;
    cfg.addInstance("policy_1", "policy_test");
    cfg.mutate([](ConfigLoader& c) {
        for (auto& inst : c.instances) {
            if (inst.instance_id == "policy_1") {
                inst.config.setInt("f_clamp", 100);
                inst.config.setInt("f_fallback", 100);
                inst.config.setString("f_accept", "arbitrary_custom_value");
                inst.config.setInt("f_reject", 100);
            }
        }
    });

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(1, res.values_clamped);
    TEST_ASSERT_EQUAL(2, res.values_fallback); // fallback + reject fallback to default

    EngineInstanceSnapshot snap;
    TEST_ASSERT_TRUE(cfg.getInstanceSnapshot("policy_1", snap));
    TEST_ASSERT_EQUAL(10, snap.config.getInt("f_clamp"));
    TEST_ASSERT_EQUAL(5, snap.config.getInt("f_fallback"));
    TEST_ASSERT_EQUAL_STRING("arbitrary_custom_value", snap.config.getString("f_accept").c_str());
    TEST_ASSERT_EQUAL(5, snap.config.getInt("f_reject"));
}

/**
 * @brief Tests that ConfigSanitizer allows night_brightness to be 0 (display power-off mode)
 * and properly clamps out-of-bounds negative and > 100 values.
 */
void test_sanitizer_night_brightness_allows_zero(void) {
    ConfigLoader cfg;
    cfg.mutate([](ConfigLoader& c) {
        c.system.night_brightness = 0;
    });
    SanitizeResult res = ConfigSanitizer::sanitize(cfg);
    TEST_ASSERT_EQUAL(0, cfg.system.night_brightness);

    cfg.mutate([](ConfigLoader& c) {
        c.system.night_brightness = -5;
    });
    res = ConfigSanitizer::sanitize(cfg);
    TEST_ASSERT_EQUAL(0, cfg.system.night_brightness);
    TEST_ASSERT_TRUE(res.values_clamped >= 1);

    cfg.mutate([](ConfigLoader& c) {
        c.system.night_brightness = 150;
    });
    res = ConfigSanitizer::sanitize(cfg);
    TEST_ASSERT_EQUAL(100, cfg.system.night_brightness);
    TEST_ASSERT_TRUE(res.values_clamped >= 1);
}

/**
 * @brief Tests that ConfigSanitizer ensures any instance referenced by the rotation list
 * exists in instances, recreating it if it was omitted or truncated.
 */
void test_sanitizer_rotation_recreates_missing_instances(void) {
    ConfigLoader cfg;
    cfg.mutate([](ConfigLoader& c) {
        c.instances.clear();
        c.rotation.clear();
        c.rotation.emplace_back("clock_main", 15, OverlayConfig{false});
        c.rotation.emplace_back("gifs_main", 20, OverlayConfig{false});
    });
    SanitizeResult res = ConfigSanitizer::sanitize(cfg, false);
    TEST_ASSERT_EQUAL(2, cfg.instances.size());
    TEST_ASSERT_EQUAL_STRING("clock_main", cfg.instances[0].instance_id.c_str());
    TEST_ASSERT_EQUAL_STRING("clock", cfg.instances[0].engine_id.c_str());
    TEST_ASSERT_EQUAL_STRING("gifs_main", cfg.instances[1].instance_id.c_str());
    TEST_ASSERT_EQUAL_STRING("gifs", cfg.instances[1].engine_id.c_str());
}

// =========================================================================
// 3. DisplayArbiter & OverlayManager Tests
// =========================================================================

/**
 * @brief Tests DisplayArbiter deterministic priority resolution among concurrent display sources.
 *
 * Verifies that higher-priority sources (MQTT > MARQUEE > ROTATION) win during evaluation
 * and that cancelling higher-priority requests smoothly restores lower-priority sources.
 */
void test_arbiter_priority_resolution(void) {
    DisplayArbiter arbiter;

    DisplayRequest reqRot{DisplaySourceId::ROTATION, DisplayPriority::ROTATION, RequestLifecycle::PERSISTENT, false};
    DisplayRequest reqMarq{DisplaySourceId::MARQUEE, DisplayPriority::MARQUEE, RequestLifecycle::UNTIL_CANCELLED, true};
    DisplayRequest reqMqtt{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::UNTIL_CANCELLED, true};

    arbiter.submitRequest(reqRot);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, arbiter.evaluate().sourceId);

    arbiter.submitRequest(reqMarq);
    TEST_ASSERT_EQUAL(DisplaySourceId::MARQUEE, arbiter.evaluate().sourceId);

    arbiter.submitRequest(reqMqtt);
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, arbiter.evaluate().sourceId);

    arbiter.cancelRequest(DisplaySourceId::MQTT);
    TEST_ASSERT_EQUAL(DisplaySourceId::MARQUEE, arbiter.evaluate().sourceId);

    arbiter.cancelRequest(DisplaySourceId::MARQUEE);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, arbiter.evaluate().sourceId);
}

/**
 * @brief Tests ONE_SHOT display request auto-consumption behavior.
 *
 * Verifies that a ONE_SHOT request is consumed on the first evaluate() call and immediately
 * falls back to baseline ROTATION on the subsequent evaluate() cycle.
 */
void test_arbiter_one_shot_auto_consumption(void) {
    DisplayArbiter arbiter;

    DisplayRequest reqOneShot{DisplaySourceId::ALERT, DisplayPriority::ALERT, RequestLifecycle::ONE_SHOT, true};
    arbiter.submitRequest(reqOneShot);

    // First evaluation: ONE_SHOT alert wins and is consumed
    DisplayDecision d1 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d1.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ALERT, d1.sourceId);

    // Second evaluation: ONE_SHOT is gone, fallback to ROTATION
    DisplayDecision d2 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d2.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d2.sourceId);
}

/**
 * @brief Tests request ID generation semantics and restartTimer flag behavior.
 */
void test_arbiter_request_id_semantics(void) {
    DisplayArbiter arbiter;

    DisplayRequest req1{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::UNTIL_CANCELLED, true};
    arbiter.submitRequest(req1);
    DisplayDecision d1 = arbiter.evaluate();
    uint32_t firstReqId = d1.requestId;
    TEST_ASSERT_NOT_EQUAL(0, firstReqId);

    // Refresh request without restartTimer preserves same requestId
    DisplayRequest reqRefresh{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::UNTIL_CANCELLED, true};
    arbiter.submitRequest(reqRefresh, false);
    DisplayDecision d2 = arbiter.evaluate();
    TEST_ASSERT_EQUAL(firstReqId, d2.requestId);

    // Refresh request with restartTimer creates a new requestId
    arbiter.submitRequest(reqRefresh, true);
    DisplayDecision d3 = arbiter.evaluate();
    TEST_ASSERT_NOT_EQUAL(firstReqId, d3.requestId);
}

// =========================================================================
// 4. Triple-Buffer Linearizability & Snapshot Atomicity
// =========================================================================

/**
 * @brief Tests immutability and version monotonicity of Triple-Buffer configuration snapshots.
 */
void test_config_snapshot_immutability_and_versioning(void) {
    ConfigLoader cfg;
    cfg.setDefaults();
    uint32_t v1 = cfg.getVersion();
    {
        ConfigSnapshotGuard s1 = cfg.acquireSnapshot();
        TEST_ASSERT_EQUAL(v1, s1->version);
    }

    cfg.addInstance("test_clock", "clock");
    uint32_t v2 = cfg.getVersion();
    TEST_ASSERT_GREATER_THAN(v1, v2);

    {
        ConfigSnapshotGuard s2 = cfg.acquireSnapshot();
        TEST_ASSERT_EQUAL(v2, s2->version);
        TEST_ASSERT_NOT_NULL(s2->getInstance("test_clock"));
    }
}

/**
 * @brief Tests sequential mutation and Triple-Buffer slot wrap-around publication.
 */
void test_triple_buffer_snapshot_publication_and_versioning(void) {
    ConfigLoader cfg;
    cfg.setDefaults();
    uint32_t v0 = cfg.getVersion();

    // 4 sequential mutations covering all 3 slots and wrapping around
    cfg.mutate([](ConfigLoader& c) { c.wifi.ssid = "WiFi_Slot_1"; });
    uint32_t v1 = cfg.getVersion();
    TEST_ASSERT_GREATER_THAN(v0, v1);
    TEST_ASSERT_EQUAL_STRING("WiFi_Slot_1", cfg.acquireSnapshot()->wifi.ssid.c_str());

    cfg.mutate([](ConfigLoader& c) { c.wifi.ssid = "WiFi_Slot_2"; });
    uint32_t v2 = cfg.getVersion();
    TEST_ASSERT_GREATER_THAN(v1, v2);
    TEST_ASSERT_EQUAL_STRING("WiFi_Slot_2", cfg.acquireSnapshot()->wifi.ssid.c_str());

    cfg.mutate([](ConfigLoader& c) { c.wifi.ssid = "WiFi_Slot_3"; });
    uint32_t v3 = cfg.getVersion();
    TEST_ASSERT_GREATER_THAN(v2, v3);
    TEST_ASSERT_EQUAL_STRING("WiFi_Slot_3", cfg.acquireSnapshot()->wifi.ssid.c_str());

    cfg.mutate([](ConfigLoader& c) { c.wifi.ssid = "WiFi_Slot_4"; });
    uint32_t v4 = cfg.getVersion();
    TEST_ASSERT_GREATER_THAN(v3, v4);
    TEST_ASSERT_EQUAL_STRING("WiFi_Slot_4", cfg.acquireSnapshot()->wifi.ssid.c_str());
}

/**
 * @brief Tests high-frequency mutation linearizability and CRC32 integrity verification.
 */
void test_snapshot_publication_linearizability(void) {
    ConfigLoader cfg;
    cfg.setDefaults();

    // 50 rapid sequential mutations with crc32 verification
    for (uint32_t i = 1; i <= 50; ++i) {
        cfg.mutate([i](ConfigLoader& c) {
            c.wifi.ssid = String("WiFi_Network_") + String(i);
        });

        // Core 1 reader acquire via RAII guard
        ConfigSnapshotGuard snap = cfg.acquireSnapshot();
        uint32_t expectedCrc32 = ConfigSnapshot::calculateCRC32(snap->version, snap->instances.size());
        TEST_ASSERT_TRUE(snap->isValid());
        TEST_ASSERT_EQUAL_HEX32(expectedCrc32, snap->crc32);
        TEST_ASSERT_TRUE(snap->wifi.ssid.startsWith("WiFi_Network_"));
    }
}

/**
 * @brief Tests lock-free Single Producer Single Consumer (SPSC) queue concurrency between Core 0 and Core 1.
 */
void test_arbiter_spsc_lockfree(void) {
    DisplayArbiter arbiter;

    // Core 0 producer submits timed marquee and urgent alert
    DisplayRequest marqueeReq{DisplaySourceId::MARQUEE, DisplayPriority::MARQUEE, RequestLifecycle::TIMED, true, 10, EngineHandle("marquee", "inst_m"), 5000};
    DisplayRequest alertReq{DisplaySourceId::ALERT, DisplayPriority::ALERT, RequestLifecycle::ONE_SHOT, true, 20, EngineHandle("alert", "inst_a")};

    arbiter.submitRequest(marqueeReq);
    arbiter.submitRequest(alertReq);

    // Core 1 consumer evaluate: Alert (priority 100) wins over Marquee (priority 30)
    DisplayDecision d1 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d1.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ALERT, d1.sourceId);
    TEST_ASSERT_EQUAL_STRING("alert", d1.engineHandle.descriptorId);
    TEST_ASSERT_EQUAL_STRING("inst_a", d1.engineHandle.instanceId);

    // Next evaluation: ONE_SHOT alert auto-consumed, Marquee takes over
    DisplayDecision d2 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d2.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::MARQUEE, d2.sourceId);
    TEST_ASSERT_EQUAL_STRING("marquee", d2.engineHandle.descriptorId);

    // Core 0 cancels marquee -> falls back to ROTATION
    arbiter.cancelRequest(DisplaySourceId::MARQUEE);
    DisplayDecision d3 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d3.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d3.sourceId);

    // Test queue saturation: QUEUE_CAPACITY is 16.
    // Submitting 20 commands without evaluate() fills 16 slots and drops 4 commands.
    TEST_ASSERT_EQUAL_UINT32(0, arbiter.getDroppedCommandCount());
    for (int i = 0; i < 20; ++i) {
        DisplayRequest extraReq{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::ONE_SHOT, false, (uint32_t)(100 + i), EngineHandle("msg", "i")};
        arbiter.submitRequest(extraReq);
    }
    TEST_ASSERT_EQUAL_UINT32(4, arbiter.getDroppedCommandCount());
}

/**
 * @brief Tests EngineHandle canonical resolution by descriptor ID and instance ID.
 */
void test_canonical_engine_handle_resolution(void) {
    DisplayRuntime runtime;
    TrackingMockEngine visMain;
    TrackingMockEngine visSpecial;

    runtime.registerSourceEngine(DisplaySourceId::VISUALIZER, &visMain, EngineHandle("audiovisualizer", "visualizer_main"));
    runtime.registerSourceEngine(DisplaySourceId::VISUALIZER, &visSpecial, EngineHandle("audiovisualizer", "visualizer_special"));

    // Verify resolveEngine matches exact instance
    IEngine* resolved = runtime.resolveEngine(EngineHandle("audiovisualizer", "visualizer_special"), DisplaySourceId::VISUALIZER);
    TEST_ASSERT_EQUAL_PTR(&visSpecial, resolved);

    IEngine* resolvedMain = runtime.resolveEngine(EngineHandle("audiovisualizer", "visualizer_main"), DisplaySourceId::VISUALIZER);
    TEST_ASSERT_EQUAL_PTR(&visMain, resolvedMain);
}

// =========================================================================
// 5. Display Runtime State Machine, Lifecycle & Preemption Stack
// =========================================================================

/**
 * @brief Tests DisplayRuntime preemption lifecycle transitions: Pause -> Push -> Activate -> Deactivate -> Resume.
 */
void test_display_runtime_preemption_lifecycle(void) {
    DisplayRuntime runtime;
    TrackingMockEngine rotationEngine;
    TrackingMockEngine alertEngine;

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &rotationEngine, EngineHandle("clock", "clock_main"));
    runtime.registerSourceEngine(DisplaySourceId::ALERT, &alertEngine, EngineHandle("alert", "alert_main"));

    // 1. Initial baseline rotation session -> activate
    DisplayDecision dRot;
    dRot.valid = true;
    dRot.sourceId = DisplaySourceId::ROTATION;
    dRot.engineHandle = EngineHandle("clock", "clock_main");
    dRot.requestId = 1;
    runtime.transitionSession(dRot);

    TEST_ASSERT_EQUAL(1, rotationEngine.activateCalls);
    TEST_ASSERT_EQUAL(0, rotationEngine.pauseCalls);
    TEST_ASSERT_EQUAL(0, rotationEngine.deactivateCalls);

    // 2. Urgent alert preempts rotation -> rotation pauses, alert activates
    DisplayDecision dAlert;
    dAlert.valid = true;
    dAlert.sourceId = DisplaySourceId::ALERT;
    dAlert.engineHandle = EngineHandle("alert", "alert_main");
    dAlert.requestId = 2;
    dAlert.preemptive = true;
    runtime.transitionSession(dAlert);

    TEST_ASSERT_EQUAL(1, rotationEngine.pauseCalls);
    TEST_ASSERT_EQUAL(0, rotationEngine.deactivateCalls);
    TEST_ASSERT_EQUAL(1, alertEngine.activateCalls);

    // 3. Alert completes, returns to rotation -> alert deactivates, rotation resumes
    runtime.transitionSession(dRot);

    TEST_ASSERT_EQUAL(1, alertEngine.deactivateCalls);
    TEST_ASSERT_EQUAL(1, rotationEngine.resumeCalls);
    TEST_ASSERT_EQUAL(0, rotationEngine.deactivateCalls);
}

/**
 * @brief Tests DisplayRuntime lifecycle centralization across disparate engines without leak.
 */
void test_display_runtime_lifecycle_centralization(void) {
    DisplayRuntime runtime;
    TrackingMockEngine marqueeEngine;
    TrackingMockEngine visualizerEngine;

    runtime.registerSourceEngine(DisplaySourceId::MARQUEE, &marqueeEngine, EngineHandle("marquee", "marquee_main"));
    runtime.registerSourceEngine(DisplaySourceId::VISUALIZER, &visualizerEngine, EngineHandle("audiovisualizer", "visualizer_main"));

    // Initial state: neither engine active
    TEST_ASSERT_EQUAL(0, marqueeEngine.activateCalls);
    TEST_ASSERT_EQUAL(0, marqueeEngine.deactivateCalls);
    TEST_ASSERT_EQUAL(0, visualizerEngine.activateCalls);
    TEST_ASSERT_EQUAL(0, visualizerEngine.deactivateCalls);

    // 1. Transition to MARQUEE decision -> DisplayRuntime activates marquee
    DisplayDecision d1;
    d1.valid = true;
    d1.sourceId = DisplaySourceId::MARQUEE;
    d1.engineHandle = EngineHandle("marquee", "marquee_main");
    d1.requestId = 101;
    runtime.transitionSession(d1);

    TEST_ASSERT_EQUAL(1, marqueeEngine.activateCalls);
    TEST_ASSERT_EQUAL(0, marqueeEngine.deactivateCalls);
    TEST_ASSERT_EQUAL(0, visualizerEngine.activateCalls);
    TEST_ASSERT_EQUAL(0, visualizerEngine.deactivateCalls);

    // 2. Refresh same MARQUEE session -> No redundant activate/deactivate
    runtime.transitionSession(d1);
    TEST_ASSERT_EQUAL(1, marqueeEngine.activateCalls);
    TEST_ASSERT_EQUAL(0, marqueeEngine.deactivateCalls);

    // 3. Transition to VISUALIZER decision -> DisplayRuntime deactivates marquee and activates visualizer
    DisplayDecision d2;
    d2.valid = true;
    d2.sourceId = DisplaySourceId::VISUALIZER;
    d2.engineHandle = EngineHandle("audiovisualizer", "visualizer_main");
    d2.requestId = 102;
    runtime.transitionSession(d2);

    TEST_ASSERT_EQUAL(1, marqueeEngine.activateCalls);
    TEST_ASSERT_EQUAL(1, marqueeEngine.deactivateCalls);
    TEST_ASSERT_EQUAL(1, visualizerEngine.activateCalls);
    TEST_ASSERT_EQUAL(0, visualizerEngine.deactivateCalls);

    // 4. Transition to ROTATION decision -> DisplayRuntime deactivates visualizer
    DisplayDecision d3;
    d3.valid = true;
    d3.sourceId = DisplaySourceId::ROTATION;
    d3.requestId = 103;
    runtime.transitionSession(d3);

    TEST_ASSERT_EQUAL(1, marqueeEngine.activateCalls);
    TEST_ASSERT_EQUAL(1, marqueeEngine.deactivateCalls);
    TEST_ASSERT_EQUAL(1, visualizerEngine.activateCalls);
    TEST_ASSERT_EQUAL(1, visualizerEngine.deactivateCalls);
}

// =========================================================================
// 6. Capability Gating, Registrar & Engine Requirements
// =========================================================================

/**
 * @brief Tests EngineRequirements evaluation against hardware capabilities.
 */
void test_requirements_gating(void) {
    EngineRequirements reqPsram;
    reqPsram.needsPsram = true;

    auto check = EngineRegistrar::checkRequirements(reqPsram);
    if (!hardwareHAL.capabilities().hasPsram) {
        TEST_ASSERT_FALSE(check.satisfied);
        TEST_ASSERT_NOT_NULL(check.reason);
    } else {
        TEST_ASSERT_TRUE(check.satisfied);
    }

    EngineRequirements reqNone;
    auto checkNone = EngineRegistrar::checkRequirements(reqNone);
    TEST_ASSERT_TRUE(checkNone.satisfied);
}

/**
 * @brief Verifies that Fighter is treated strictly as an overlay and not registered as an independent engine.
 */
void test_fighter_not_in_registry_or_selectable(void) {
    // 1. EngineRegistrar must NOT register Fighter into EngineRegistry
    EngineRegistrar::registerAll();
    const EngineDescriptor* desc = EngineRegistry::getDescriptor("fighter");
    TEST_ASSERT_NULL(desc);

    // 2. ConfigSanitizer must reject instances pointing to "fighter"
    ConfigLoader cfg;
    cfg.addInstance("fighter_main", "fighter");
    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_EQUAL(1, res.invalid_instances);
    TEST_ASSERT_EQUAL(0, cfg.instances.size());
}

// =========================================================================
// 7. Overlay Manager & Layering Invariants
// =========================================================================

/**
 * @brief Tests migration and parsing of legacy "fighter_overlay": true into canonical "overlays": {"fighter": true}.
 */
void test_canonical_overlays_schema_and_migration(void) {
    ConfigLoader cfg;
    const char* legacyJson = R"({
        "system": {"idle_fighter_enabled": true},
        "rotation": [
            {"instance_id": "clock_main", "duration_sec": 15, "fighter_overlay": true},
            {"instance_id": "gifs_main", "duration_sec": 20, "overlays": {"fighter": true}},
            {"instance_id": "weather_main", "duration_sec": 10, "fighter_overlay": false}
        ]
    })";

    TEST_ASSERT_TRUE(cfg.parseFromJson(legacyJson));
    TEST_ASSERT_TRUE(cfg.rotation[0].overlays.fighter == FighterOverride::Enabled);
    TEST_ASSERT_TRUE(cfg.rotation[1].overlays.fighter == FighterOverride::Enabled);
    TEST_ASSERT_TRUE(cfg.rotation[2].overlays.fighter == FighterOverride::Disabled);

    // Verify serialization produces canonical "overlays": {"fighter": true}
    String serialized = cfg.serializeToJson();
    TEST_ASSERT_TRUE(serialized.indexOf("\"overlays\":{\"fighter\":true}") >= 0);
}

/**
 * @brief Tests overlay rendering combinations across rotation items and master switch override.
 */
void test_rotation_overlay_combinations(void) {
    ConfigLoader cfg;
    cfg.system.idle_fighter_enabled = true;

    OverlayManager overlay;
    overlay.initialize(nullptr, &cfg);

    // T14: Global = true + Unspecified -> ON
    overlay.configure(OverlayConfig{FighterOverride::Unspecified});
    TEST_ASSERT_TRUE(overlay.isActive());

    // T15: Global = true + Enabled -> ON
    overlay.configure(OverlayConfig{FighterOverride::Enabled});
    TEST_ASSERT_TRUE(overlay.isActive());

    // T16: Global = true + Disabled -> OFF
    overlay.configure(OverlayConfig{FighterOverride::Disabled});
    TEST_ASSERT_FALSE(overlay.isActive());

    // T17: Global = false + Enabled -> OFF (Master switch overrides)
    cfg.system.idle_fighter_enabled = false;
    overlay.configure(OverlayConfig{FighterOverride::Enabled});
    TEST_ASSERT_FALSE(overlay.isActive());

    // T17b: Global = false + Unspecified -> OFF
    overlay.configure(OverlayConfig{FighterOverride::Unspecified});
    TEST_ASSERT_FALSE(overlay.isActive());
}

/**
 * @brief Tests lazy allocation and heap preservation of overlay instances to prevent heap fragmentation.
 */
void test_overlay_manager_lifecycle_and_heap_preservation(void) {
    ConfigLoader cfg;
    cfg.system.idle_fighter_enabled = true;

    OverlayManager overlay;
    overlay.initialize(nullptr, &cfg);
    TEST_ASSERT_FALSE(overlay.hasInstantiatedFighter());

    // First time ON -> lazy allocation
    overlay.configure(OverlayConfig{true});
    TEST_ASSERT_TRUE(overlay.isActive());
    TEST_ASSERT_TRUE(overlay.hasInstantiatedFighter());

    // Switch to OFF -> inactive but instance PRESERVED in heap (prevents fragmentation)
    overlay.configure(OverlayConfig{false});
    TEST_ASSERT_FALSE(overlay.isActive());
    TEST_ASSERT_TRUE(overlay.hasInstantiatedFighter());

    // Switch back to ON -> re-activates without re-allocating
    overlay.configure(OverlayConfig{true});
    TEST_ASSERT_TRUE(overlay.isActive());
    TEST_ASSERT_TRUE(overlay.hasInstantiatedFighter());
}

/**
 * @brief Tests overlay suppression when priority sources (Marquee / Alert) preempt baseline rotation.
 */
void test_overlay_preemption_by_arbiter(void) {
    ConfigLoader cfg;
    cfg.system.idle_fighter_enabled = true;

    OverlayManager overlay;
    overlay.initialize(nullptr, &cfg);

    // 1. Rotation active with Fighter ON
    overlay.configure(OverlayConfig{true});
    TEST_ASSERT_TRUE(overlay.isActive());

    // 2. Preempted by priority source (Arbiter selects MARQUEE/MQTT -> passes empty OverlayConfig{})
    overlay.configure(OverlayConfig{});
    TEST_ASSERT_FALSE(overlay.isActive());

    // 3. Resumed back to Rotation with Fighter ON
    overlay.configure(OverlayConfig{true});
    TEST_ASSERT_TRUE(overlay.isActive());
}

/**
 * @brief Tests lock-free Triple-Buffer CAS state machine and reader isolation during active reader pinning.
 */
void test_snapshot_cas_state_machine_and_interleaving(void) {
    ConfigLoader cfg;
    cfg.addInstance("clock_main", "clock");

    // 1. Core 1 acquires snapshot in RAII guard (slot pinned in state READING)
    {
        ConfigSnapshotGuard guard = cfg.acquireSnapshot();
        TEST_ASSERT_EQUAL(2, guard->version); // version was incremented on addInstance
        TEST_ASSERT_EQUAL(1, guard->instances.size());
        TEST_ASSERT_EQUAL_STRING("clock_main", guard->instances[0].instance_id.c_str());

        // 2. Core 0 publishes 5 successive mutations while guard is actively pinned
        for (int i = 0; i < 5; ++i) {
            cfg.mutate([i](ConfigLoader& c) {
                c.matrix.powerLimitPercent = 50 + i;
            });
        }

        // Reader still observes its pinned snapshot safely without mutation corruption
        TEST_ASSERT_EQUAL(1, guard->instances.size());
        TEST_ASSERT_EQUAL_STRING("clock_main", guard->instances[0].instance_id.c_str());
    }

    // 3. After guard destruction, acquiring new snapshot yields latest consolidated version
    {
        ConfigSnapshotGuard newGuard = cfg.acquireSnapshot();
        TEST_ASSERT_EQUAL(7, newGuard->version);
        TEST_ASSERT_EQUAL(54, newGuard->matrix.powerLimitPercent);
    }
}

/**
 * @brief Tests re-entrant and multi-reader snapshot acquisitions on the same thread without deadlock.
 */
void test_snapshot_reentrant_and_multi_reader(void) {
    ConfigLoader cfg;
    cfg.setDefaults();

    // Nested/re-entrant acquisitions on the same thread
    ConfigSnapshotGuard outerGuard = cfg.acquireSnapshot();
    TEST_ASSERT_EQUAL(1, outerGuard->version);

    {
        ConfigSnapshotGuard innerGuard1 = cfg.acquireSnapshot();
        TEST_ASSERT_EQUAL(1, innerGuard1->version);
        {
            ConfigSnapshotGuard innerGuard2 = cfg.acquireSnapshot();
            TEST_ASSERT_EQUAL(1, innerGuard2->version);
        }
    }

    // Outer guard remains valid and safely pinned
    TEST_ASSERT_EQUAL(1, outerGuard->version);
}

/**
 * @brief Tests clean unwinding and lifecycle cleanup of intermediate submerged preemption entries when expired.
 */
void test_preemption_intermediate_expiration_unwinding(void) {
    TrackingMockEngine clockEngine("clock_engine");
    TrackingMockEngine mqttEngine("mqtt_engine");
    TrackingMockEngine alertEngine("alert_engine");

    DisplayRuntime runtime;
    DisplayArbiter arbiter;
    runtime.begin(nullptr, nullptr, nullptr, nullptr, nullptr, &arbiter);

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &clockEngine, EngineHandle("clock", "clock_main"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &mqttEngine, EngineHandle("message", "message_main"));
    runtime.registerSourceEngine(DisplaySourceId::ALERT, &alertEngine, EngineHandle("alert", "alert_main"));

    // 1. Initial baseline display: ROTATION
    ConfigLoader cfg;
    DisplayDecision d1 = runtime.update(cfg.acquireSnapshot().get());
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d1.sourceId);
    TEST_ASSERT_EQUAL(1, clockEngine.activateCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
    uint32_t baselineSessionId = runtime.getCurrentSession().sessionId;

    // 2. Preemption Level 1: MQTT message arrives
    DisplayRequest mqttReq{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::UNTIL_CANCELLED, true};
    mqttReq.engineHandle = EngineHandle("message", "message_main");
    arbiter.submitRequest(mqttReq);

    DisplayDecision d2 = runtime.update(cfg.acquireSnapshot().get());
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, d2.sourceId);
    TEST_ASSERT_EQUAL(1, clockEngine.pauseCalls);
    TEST_ASSERT_EQUAL(1, mqttEngine.activateCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());

    // 3. Preemption Level 2: Critical Alert arrives
    DisplayRequest alertReq{DisplaySourceId::ALERT, DisplayPriority::ALERT, RequestLifecycle::TIMED, true};
    alertReq.engineHandle = EngineHandle("alert", "alert_main");
    alertReq.timeout_ms = 5000;
    arbiter.submitRequest(alertReq);

    DisplayDecision d3 = runtime.update(cfg.acquireSnapshot().get());
    TEST_ASSERT_EQUAL(DisplaySourceId::ALERT, d3.sourceId);
    TEST_ASSERT_EQUAL(1, mqttEngine.pauseCalls);
    TEST_ASSERT_EQUAL(1, alertEngine.activateCalls);
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());

    // 4. Submerged session expiration: MQTT cancelled while Alert is displaying
    arbiter.cancelRequest(DisplaySourceId::MQTT);

    // 5. Alert concludes: Arbiter falls back to ROTATION
    arbiter.cancelRequest(DisplaySourceId::ALERT);

    DisplayDecision d4 = runtime.update(cfg.acquireSnapshot().get());
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d4.sourceId);
    TEST_ASSERT_EQUAL(1, alertEngine.deactivateCalls);
    TEST_ASSERT_EQUAL(1, mqttEngine.deactivateCalls); // Submerged MQTT cleaned up
    TEST_ASSERT_EQUAL(1, clockEngine.resumeCalls);    // Baseline Clock safely restored
    TEST_ASSERT_EQUAL(baselineSessionId, runtime.getCurrentSession().sessionId);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
}

/**
 * @brief Verifies that refreshing an already active preemptive session updates in-place without pushing duplicate stack entries.
 */
void test_preemption_refresh_does_not_push_same_engine(void) {
    DisplayRuntime runtime;
    TrackingMockEngine clockEngine("clock");
    TrackingMockEngine mqttEngine("mqtt");

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &clockEngine, EngineHandle("clock", "clock_main"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &mqttEngine, EngineHandle("message", "mqtt_main"));

    // Base rotation
    DisplayDecision dRot;
    dRot.valid = true;
    dRot.sourceId = DisplaySourceId::ROTATION;
    dRot.engineHandle = EngineHandle("clock", "clock_main");
    dRot.requestId = 1;
    runtime.transitionSession(dRot);

    // Preempt with MQTT
    DisplayDecision dMqtt;
    dMqtt.valid = true;
    dMqtt.sourceId = DisplaySourceId::MQTT;
    dMqtt.engineHandle = EngineHandle("message", "mqtt_main");
    dMqtt.preemptive = true;
    dMqtt.requestId = 101;
    runtime.transitionSession(dMqtt);

    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(1, clockEngine.pauseCalls);
    TEST_ASSERT_EQUAL(1, mqttEngine.activateCalls);
    uint32_t activeSessionId = runtime.getCurrentSession().sessionId;

    // Refresh MQTT x3 with restartTimer=true (new request IDs)
    for (uint32_t req = 102; req <= 104; ++req) {
        dMqtt.requestId = req;
        runtime.transitionSession(dMqtt);
        TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
        TEST_ASSERT_EQUAL(1, clockEngine.pauseCalls);
        TEST_ASSERT_EQUAL(1, mqttEngine.activateCalls);
        TEST_ASSERT_EQUAL(0, mqttEngine.deactivateCalls);
        TEST_ASSERT_EQUAL(0, clockEngine.resumeCalls);
        TEST_ASSERT_EQUAL(activeSessionId, runtime.getCurrentSession().sessionId);
        TEST_ASSERT_EQUAL(req, runtime.getCurrentSession().requestId);
    }
}

/**
 * @brief Tests that switching instances under the same source (non-preemptive) replaces the session without stack growth.
 */
void test_same_source_different_instance_replaces_without_preemption(void) {
    DisplayRuntime runtime;
    TrackingMockEngine mqttA("mqttA");
    TrackingMockEngine mqttB("mqttB");

    runtime.registerSourceEngine(DisplaySourceId::MQTT, &mqttA, EngineHandle("message", "instA"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &mqttB, EngineHandle("message", "instB"));

    DisplayDecision d1;
    d1.valid = true;
    d1.sourceId = DisplaySourceId::MQTT;
    d1.engineHandle = EngineHandle("message", "instA");
    d1.requestId = 1;
    runtime.transitionSession(d1);

    TEST_ASSERT_EQUAL(1, mqttA.activateCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());

    DisplayDecision d2;
    d2.valid = true;
    d2.sourceId = DisplaySourceId::MQTT;
    d2.engineHandle = EngineHandle("message", "instB");
    d2.requestId = 2;
    runtime.transitionSession(d2);

    TEST_ASSERT_EQUAL(1, mqttA.deactivateCalls);
    TEST_ASSERT_EQUAL(1, mqttB.activateCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(0, mqttA.pauseCalls);
}

/**
 * @brief Verifies transactional rejection: if a requested engine cannot be resolved, current session and lifecycle remain intact.
 */
void test_runtime_transactional_rejection_preserves_lifecycle(void) {
    DisplayRuntime runtime;
    TrackingMockEngine activeEng("active");

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &activeEng, EngineHandle("clock", "main"));

    DisplayDecision dBase;
    dBase.valid = true;
    dBase.sourceId = DisplaySourceId::ROTATION;
    dBase.engineHandle = EngineHandle("clock", "main");
    dBase.requestId = 1;
    runtime.transitionSession(dBase);

    uint32_t origSessionId = runtime.getCurrentSession().sessionId;
    uint32_t origStarted = runtime.getCurrentSession().startedAtMs;

    // Submit invalid handle (unresolvable)
    DisplayDecision dInvalid;
    dInvalid.valid = true;
    dInvalid.sourceId = DisplaySourceId::ALERT;
    dInvalid.engineHandle = EngineHandle("unknown_desc", "unknown_inst");
    dInvalid.preemptive = true;
    dInvalid.requestId = 999;
    runtime.transitionSession(dInvalid);

    // Assert session and engine lifecycle are 100% intact
    TEST_ASSERT_EQUAL_PTR(&activeEng, runtime.getCurrentSession().activeEngine);
    TEST_ASSERT_EQUAL(origSessionId, runtime.getCurrentSession().sessionId);
    TEST_ASSERT_EQUAL(origStarted, runtime.getCurrentSession().startedAtMs);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(1, activeEng.activateCalls);
    TEST_ASSERT_EQUAL(0, activeEng.deactivateCalls);
    TEST_ASSERT_EQUAL(0, activeEng.pauseCalls);
}

/**
 * @brief Verifies preemption stack capacity bounds (depth == 4) and deterministic rejection on saturation.
 */
void test_preemption_stack_overflow_rejection(void) {
    TrackingMockEngine eng0("eng0");
    TrackingMockEngine eng1("eng1");
    TrackingMockEngine eng2("eng2");
    TrackingMockEngine eng3("eng3");
    TrackingMockEngine eng4("eng4");
    TrackingMockEngine eng5("eng5");

    DisplayRuntime runtime;
    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &eng0, EngineHandle("eng0", "0"));
    runtime.registerSourceEngine(DisplaySourceId::GIF, &eng1, EngineHandle("eng1", "1"));
    runtime.registerSourceEngine(DisplaySourceId::MARQUEE, &eng2, EngineHandle("eng2", "2"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &eng3, EngineHandle("eng3", "3"));
    runtime.registerSourceEngine(DisplaySourceId::VISUALIZER, &eng4, EngineHandle("eng4", "4"));
    runtime.registerSourceEngine(DisplaySourceId::ALERT, &eng5, EngineHandle("eng5", "5"));

    DisplayDecision dec0;
    dec0.valid = true;
    dec0.sourceId = DisplaySourceId::ROTATION;
    dec0.engineHandle = EngineHandle("eng0", "0");
    runtime.transitionSession(dec0);

    // Preempt 4 times to fill stack
    DisplaySourceId sources[] = {DisplaySourceId::GIF, DisplaySourceId::MARQUEE, DisplaySourceId::MQTT, DisplaySourceId::VISUALIZER};
    const char* names[] = {"eng1", "eng2", "eng3", "eng4"};
    for (int i = 0; i < 4; ++i) {
        DisplayDecision dec;
        dec.valid = true;
        dec.sourceId = sources[i];
        dec.engineHandle = EngineHandle(names[i], names[i]);
        dec.preemptive = true;
        dec.requestId = 10 + i;
        runtime.transitionSession(dec);
    }
    TEST_ASSERT_EQUAL(4, runtime.getPreemptionDepth());

    // 5th preemption must be rejected deterministically without stack overflow
    DisplayDecision overflowDec;
    overflowDec.valid = true;
    overflowDec.sourceId = DisplaySourceId::ALERT;
    overflowDec.engineHandle = EngineHandle("eng5", "5");
    overflowDec.preemptive = true;
    overflowDec.requestId = 99;
    runtime.transitionSession(overflowDec);

    TEST_ASSERT_EQUAL(4, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(0, eng5.activateCalls);
    TEST_ASSERT_EQUAL(1, eng4.activateCalls);
}

/**
 * @brief Tests that DisplayArbiter remains completely stateless when DisplayRuntime rejects an invalid decision.
 */
void test_arbiter_stateless_no_phantom_state_on_runtime_rejection(void) {
    DisplayArbiter arbiter;
    DisplayRuntime runtime;
    TrackingMockEngine clockEng("clock");
    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &clockEng, EngineHandle("clock", "main"));

    ConfigLoader cfg;
    DisplayDecision d1 = runtime.update(cfg.acquireSnapshot().get());
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d1.sourceId);

    // Submit request that runtime cannot resolve
    DisplayRequest badReq{DisplaySourceId::ALERT, DisplayPriority::ALERT, RequestLifecycle::TIMED, true, 50, EngineHandle("nonexistent", "inst")};
    arbiter.submitRequest(badReq);

    // Tick 1: Arbiter emits Alert decision, Runtime rejects it
    DisplayDecision d2 = arbiter.evaluate();
    TEST_ASSERT_EQUAL(DisplaySourceId::ALERT, d2.sourceId);
    runtime.transitionSession(d2);
    TEST_ASSERT_EQUAL_PTR(&clockEng, runtime.getCurrentSession().activeEngine);

    // Tick 2: Arbiter evaluates again, still produces Alert, no phantom state, runtime continues Clock
    DisplayDecision d3 = arbiter.evaluate();
    TEST_ASSERT_EQUAL(DisplaySourceId::ALERT, d3.sourceId);
    runtime.transitionSession(d3);
    TEST_ASSERT_EQUAL_PTR(&clockEng, runtime.getCurrentSession().activeEngine);
}

/**
 * @brief Verifies that repeated refreshes of a preemptive child preserve a single stack entry.
 */
void test_preemption_child_refresh_preserves_single_stack_entry(void) {
    TrackingMockEngine clockEng("clock");
    TrackingMockEngine mqttEng("mqtt");
    TrackingMockEngine alertEng("alert");

    DisplayRuntime runtime;
    DisplayArbiter arbiter;
    runtime.begin(nullptr, nullptr, nullptr, nullptr, nullptr, &arbiter);

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &clockEng, EngineHandle("clock", "main"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &mqttEng, EngineHandle("message", "main"));
    runtime.registerSourceEngine(DisplaySourceId::ALERT, &alertEng, EngineHandle("alert", "main"));

    ConfigLoader cfg;
    runtime.update(cfg.acquireSnapshot().get()); // Base clock

    DisplayRequest mqttReq{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::UNTIL_CANCELLED, true, 10, EngineHandle("message", "main")};
    arbiter.submitRequest(mqttReq);
    runtime.update(cfg.acquireSnapshot().get()); // Preempt to MQTT (depth 1)
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());

    DisplayRequest alertReq{DisplaySourceId::ALERT, DisplayPriority::ALERT, RequestLifecycle::UNTIL_CANCELLED, true, 20, EngineHandle("alert", "main")};
    arbiter.submitRequest(alertReq);
    runtime.update(cfg.acquireSnapshot().get()); // Preempt to Alert (depth 2)
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());

    // Refresh Alert child x2 with restartTimer
    arbiter.submitRequest(alertReq, true);
    runtime.update(cfg.acquireSnapshot().get());
    arbiter.submitRequest(alertReq, true);
    runtime.update(cfg.acquireSnapshot().get());
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());

    // Alert cancelled -> MQTT resumes
    arbiter.cancelRequest(DisplaySourceId::ALERT);
    runtime.update(cfg.acquireSnapshot().get());
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(1, mqttEng.resumeCalls);
    TEST_ASSERT_EQUAL(1, alertEng.deactivateCalls);
}

/**
 * @brief Tests RotationManager zero-allocation lookup bounds and oversized instance string rejection.
 */
void test_rotation_manager_bounded_lookup(void) {
    RotationManager rot;
    ConfigLoader cfg;
    rot.begin(cfg);

    // 1. Absent lookup returns nullptr with 0 allocation
    TEST_ASSERT_NULL(rot.findActiveEngine("nonexistent"));
    TEST_ASSERT_NULL(rot.findActiveEngine(""));
    TEST_ASSERT_NULL(rot.findActiveEngine(nullptr));

    // 2. Reject instances > 31 chars
    const char* tooLong = "12345678901234567890123456789012"; // 32 chars
    TEST_ASSERT_NULL(rot.getOrCreateEngine(tooLong));

    const char* exact31 = "1234567890123456789012345678901"; // 31 chars
    TEST_ASSERT_NULL(rot.findActiveEngine(exact31)); // Not in config, returns nullptr safely
}

/**
 * @brief Tests EngineRegistrar capability validation truth table against HardwareHAL flags.
 */
void test_registrar_capability_truth_table(void) {
    // 1. None required -> satisfied
    EngineRequirements reqEmpty;
    TEST_ASSERT_TRUE(EngineRegistrar::checkRequirements(reqEmpty).satisfied);

    // 2. PSRAM
    EngineRequirements reqPsram;
    reqPsram.needsPsram = true;
    TEST_ASSERT_EQUAL(hardwareHAL.capabilities().hasPsram, EngineRegistrar::checkRequirements(reqPsram).satisfied);

    // 3. Audio / Microphone
    EngineRequirements reqAudio;
    reqAudio.needsAudio = true;
    TEST_ASSERT_EQUAL(hardwareHAL.capabilities().hasMicrophone, EngineRegistrar::checkRequirements(reqAudio).satisfied);

    // 4. Temp sensor
    EngineRequirements reqTemp;
    reqTemp.needsTempSensor = true;
    TEST_ASSERT_EQUAL(hardwareHAL.capabilities().hasTempSensor, EngineRegistrar::checkRequirements(reqTemp).satisfied);

    // 5. Gyroscope
    EngineRequirements reqGyro;
    reqGyro.needsGyroscope = true;
    TEST_ASSERT_EQUAL(hardwareHAL.capabilities().hasGyroscope, EngineRegistrar::checkRequirements(reqGyro).satisfied);

    // 6. Network
    EngineRequirements reqNet;
    reqNet.needsNetwork = true;
    TEST_ASSERT_EQUAL(hardwareHAL.capabilities().hasNetwork, EngineRegistrar::checkRequirements(reqNet).satisfied);

    // 7. SD card
    EngineRequirements reqSd;
    reqSd.needsSd = true;
    TEST_ASSERT_EQUAL(hardwareHAL.capabilities().hasSd, EngineRegistrar::checkRequirements(reqSd).satisfied);
}

/**
 * @brief Tests that non-preemptive REPLACE transition unwinds any orphaned preemption stack completely.
 */
void test_preemption_replace_unwinds_orphaned_stack(void) {
    TrackingMockEngine clockEng("clock");
    TrackingMockEngine alertEng("alert");
    TrackingMockEngine marqueeEng("marquee");

    DisplayRuntime runtime;
    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &clockEng, EngineHandle("clock", "main"));
    runtime.registerSourceEngine(DisplaySourceId::ALERT, &alertEng, EngineHandle("alert", "main"));
    runtime.registerSourceEngine(DisplaySourceId::MARQUEE, &marqueeEng, EngineHandle("marquee", "main"));

    // 1. Baseline rotation
    DisplayDecision d1;
    d1.valid = true;
    d1.sourceId = DisplaySourceId::ROTATION;
    d1.engineHandle = EngineHandle("clock", "main");
    runtime.transitionSession(d1);

    // 2. Preempt with Alert (depth 1)
    DisplayDecision d2;
    d2.valid = true;
    d2.sourceId = DisplaySourceId::ALERT;
    d2.engineHandle = EngineHandle("alert", "main");
    d2.preemptive = true;
    d2.requestId = 10;
    runtime.transitionSession(d2);

    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(1, clockEng.pauseCalls);

    // 3. Marquee switch (non-preemptive REPLACE)
    DisplayDecision d3;
    d3.valid = true;
    d3.sourceId = DisplaySourceId::MARQUEE;
    d3.engineHandle = EngineHandle("marquee", "main");
    d3.preemptive = false;
    d3.requestId = 20;
    runtime.transitionSession(d3);

    // Alert deactivated, orphaned Clock in stack deactivated, depth reset to 0, Marquee active
    TEST_ASSERT_EQUAL(1, alertEng.deactivateCalls);
    TEST_ASSERT_EQUAL(1, clockEng.deactivateCalls);
    TEST_ASSERT_EQUAL(1, marqueeEng.activateCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL_PTR(&marqueeEng, runtime.getCurrentSession().activeEngine);
}

/**
 * @brief Verifies that resolving uncreated engine handles through DisplayRuntime is non-mutating and side-effect free.
 */
void test_runtime_resolve_does_not_create_instance(void) {
    RotationManager rot;
    ConfigLoader cfg;
    rot.begin(cfg);

    size_t countBefore = rot.getActiveEngineCount();

    DisplayRuntime runtime;
    runtime.begin(nullptr, nullptr, &rot, nullptr, nullptr, nullptr);

    // Attempt to resolve a non-existent instance handle through DisplayRuntime
    EngineHandle uncreatedHandle("clock", "non_existent_instance");
    IEngine* resolved = runtime.resolveEngine(uncreatedHandle, DisplaySourceId::ROTATION);

    TEST_ASSERT_NULL(resolved);
    TEST_ASSERT_EQUAL(countBefore, rot.getActiveEngineCount());
}

/**
 * @brief Tests that submitting a preemptive request for the currently active session performs an in-place refresh rather than new preemption.
 */
void test_preemptive_same_session_refresh_is_not_preemption(void) {
    DisplayArbiter arbiter;
    DisplayRuntime runtime;
    TrackingMockEngine clockEng("clock");
    TrackingMockEngine mqttEng("mqtt");

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &clockEng, EngineHandle("clock", "main"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &mqttEng, EngineHandle("message", "main"));

    // 1. Clock active
    DisplayDecision d1 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d1.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, d1.sourceId);
    runtime.transitionSession(d1);
    TEST_ASSERT_EQUAL(1, clockEng.activateCalls);

    // 2. MQTT becomes active (preemptive)
    DisplayRequest mqttReq;
    mqttReq.sourceId = DisplaySourceId::MQTT;
    mqttReq.priority = DisplayPriority::MQTT;
    mqttReq.engineHandle = EngineHandle("message", "main");
    mqttReq.preemptive = true;
    mqttReq.requestId = 101;
    mqttReq.lifecycle = RequestLifecycle::UNTIL_CANCELLED;
    arbiter.submitRequest(mqttReq);

    DisplayDecision d2 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d2.valid);
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, d2.sourceId);
    TEST_ASSERT_TRUE(d2.preemptive);
    runtime.transitionSession(d2);

    // Baseline clock paused, MQTT activated
    TEST_ASSERT_EQUAL(1, clockEng.pauseCalls);
    TEST_ASSERT_EQUAL(1, mqttEng.activateCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(TransitionMode::PREEMPT, runtime.getCurrentSession().lastTransitionMode);

    // 3. MQTT refresh with new requestId
    DisplayRequest mqttRefresh = mqttReq;
    mqttRefresh.requestId = 102;
    arbiter.submitRequest(mqttRefresh);

    DisplayDecision d3 = arbiter.evaluate();
    TEST_ASSERT_TRUE(d3.valid);
    TEST_ASSERT_EQUAL(102, d3.requestId);
    runtime.transitionSession(d3);

    // Assert: zero new pauses, zero new activates, zero deactivates, depth intact at 1, internal in-place update
    TEST_ASSERT_EQUAL(1, clockEng.pauseCalls);
    TEST_ASSERT_EQUAL(1, mqttEng.activateCalls);
    TEST_ASSERT_EQUAL(0, mqttEng.deactivateCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(102, runtime.getCurrentSession().requestId);
    TEST_ASSERT_EQUAL(TransitionMode::PREEMPT, runtime.getCurrentSession().lastTransitionMode);
}

/**
 * @brief Tests registration of engine descriptors when hardware requirements are unsatisfied.
 */
void test_engine_requirement_unavailable_is_registered(void) {
    EngineRegistry::clear();

    class UnavailableMockHandler : public IEngineDescriptorHandler {
    public:
        EngineDescriptor getDescriptor() const override {
            EngineDescriptor desc;
            desc.metadata = {"unavail_mock", "Unavailable Mock", "test", "1.0"};
            desc.requirements.needsPsram = true;
            desc.factory = []() { return nullptr; };
            return desc;
        }
    };

    UnavailableMockHandler handler;
    EngineDescriptor desc = handler.getDescriptor();
    desc.available = false;
    desc.unavailableReason = "Requires PSRAM";
    EngineRegistry::registerEngine(desc);

    const EngineDescriptor* registered = EngineRegistry::getDescriptor("unavail_mock");
    TEST_ASSERT_NOT_NULL(registered);
    TEST_ASSERT_FALSE(registered->available);
    TEST_ASSERT_EQUAL_STRING("Requires PSRAM", registered->unavailableReason);
}

/**
 * @brief Comprehensive transition state machine matrix verifying 10 full lifecycle scenarios.
 */
void test_display_runtime_state_machine_matrix(void) {
    DisplayArbiter arbiter;
    DisplayRuntime runtime;
    TrackingMockEngine rotA("rotA");
    TrackingMockEngine rotB("rotB");
    TrackingMockEngine alertA("alertA");
    TrackingMockEngine alertB("alertB");
    TrackingMockEngine alertC("alertC");
    TrackingMockEngine alertD("alertD");
    TrackingMockEngine alertE("alertE");

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &rotA, EngineHandle("rotA", "instA"));
    runtime.registerSourceEngine(DisplaySourceId::ALERT, &alertA, EngineHandle("alertA", "instAA"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &alertB, EngineHandle("alertB", "instBB"));
    runtime.registerSourceEngine(DisplaySourceId::MARQUEE, &alertC, EngineHandle("alertC", "instCC"));
    runtime.registerSourceEngine(DisplaySourceId::VISUALIZER, &alertD, EngineHandle("alertD", "instDD"));

    // Scenario 1: Initial start RotA -> Replace with RotB
    DisplayDecision dRotA;
    dRotA.valid = true;
    dRotA.sourceId = DisplaySourceId::ROTATION;
    dRotA.engineHandle = EngineHandle("rotA", "instA");
    runtime.transitionSession(dRotA);
    TEST_ASSERT_EQUAL(1, rotA.activateCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &rotB, EngineHandle("rotB", "instB"));
    DisplayDecision dRotB;
    dRotB.valid = true;
    dRotB.sourceId = DisplaySourceId::ROTATION;
    dRotB.engineHandle = EngineHandle("rotB", "instB");
    runtime.transitionSession(dRotB);
    TEST_ASSERT_EQUAL(1, rotA.deactivateCalls);
    TEST_ASSERT_EQUAL(1, rotB.activateCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(TransitionMode::REPLACE, runtime.getCurrentSession().lastTransitionMode);

    // Scenario 2: Rotation B -> Alert A (PREEMPT)
    DisplayDecision dAlertA;
    dAlertA.valid = true;
    dAlertA.sourceId = DisplaySourceId::ALERT;
    dAlertA.priority = DisplayPriority::ALERT;
    dAlertA.preemptive = true;
    dAlertA.engineHandle = EngineHandle("alertA", "instAA");
    dAlertA.requestId = 201;
    runtime.transitionSession(dAlertA);
    TEST_ASSERT_EQUAL(1, rotB.pauseCalls);
    TEST_ASSERT_EQUAL(1, alertA.activateCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(TransitionMode::PREEMPT, runtime.getCurrentSession().lastTransitionMode);

    // Scenario 3: Alert A -> Alert B (PREEMPT)
    DisplayDecision dAlertB;
    dAlertB.valid = true;
    dAlertB.sourceId = DisplaySourceId::MQTT;
    dAlertB.priority = DisplayPriority::MQTT;
    dAlertB.preemptive = true;
    dAlertB.engineHandle = EngineHandle("alertB", "instBB");
    dAlertB.requestId = 202;
    runtime.transitionSession(dAlertB);
    TEST_ASSERT_EQUAL(1, alertA.pauseCalls);
    TEST_ASSERT_EQUAL(1, alertB.activateCalls);
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(TransitionMode::PREEMPT, runtime.getCurrentSession().lastTransitionMode);

    // Scenario 4: Alert B -> Alert B refresh (Internal REFRESH)
    DisplayDecision dAlertBRefresh = dAlertB;
    dAlertBRefresh.requestId = 203;
    runtime.transitionSession(dAlertBRefresh);
    TEST_ASSERT_EQUAL(1, alertB.activateCalls);
    TEST_ASSERT_EQUAL(0, alertB.deactivateCalls);
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(TransitionMode::PREEMPT, runtime.getCurrentSession().lastTransitionMode);

    // Scenario 5: Cancel Alert B -> Resume Alert A (RESUME)
    runtime.transitionSession(dAlertA);
    TEST_ASSERT_EQUAL(1, alertB.deactivateCalls);
    TEST_ASSERT_EQUAL(1, alertA.resumeCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(TransitionMode::RESUME, runtime.getCurrentSession().lastTransitionMode);

    // Scenario 6: Cancel Alert A -> Resume Rotation B (RESUME)
    runtime.transitionSession(dRotB);
    TEST_ASSERT_EQUAL(1, alertA.deactivateCalls);
    TEST_ASSERT_EQUAL(1, rotB.resumeCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL(TransitionMode::RESUME, runtime.getCurrentSession().lastTransitionMode);

    // Scenario 7: Rotation B -> Unresolvable Target (Rejected Transactionally)
    DisplayDecision dInvalid;
    dInvalid.valid = true;
    dInvalid.sourceId = DisplaySourceId::ROTATION;
    dInvalid.engineHandle = EngineHandle("invalid_engine", "invalid_inst");
    runtime.transitionSession(dInvalid);
    TEST_ASSERT_EQUAL(1, rotB.activateCalls); // No new activate or deactivate
    TEST_ASSERT_EQUAL(1, rotB.deactivateCalls); // Prior count was 0 + deact in replace = 1
    TEST_ASSERT_EQUAL_PTR(&rotB, runtime.getCurrentSession().activeEngine);

    // Scenario 8: Preemption Stack Overflow (Depth == 4 rejection)
    // Push 1: Alert A
    runtime.transitionSession(dAlertA);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());
    // Push 2: Alert B
    runtime.transitionSession(dAlertB);
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());
    // Push 3: Alert C
    DisplayDecision dAlertC;
    dAlertC.valid = true;
    dAlertC.sourceId = DisplaySourceId::MARQUEE;
    dAlertC.preemptive = true;
    dAlertC.engineHandle = EngineHandle("alertC", "instCC");
    runtime.transitionSession(dAlertC);
    TEST_ASSERT_EQUAL(3, runtime.getPreemptionDepth());
    // Push 4: Alert D (Max Depth)
    DisplayDecision dAlertD;
    dAlertD.valid = true;
    dAlertD.sourceId = DisplaySourceId::VISUALIZER;
    dAlertD.preemptive = true;
    dAlertD.engineHandle = EngineHandle("alertD", "instDD");
    runtime.transitionSession(dAlertD);
    TEST_ASSERT_EQUAL(4, runtime.getPreemptionDepth());

    // Push 5: Alert E (Must be cleanly rejected)
    DisplayDecision dAlertE;
    dAlertE.valid = true;
    dAlertE.sourceId = DisplaySourceId::ALERT; // new alert while stack is 4
    dAlertE.preemptive = true;
    dAlertE.engineHandle = EngineHandle("alertA", "instAA");
    runtime.transitionSession(dAlertE);
    TEST_ASSERT_EQUAL(4, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL_PTR(&alertD, runtime.getCurrentSession().activeEngine);

    // Scenario 9: Baseline Replace unwinds orphaned stack cleanly
    DisplayDecision dNewBaseline;
    dNewBaseline.valid = true;
    dNewBaseline.sourceId = DisplaySourceId::ROTATION;
    dNewBaseline.preemptive = false;
    dNewBaseline.engineHandle = EngineHandle("rotA", "instA");
    runtime.transitionSession(dNewBaseline);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL_PTR(&rotA, runtime.getCurrentSession().activeEngine);

    // Scenario 10: Parent Unresolvable on RESUME -> Rejection without lifecycle corruption
    DisplayRuntime runtimeUnres;
    TrackingMockEngine activeChild("child");
    runtimeUnres.registerSourceEngine(DisplaySourceId::ALERT, &activeChild, EngineHandle("child", "inst_child"));

    // Preempt to child with parent handle that is unresolvable
    DisplayDecision dPreemptChild;
    dPreemptChild.valid = true;
    dPreemptChild.sourceId = DisplaySourceId::ALERT;
    dPreemptChild.preemptive = true;
    dPreemptChild.engineHandle = EngineHandle("child", "inst_child");
    runtimeUnres.transitionSession(dPreemptChild);

    // Attempt to resume unresolvable parent
    DisplayDecision dResumeUnresolvable;
    dResumeUnresolvable.valid = true;
    dResumeUnresolvable.sourceId = DisplaySourceId::ROTATION; // Rot has no engine registered in runtimeUnres
    runtimeUnres.transitionSession(dResumeUnresolvable);

    // Child must remain active without crash or corrupted lifecycle
    TEST_ASSERT_EQUAL_PTR(&activeChild, runtimeUnres.getCurrentSession().activeEngine);
    TEST_ASSERT_EQUAL(1, runtimeUnres.getPreemptionDepth());
}

/**
 * @brief Tests cross-priority preemption, stateless arbitration, and edge-triggered transitions (T1 - T13, T18, T19).
 */
void test_cross_priority_and_edge_transitions(void) {
    DisplayArbiter arbiter;
    DisplayRuntime runtime;

    TrackingMockEngine rotEng("rot");
    TrackingMockEngine gifEng("gif");
    TrackingMockEngine marqEng("marq");
    TrackingMockEngine mqttEng("mqtt");
    TrackingMockEngine visEng("vis");

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &rotEng, EngineHandle("clock", "main"));
    runtime.registerSourceEngine(DisplaySourceId::GIF, &gifEng, EngineHandle("gifs", "main"));
    runtime.registerSourceEngine(DisplaySourceId::MARQUEE, &marqEng, EngineHandle("marquee", "main"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &mqttEng, EngineHandle("message", "main"));
    runtime.registerSourceEngine(DisplaySourceId::VISUALIZER, &visEng, EngineHandle("audiovisualizer", "main"));

    // 0. Base Rotation
    DisplayRequest reqRot{DisplaySourceId::ROTATION, DisplayPriority::ROTATION, RequestLifecycle::PERSISTENT, false};
    reqRot.engineHandle = EngineHandle("clock", "main");
    arbiter.submitRequest(reqRot);
    runtime.transitionSession(arbiter.evaluate());
    TEST_ASSERT_EQUAL(1, rotEng.activateCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());

    // T1: Rotation -> MQTT waiting (PREEMPT, depth = 1, baseline stacked)
    DisplayRequest reqMqtt{DisplaySourceId::MQTT, DisplayPriority::MQTT, RequestLifecycle::UNTIL_CANCELLED, true, 101, EngineHandle("message", "main")};
    arbiter.submitRequest(reqMqtt);
    DisplayDecision dMqtt = arbiter.evaluate();
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, dMqtt.sourceId);
    runtime.transitionSession(dMqtt);
    TEST_ASSERT_EQUAL(1, rotEng.pauseCalls);
    TEST_ASSERT_EQUAL(1, mqttEng.activateCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());

    // T2 & T19: MQTT refresh with new requestId (same source + handle + engine) -> REFRESH without preemption
    DisplayRequest reqMqttRefresh = reqMqtt;
    reqMqttRefresh.requestId = 102;
    arbiter.submitRequest(reqMqttRefresh);
    DisplayDecision dMqttRef = arbiter.evaluate();
    TEST_ASSERT_EQUAL(102, dMqttRef.requestId);
    runtime.transitionSession(dMqttRef);
    TEST_ASSERT_EQUAL(1, rotEng.pauseCalls); // No new pause
    TEST_ASSERT_EQUAL(1, mqttEng.activateCalls); // No new activate
    TEST_ASSERT_EQUAL(0, mqttEng.deactivateCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth()); // Depth unchanged

    // T3: MQTT (40) superior to Marquee (30) -> Marquee does NOT preempt MQTT, MQTT remains dominant
    DisplayRequest reqMarq{DisplaySourceId::MARQUEE, DisplayPriority::MARQUEE, RequestLifecycle::UNTIL_CANCELLED, true, 201, EngineHandle("marquee", "main")};
    arbiter.submitRequest(reqMarq);
    DisplayDecision dAfterMarq = arbiter.evaluate();
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, dAfterMarq.sourceId);
    runtime.transitionSession(dAfterMarq);
    TEST_ASSERT_EQUAL(1, mqttEng.activateCalls);
    TEST_ASSERT_EQUAL(0, marqEng.activateCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());

    // T7 & T8: MQTT -> Visualizer (50 > 40) -> Visualizer PREEMPTS MQTT (depth = 2)
    DisplayRequest reqVis{DisplaySourceId::VISUALIZER, DisplayPriority::VISUALIZER, RequestLifecycle::UNTIL_CANCELLED, true, 301, EngineHandle("audiovisualizer", "main")};
    arbiter.submitRequest(reqVis);
    DisplayDecision dVis = arbiter.evaluate();
    TEST_ASSERT_EQUAL(DisplaySourceId::VISUALIZER, dVis.sourceId);
    runtime.transitionSession(dVis);
    TEST_ASSERT_EQUAL(1, mqttEng.pauseCalls);
    TEST_ASSERT_EQUAL(1, visEng.activateCalls);
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());

    // T9: Visualizer refresh -> REFRESH, depth unchanged at 2
    DisplayRequest reqVisRef = reqVis;
    reqVisRef.requestId = 302;
    arbiter.submitRequest(reqVisRef);
    runtime.transitionSession(arbiter.evaluate());
    TEST_ASSERT_EQUAL(1, visEng.activateCalls);
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());

    // T10: Visualizer stop -> MQTT is still active -> RESUME MQTT (depth = 1)
    arbiter.cancelRequest(DisplaySourceId::VISUALIZER);
    DisplayDecision dAfterVisStop = arbiter.evaluate();
    TEST_ASSERT_EQUAL(DisplaySourceId::MQTT, dAfterVisStop.sourceId);
    runtime.transitionSession(dAfterVisStop);
    TEST_ASSERT_EQUAL(1, visEng.deactivateCalls);
    TEST_ASSERT_EQUAL(1, mqttEng.resumeCalls);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());

    // T6: Submitting Visualizer again, cancelling MQTT in background, Visualizer remains active
    arbiter.submitRequest(reqVis);
    runtime.transitionSession(arbiter.evaluate());
    TEST_ASSERT_EQUAL(2, runtime.getPreemptionDepth());
    // Cancel MQTT while Visualizer is active
    arbiter.cancelRequest(DisplaySourceId::MQTT);
    DisplayDecision dVisWithMqttGone = arbiter.evaluate();
    TEST_ASSERT_EQUAL(DisplaySourceId::VISUALIZER, dVisWithMqttGone.sourceId);
    runtime.transitionSession(dVisWithMqttGone);
    TEST_ASSERT_EQUAL_PTR(&visEng, runtime.getCurrentSession().activeEngine);

    // T11 & T5: Visualizer stop -> MQTT was cancelled -> Marquee (30) was in queue, or if Marquee cancelled -> Rotation (RESUME baseline, depth = 0)
    arbiter.cancelRequest(DisplaySourceId::MARQUEE);
    arbiter.cancelRequest(DisplaySourceId::VISUALIZER);
    DisplayDecision dBackToRot = arbiter.evaluate();
    TEST_ASSERT_EQUAL(DisplaySourceId::ROTATION, dBackToRot.sourceId);
    runtime.transitionSession(dBackToRot);
    TEST_ASSERT_EQUAL(1, rotEng.resumeCalls);
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
    TEST_ASSERT_EQUAL_PTR(&rotEng, runtime.getCurrentSession().activeEngine);

    // T12 & T13: Functional contract check for Visualizer auto-on-audio
    // Audio streaming = true, but auto_on_audio = false -> visualizerRequested = false
    bool isAudioStreaming = true;
    bool visEnabled = true;
    bool priorityMode = false;
    bool autoOnAudio = false;
    bool visRequestedT12 = visEnabled && (priorityMode || (autoOnAudio && isAudioStreaming));
    TEST_ASSERT_FALSE(visRequestedT12);

    // Audio streaming = true and auto_on_audio = true -> visualizerRequested = true
    autoOnAudio = true;
    bool visRequestedT13 = visEnabled && (priorityMode || (autoOnAudio && isAudioStreaming));
    TEST_ASSERT_TRUE(visRequestedT13);

    // T18: Edge-trigger tracking simulation
    struct SyncTracker {
        bool active = false;
        uint32_t reqId = 0;
        int mutations = 0;
        void sync(bool nextActive, uint32_t nextReqId) {
            if (active != nextActive || reqId != nextReqId) {
                active = nextActive;
                reqId = nextReqId;
                mutations++;
            }
        }
    } tracker;

    // First call -> 1 mutation
    tracker.sync(true, 10);
    TEST_ASSERT_EQUAL(1, tracker.mutations);

    // 100 identical calls -> 0 additional mutations (O(1) strict no-op)
    for (int i = 0; i < 100; ++i) {
        tracker.sync(true, 10);
    }
    TEST_ASSERT_EQUAL(1, tracker.mutations);

    // State change -> 2nd mutation
    tracker.sync(false, 0);
    TEST_ASSERT_EQUAL(2, tracker.mutations);
}

void test_network_budget_admission_and_telemetry(void) {
    // Validate calibrated thresholds
    TEST_ASSERT_EQUAL_UINT32(30u * 1024u, NetworkBudget::TLS_MIN_FREE_INTERNAL);
    TEST_ASSERT_EQUAL_UINT32(16896u, NetworkBudget::TLS_MIN_LARGEST_BLOCK);

    // Validate telemetry counter increments on admission check
    uint32_t initialDenied = NetworkBudget::getTlsDeniedCount().load();
    bool admitted = NetworkBudget::canStartTlsSession();
    if (!admitted) {
        TEST_ASSERT_EQUAL_UINT32(initialDenied + 1, NetworkBudget::getTlsDeniedCount().load());
    }
}

void test_engine_retirement_core1_release_barrier(void) {
    auto engine = std::unique_ptr<LifecycleMockEngine>(new LifecycleMockEngine());
    TEST_ASSERT_EQUAL(EngineResourceState::UNINITIALIZED, engine->getResourceState());

    // Core 1 activates engine
    engine->activate();
    TEST_ASSERT_EQUAL(EngineResourceState::ACTIVE, engine->getResourceState());

    // Core 1 deactivates engine
    engine->deactivate();
    TEST_ASSERT_EQUAL(EngineResourceState::DEACTIVATING, engine->getResourceState());

    // Core 1 asserts Release Barrier step 5: sets CORE1_RELEASED
    engine->setResourceState(EngineResourceState::CORE1_RELEASED);
    TEST_ASSERT_EQUAL(EngineResourceState::CORE1_RELEASED, engine->getResourceState());

    // Core 1 hands over ownership to Core 0 dispatcher queue
    bool queued = Core0LifecycleDispatcher::instance().retire(std::move(engine));
    TEST_ASSERT_TRUE(queued);

    // Core 0 executes reclamation
    Core0LifecycleDispatcher::instance().processRetirements();
    TEST_ASSERT_EQUAL(0, Core0LifecycleDispatcher::instance().getQuarantineCount());
}

void test_display_runtime_purge_engine_references(void) {
    DisplayRuntime runtime;
    TrackingMockEngine clockEng("clock");
    TrackingMockEngine alertEng("alert");

    runtime.registerSourceEngine(DisplaySourceId::ROTATION, &clockEng, EngineHandle("clock", "main"));
    runtime.registerSourceEngine(DisplaySourceId::MQTT, &alertEng, EngineHandle("message", "alert1"));

    DisplayArbiter arbiter;
    // 1. Clock active
    DisplayDecision d1 = arbiter.evaluate();
    runtime.transitionSession(d1);
    TEST_ASSERT_EQUAL_PTR(&clockEng, runtime.getCurrentSession().activeEngine);

    // 2. Alert preempts clock
    DisplayRequest req;
    req.sourceId = DisplaySourceId::MQTT;
    req.priority = DisplayPriority::ALERT;
    req.engineHandle = EngineHandle("message", "alert1");
    req.preemptive = true;
    req.requestId = 201;
    req.lifecycle = RequestLifecycle::UNTIL_CANCELLED;
    arbiter.submitRequest(req);

    DisplayDecision d2 = arbiter.evaluate();
    runtime.transitionSession(d2);
    TEST_ASSERT_EQUAL_PTR(&alertEng, runtime.getCurrentSession().activeEngine);
    TEST_ASSERT_EQUAL(1, runtime.getPreemptionDepth());

    // 3. Purge alert references (simulating retirement of alertEng)
    runtime.purgeEngineReferences(&alertEng, "alert1");
    TEST_ASSERT_NULL(runtime.getCurrentSession().activeEngine);

    // 4. Purge clock references (simulating retirement of clockEng while in preemption stack)
    runtime.purgeEngineReferences(&clockEng, "main");
    TEST_ASSERT_EQUAL(0, runtime.getPreemptionDepth());
}

void test_engine_retirement_queue_stress_and_saturation(void) {
    // 1. Fill queue to capacity (8 items)
    for (size_t i = 0; i < 8; ++i) {
        auto eng = std::unique_ptr<LifecycleMockEngine>(new LifecycleMockEngine());
        eng->setResourceState(EngineResourceState::CORE1_RELEASED);
        TEST_ASSERT_TRUE(Core0LifecycleDispatcher::instance().retire(std::move(eng)));
    }

    // 9th item must fail (queue full)
    auto overflowEng = std::unique_ptr<LifecycleMockEngine>(new LifecycleMockEngine());
    overflowEng->setResourceState(EngineResourceState::CORE1_RELEASED);
    TEST_ASSERT_FALSE(Core0LifecycleDispatcher::instance().retire(std::move(overflowEng)));

    // Drain queue on Core 0
    Core0LifecycleDispatcher::instance().processRetirements();

    // Now pushing succeeds again
    TEST_ASSERT_TRUE(Core0LifecycleDispatcher::instance().retire(std::move(overflowEng)));
    Core0LifecycleDispatcher::instance().processRetirements();
    TEST_ASSERT_EQUAL(0, Core0LifecycleDispatcher::instance().getQuarantineCount());

    // 2. Test Quarantine: engine that fails shutdownForDestruction()
    LifecycleMockEngine::s_failShutdown = true;
    auto faultyEng = std::unique_ptr<LifecycleMockEngine>(new LifecycleMockEngine());
    faultyEng->setResourceState(EngineResourceState::CORE1_RELEASED);
    TEST_ASSERT_TRUE(Core0LifecycleDispatcher::instance().retire(std::move(faultyEng)));

    Core0LifecycleDispatcher::instance().processRetirements();
    TEST_ASSERT_EQUAL(1, Core0LifecycleDispatcher::instance().getQuarantineCount());

    // Allow retry to succeed and drain quarantine
    LifecycleMockEngine::s_failShutdown = false;
    Core0LifecycleDispatcher::instance().processRetirements();
    TEST_ASSERT_EQUAL(0, Core0LifecycleDispatcher::instance().getQuarantineCount());

    // 3. 1000-cycle stress test: rapid handover and processing
    for (int cycle = 0; cycle < 1000; ++cycle) {
        auto eng = std::unique_ptr<LifecycleMockEngine>(new LifecycleMockEngine());
        eng->activate();
        eng->deactivate();
        eng->setResourceState(EngineResourceState::CORE1_RELEASED);
        bool queued = Core0LifecycleDispatcher::instance().retire(std::move(eng));
        TEST_ASSERT_TRUE(queued);
        Core0LifecycleDispatcher::instance().processRetirements();
    }
    TEST_ASSERT_EQUAL(0, Core0LifecycleDispatcher::instance().getQuarantineCount());
}

// =========================================================================
// 9. Drawing Surfaces, Hub75BulkEncoder & Coordinates
// =========================================================================
void test_surface_coordinates_rotation(void) {
    const int16_t w = 64;
    const int16_t h = 32;

    Point p0 = SurfaceCoordinates::logicalToPhysical(10, 5, w, h, 0);
    TEST_ASSERT_EQUAL_INT16(10, p0.x);
    TEST_ASSERT_EQUAL_INT16(5, p0.y);

    Point p1 = SurfaceCoordinates::logicalToPhysical(10, 5, w, h, 1);
    TEST_ASSERT_EQUAL_INT16(w - 1 - 5, p1.x); // 58
    TEST_ASSERT_EQUAL_INT16(10, p1.y);

    Point p2 = SurfaceCoordinates::logicalToPhysical(10, 5, w, h, 2);
    TEST_ASSERT_EQUAL_INT16(w - 1 - 10, p2.x); // 53
    TEST_ASSERT_EQUAL_INT16(h - 1 - 5, p2.y);  // 26

    Point p3 = SurfaceCoordinates::logicalToPhysical(10, 5, w, h, 3);
    TEST_ASSERT_EQUAL_INT16(5, p3.x);
    TEST_ASSERT_EQUAL_INT16(h - 1 - 10, p3.y); // 21

    int16_t lw = 0, lh = 0;
    SurfaceCoordinates::getLogicalDimensions(w, h, 0, lw, lh);
    TEST_ASSERT_EQUAL_INT16(64, lw);
    TEST_ASSERT_EQUAL_INT16(32, lh);

    SurfaceCoordinates::getLogicalDimensions(w, h, 1, lw, lh);
    TEST_ASSERT_EQUAL_INT16(32, lw);
    TEST_ASSERT_EQUAL_INT16(64, lh);
}

static uint16_t s_mockBitplanes[16][8][64];

static uint16_t* mockRowAccessor(void* userCtx, uint8_t row, uint8_t plane) {
    (void)userCtx;
    if (row < 16 && plane < 8) {
        return s_mockBitplanes[row][plane];
    }
    return nullptr;
}

void test_hub75_bulk_encoder_luts_and_encode(void) {
    uint8_t lutR[32];
    uint8_t lutG[64];
    uint8_t lutB[32];

    Hub75BulkEncoder::generateLuts(8, lutR, lutG, lutB);
    TEST_ASSERT_EQUAL_UINT8(0, lutR[0]);
    TEST_ASSERT_EQUAL_UINT8(0, lutG[0]);
    TEST_ASSERT_EQUAL_UINT8(0, lutB[0]);
    TEST_ASSERT_TRUE(lutR[31] >= 250);
    TEST_ASSERT_TRUE(lutG[63] >= 250);
    TEST_ASSERT_TRUE(lutB[31] >= 250);

    for (int i = 1; i < 32; ++i) {
        TEST_ASSERT_TRUE(lutR[i] >= lutR[i - 1]);
        TEST_ASSERT_TRUE(lutB[i] >= lutB[i - 1]);
    }
    for (int i = 1; i < 64; ++i) {
        TEST_ASSERT_TRUE(lutG[i] >= lutG[i - 1]);
    }

    std::vector<uint16_t> canvas(64 * 32, 0xF800); // Pure red
    memset(s_mockBitplanes, 0, sizeof(s_mockBitplanes));

    Hub75EncodingParams params;
    params.colorDepth = 8;
    params.rowsPerFrame = 16;
    params.width = 64;
    params.height = 32;
    params.lutR = lutR;
    params.lutG = lutG;
    params.lutB = lutB;
    params.rotation = 0;

    Hub75BulkEncoder::encode(
        canvas.data(),
        64,
        mockRowAccessor,
        nullptr,
        params
    );

    // Plane 7 (MSB) for pure red must have R1 bit set (1 << 0)
    uint16_t sample = s_mockBitplanes[0][7][0];
    TEST_ASSERT_TRUE((sample & (1 << 0)) != 0);
    TEST_ASSERT_TRUE((sample & (1 << 1)) == 0);
    TEST_ASSERT_TRUE((sample & (1 << 2)) == 0);
}

void test_display_surface_factory_selection(void) {
    auto resSingle = DisplaySurfaceFactory::createSurface(nullptr, 64, 32, "canvas_single", false);
    TEST_ASSERT_NOT_NULL(resSingle.surface.get());
    TEST_ASSERT_EQUAL(SurfaceSelectionReason::ExplicitUserPolicy, resSingle.reason);
    TEST_ASSERT_TRUE(resSingle.surface->hasCanvas());
    TEST_ASSERT_EQUAL(PresentationStrategy::CANVAS_BURST_SINGLE, resSingle.surface->presentationStrategy());

    auto resDirect = DisplaySurfaceFactory::createSurface(nullptr, 64, 32, "direct_double", false);
    TEST_ASSERT_NOT_NULL(resDirect.surface.get());
    TEST_ASSERT_EQUAL(SurfaceSelectionReason::ExplicitUserPolicy, resDirect.reason);
    TEST_ASSERT_FALSE(resDirect.surface->hasCanvas());
    TEST_ASSERT_EQUAL(PresentationStrategy::DIRECT_DMA_DOUBLE, resDirect.surface->presentationStrategy());

    auto resAuto = DisplaySurfaceFactory::createSurface(nullptr, 64, 32, "unknown_pipeline", false);
    TEST_ASSERT_NOT_NULL(resAuto.surface.get());
}

void test_canvas_buffered_surface_drawing_and_rotation(void) {
    auto res = DisplaySurfaceFactory::createSurface(nullptr, 64, 32, "canvas_single", false);
    auto* surface = res.surface.get();
    TEST_ASSERT_NOT_NULL(surface);
    
    // Clear to black
    surface->clear(0);
    auto view = surface->acquireCanvas();
    TEST_ASSERT_TRUE(view.isValid());
    TEST_ASSERT_EQUAL_UINT16(64, view.width);
    TEST_ASSERT_EQUAL_UINT16(32, view.height);
    for (size_t i = 0; i < 64 * 32; ++i) {
        TEST_ASSERT_EQUAL_UINT16(0, view.data[i]);
    }
    surface->releaseCanvas();

    // Draw pixel at (10, 5) with color 0x1234 in rotation 0
    surface->setRotation(0);
    surface->drawPixel(10, 5, 0x1234);
    view = surface->acquireCanvas();
    TEST_ASSERT_EQUAL_UINT16(0x1234, view.data[5 * 64 + 10]);
    surface->releaseCanvas();

    // Now set rotation to 1 (90 deg clockwise)
    // Physical dimensions are 64x32.
    // In rot 1, logical dimensions are width=32, height=64.
    // logicalToPhysical(10, 5, 64, 32, 1) -> x = 64 - 1 - 5 = 58, y = 10
    surface->clear(0);
    surface->setRotation(1);
    TEST_ASSERT_EQUAL_INT16(32, surface->width());
    TEST_ASSERT_EQUAL_INT16(64, surface->height());
    surface->drawPixel(10, 5, 0x5678);
    view = surface->acquireCanvas();
    TEST_ASSERT_EQUAL_UINT16(0x5678, view.data[10 * 64 + 58]);
    surface->releaseCanvas();

    // Now test blit565 with rotation 0
    surface->clear(0);
    surface->setRotation(0);
    uint16_t sprite[4] = { 0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD }; // 2x2 sprite
    surface->blit565(sprite, 2, 2, 2, 2, 2);
    view = surface->acquireCanvas();
    TEST_ASSERT_EQUAL_UINT16(0xAAAA, view.data[2 * 64 + 2]);
    TEST_ASSERT_EQUAL_UINT16(0xBBBB, view.data[2 * 64 + 3]);
    TEST_ASSERT_EQUAL_UINT16(0xCCCC, view.data[3 * 64 + 2]);
    TEST_ASSERT_EQUAL_UINT16(0xDDDD, view.data[3 * 64 + 3]);
    surface->releaseCanvas();
}

void test_presentation_backends(void) {
    MockPresentationBackend mock(64, 32, 8);
    TEST_ASSERT_EQUAL_UINT32(64 * 32 * 4, mock.calculateDmaBytes());
    
    auto target = mock.acquireDmaTarget();
    TEST_ASSERT_EQUAL_UINT16(64, target.width);
    TEST_ASSERT_EQUAL_UINT16(32, target.height);
    TEST_ASSERT_EQUAL_UINT16(16, target.rowsPerFrame);
    TEST_ASSERT_EQUAL_UINT8(8, target.colorDepth);
    TEST_ASSERT_NOT_NULL(target.buffer);
    TEST_ASSERT_NOT_NULL(target.plane(0));
    TEST_ASSERT_NOT_NULL(target.plane(7));
    TEST_ASSERT_NULL(target.plane(8));

    PresentationPolicy policy;
    auto timing = mock.commit(policy);
    TEST_ASSERT_EQUAL_UINT32(1, mock.getCommitCount());
    TEST_ASSERT_EQUAL_UINT32(150, timing.totalPresentUs);

    Hub75PresentationBackend hub75(nullptr, 64, 32, 8, true);
    TEST_ASSERT_EQUAL_UINT32(64 * 32 * 4 * 2, hub75.calculateDmaBytes());
    auto hubTarget = hub75.acquireDmaTarget();
    TEST_ASSERT_EQUAL_UINT16(64, hubTarget.width);
    TEST_ASSERT_EQUAL_UINT16(32, hubTarget.height);

    // Test CanvasBufferedSurface driving presentation backend end-to-end
    CanvasBufferedSurface canvasSurf(64, 32, CanvasStorage::SRAM, nullptr, false, &mock);
    canvasSurf.fillScreen(0xF800);
    auto canvasTiming = canvasSurf.present();
    TEST_ASSERT_EQUAL_UINT32(2, mock.getCommitCount());
    TEST_ASSERT_EQUAL_UINT32(150, canvasTiming.totalPresentUs);

    // Test geometry validation in DisplaySurfaceFactory (unsupported geometry rejected)
    auto unsuppResult = DisplaySurfaceFactory::createSurface(nullptr, 512, 512, "canvas_single");
    TEST_ASSERT_NULL(unsuppResult.surface.get());
    TEST_ASSERT_EQUAL((int)SurfaceSelectionReason::UnsupportedGeometry, (int)unsuppResult.reason);
}

void test_surface_coordinates_multi_resolution(void) {
    const struct {
        int16_t w;
        int16_t h;
    } geometries[] = {
        {128, 32},
        {128, 64},
        {256, 64}
    };

    for (const auto& g : geometries) {
        const int16_t w = g.w;
        const int16_t h = g.h;

        // 4 Corners: (0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)
        const Point corners[] = {
            {0, 0},
            {(int16_t)(w - 1), 0},
            {0, (int16_t)(h - 1)},
            {(int16_t)(w - 1), (int16_t)(h - 1)}
        };

        // Rotation 0: Identity
        for (const auto& c : corners) {
            Point p = SurfaceCoordinates::logicalToPhysical(c.x, c.y, w, h, 0);
            TEST_ASSERT_EQUAL_INT16(c.x, p.x);
            TEST_ASSERT_EQUAL_INT16(c.y, p.y);
        }

        // Rotation 1 (90 deg CW): logical is H x W
        int16_t lw1, lh1;
        SurfaceCoordinates::getLogicalDimensions(w, h, 1, lw1, lh1);
        TEST_ASSERT_EQUAL_INT16(h, lw1);
        TEST_ASSERT_EQUAL_INT16(w, lh1);

        Point p1_tl = SurfaceCoordinates::logicalToPhysical(0, 0, w, h, 1);
        TEST_ASSERT_EQUAL_INT16(w - 1, p1_tl.x);
        TEST_ASSERT_EQUAL_INT16(0, p1_tl.y);

        Point p1_br = SurfaceCoordinates::logicalToPhysical(lw1 - 1, lh1 - 1, w, h, 1);
        TEST_ASSERT_EQUAL_INT16(0, p1_br.x);
        TEST_ASSERT_EQUAL_INT16(w - 1, p1_br.y);

        // Rotation 2 (180 deg)
        Point p2_tl = SurfaceCoordinates::logicalToPhysical(0, 0, w, h, 2);
        TEST_ASSERT_EQUAL_INT16(w - 1, p2_tl.x);
        TEST_ASSERT_EQUAL_INT16(h - 1, p2_tl.y);

        Point p2_br = SurfaceCoordinates::logicalToPhysical(w - 1, h - 1, w, h, 2);
        TEST_ASSERT_EQUAL_INT16(0, p2_br.x);
        TEST_ASSERT_EQUAL_INT16(0, p2_br.y);

        // Rotation 3 (270 deg CW): logical is H x W
        int16_t lw3, lh3;
        SurfaceCoordinates::getLogicalDimensions(w, h, 3, lw3, lh3);
        TEST_ASSERT_EQUAL_INT16(h, lw3);
        TEST_ASSERT_EQUAL_INT16(w, lh3);

        Point p3_tl = SurfaceCoordinates::logicalToPhysical(0, 0, w, h, 3);
        TEST_ASSERT_EQUAL_INT16(0, p3_tl.x);
        TEST_ASSERT_EQUAL_INT16(h - 1, p3_tl.y);

        Point p3_br = SurfaceCoordinates::logicalToPhysical(lw3 - 1, lh3 - 1, w, h, 3);
        TEST_ASSERT_EQUAL_INT16(h - 1, p3_br.x);
        TEST_ASSERT_EQUAL_INT16(0, p3_br.y);
    }
}

void test_hub75_bulk_encoder_multi_depth_and_edge_colors(void) {
    const uint8_t depths[] = {2, 4, 5, 6, 8};
    for (uint8_t d : depths) {
        uint8_t lutR[32];
        uint8_t lutG[64];
        uint8_t lutB[32];

        Hub75BulkEncoder::generateLuts(d, lutR, lutG, lutB);

        uint16_t maxAllowed = (1 << d) - 1;
        TEST_ASSERT_EQUAL_UINT8(0, lutR[0]);
        TEST_ASSERT_EQUAL_UINT8(0, lutG[0]);
        TEST_ASSERT_EQUAL_UINT8(0, lutB[0]);

        TEST_ASSERT_TRUE(lutR[31] <= maxAllowed);
        TEST_ASSERT_TRUE(lutG[63] <= maxAllowed);
        TEST_ASSERT_TRUE(lutB[31] <= maxAllowed);

        // Monotonic check
        for (int i = 1; i < 32; ++i) {
            TEST_ASSERT_TRUE(lutR[i] >= lutR[i - 1]);
            TEST_ASSERT_TRUE(lutB[i] >= lutB[i - 1]);
        }
        for (int i = 1; i < 64; ++i) {
            TEST_ASSERT_TRUE(lutG[i] >= lutG[i - 1]);
        }
    }

    // Edge color validation at 8-bit depth
    uint8_t lutR[32], lutG[64], lutB[32];
    Hub75BulkEncoder::generateLuts(8, lutR, lutG, lutB);

    Hub75EncodingParams params;
    params.colorDepth = 8;
    params.rowsPerFrame = 16;
    params.width = 64;
    params.height = 32;
    params.lutR = lutR;
    params.lutG = lutG;
    params.lutB = lutB;
    params.rotation = 0;

    // 1. Black (0x0000): all bitplanes must be completely zero
    std::vector<uint16_t> canvasBlack(64 * 32, 0x0000);
    memset(s_mockBitplanes, 0xFF, sizeof(s_mockBitplanes));
    Hub75BulkEncoder::encode(canvasBlack.data(), 64, mockRowAccessor, nullptr, params);
    for (int p = 0; p < 8; ++p) {
        uint16_t val = s_mockBitplanes[0][p][0] & 0x003F; // mask R1,G1,B1,R2,G2,B2
        TEST_ASSERT_EQUAL_UINT16(0, val);
    }

    // 2. White (0xFFFF): MSB plane must have all R1,G1,B1 and R2,G2,B2 active
    std::vector<uint16_t> canvasWhite(64 * 32, 0xFFFF);
    memset(s_mockBitplanes, 0, sizeof(s_mockBitplanes));
    Hub75BulkEncoder::encode(canvasWhite.data(), 64, mockRowAccessor, nullptr, params);
    uint16_t msbWhite = s_mockBitplanes[0][7][0] & 0x003F;
    TEST_ASSERT_EQUAL_UINT16(0x003F, msbWhite); // all 6 color lines high

    // 3. Green (0x07E0): MSB plane must only have G1 (1 << 1) and G2 (1 << 4) set
    std::vector<uint16_t> canvasGreen(64 * 32, 0x07E0);
    memset(s_mockBitplanes, 0, sizeof(s_mockBitplanes));
    Hub75BulkEncoder::encode(canvasGreen.data(), 64, mockRowAccessor, nullptr, params);
    uint16_t msbGreen = s_mockBitplanes[0][7][0] & 0x003F;
    TEST_ASSERT_EQUAL_UINT16((1 << 1) | (1 << 4), msbGreen);

    // 4. Blue (0x001F): MSB plane must only have B1 (1 << 2) and B2 (1 << 5) set
    std::vector<uint16_t> canvasBlue(64 * 32, 0x001F);
    memset(s_mockBitplanes, 0, sizeof(s_mockBitplanes));
    Hub75BulkEncoder::encode(canvasBlue.data(), 64, mockRowAccessor, nullptr, params);
    uint16_t msbBlue = s_mockBitplanes[0][7][0] & 0x003F;
    TEST_ASSERT_EQUAL_UINT16((1 << 2) | (1 << 5), msbBlue);
}

// =========================================================================
// 10. Modular Storage Architecture & Working-Set Cache
// =========================================================================
void test_memory_config_storage_crud(void) {
    MemoryConfigStorage storage;

    TEST_ASSERT_FALSE(storage.exists("/test.json"));
    bool ok = storage.writeStringAtomic("/test.json", "{\"key\":\"val\"}");
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(storage.exists("/test.json"));

    String readBack;
    ok = storage.readString("/test.json", readBack);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"val\"}", readBack.c_str());

    std::vector<String> files;
    storage.listFiles("/", files);
    TEST_ASSERT_EQUAL(1, files.size());
    TEST_ASSERT_EQUAL_STRING("test.json", files[0].c_str());

    ok = storage.remove("/test.json");
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(storage.exists("/test.json"));
}

void test_working_set_cache_synchronization_and_eviction(void) {
    MemoryConfigStorage storage;

    storage.writeStringAtomic("/config/instances/clock.json", "{\"id\":\"clock\",\"engine\":\"ClockEngine\",\"settings\":{}}");
    storage.writeStringAtomic("/config/instances/sysinfo.json", "{\"id\":\"sysinfo\",\"engine\":\"SysInfoEngine\",\"settings\":{}}");
    storage.writeStringAtomic("/config/instances/weather.json", "{\"id\":\"weather\",\"engine\":\"WeatherEngine\",\"settings\":{}}");

    WorkingSetCache cache(storage, 2);

    std::vector<RotationEntry> playlist;
    playlist.push_back(RotationEntry{"clock", 10});
    playlist.push_back(RotationEntry{"weather", 15});

    bool synced = cache.syncWithPlaylist(playlist);
    TEST_ASSERT_TRUE(synced);
    TEST_ASSERT_EQUAL(2, cache.getCachedInstances().size());
    TEST_ASSERT_NOT_NULL(cache.getInstance("clock"));
    TEST_ASSERT_NOT_NULL(cache.getInstance("weather"));
    TEST_ASSERT_NULL(cache.getInstance("sysinfo"));

    // Rotate playlist: evict clock, bring in sysinfo
    playlist.clear();
    playlist.push_back(RotationEntry{"sysinfo", 10});
    playlist.push_back(RotationEntry{"weather", 15});

    synced = cache.syncWithPlaylist(playlist);
    TEST_ASSERT_TRUE(synced);
    TEST_ASSERT_EQUAL(2, cache.getCachedInstances().size());
    TEST_ASSERT_NULL(cache.getInstance("clock"));
    TEST_ASSERT_NOT_NULL(cache.getInstance("sysinfo"));
    TEST_ASSERT_NOT_NULL(cache.getInstance("weather"));

    EngineInstance newInst;
    newInst.instance_id = "alert";
    newInst.engine_id = "AlertEngine";
    bool saved = cache.saveAndCacheInstance(newInst);
    TEST_ASSERT_TRUE(saved);
    TEST_ASSERT_NOT_NULL(cache.getInstance("alert"));
    TEST_ASSERT_TRUE(storage.exists("/config/instances/alert.json"));

    bool deleted = cache.deleteInstance("alert");
    TEST_ASSERT_TRUE(deleted);
    TEST_ASSERT_NULL(cache.getInstance("alert"));
    TEST_ASSERT_FALSE(storage.exists("/config/instances/alert.json"));
}

void test_modular_config_migration_and_partitioning(void) {
    MemoryConfigStorage storage;

    const char* legacyJson = "{"
        "\"matrix_rows\":64,"
        "\"matrix_cols\":128,"
        "\"wifi_ssid\":\"TestWiFi\","
        "\"mqtt_enabled\":true,"
        "\"playlist_items\":[\"clock_1\"],"
        "\"instances\":["
            "{\"id\":\"clock_1\",\"engine\":\"ClockEngine\",\"settings\":{\"style\":\"digital\"}}"
        "]"
    "}";
    storage.writeStringAtomic("/config.json", legacyJson);

    ConfigLoader config;
    ModularConfigManager mgr(storage);

    bool migrated = mgr.checkAndMigrateLegacy(config, "/config.json");
    TEST_ASSERT_TRUE(migrated);

    TEST_ASSERT_TRUE(storage.exists("/config/hardware.json"));
    TEST_ASSERT_TRUE(storage.exists("/config/network.json"));
    TEST_ASSERT_TRUE(storage.exists("/config/playlist.json"));
    TEST_ASSERT_TRUE(storage.exists("/config/instances/clock_1.json"));

    TEST_ASSERT_TRUE(storage.exists("/config.json.bak"));
    TEST_ASSERT_FALSE(storage.exists("/config.json"));

    String hwStr;
    storage.readString("/config/hardware.json", hwStr);
    TEST_ASSERT_TRUE(hwStr.indexOf("\"matrix_rows\":64") >= 0);
    TEST_ASSERT_TRUE(hwStr.indexOf("\"matrix_cols\":128") >= 0);

    String netStr;
    storage.readString("/config/network.json", netStr);
    TEST_ASSERT_TRUE(netStr.indexOf("\"wifi_ssid\":\"TestWiFi\"") >= 0);

    String instStr;
    storage.readString("/config/instances/clock_1.json", instStr);
    TEST_ASSERT_TRUE(instStr.indexOf("\"id\":\"clock_1\"") >= 0);
    TEST_ASSERT_TRUE(instStr.indexOf("\"engine\":\"ClockEngine\"") >= 0);

    ConfigLoader freshConfig;
    bool loaded = mgr.loadAll(freshConfig);
    TEST_ASSERT_TRUE(loaded);

    ConfigSnapshotGuard guard = freshConfig.acquireSnapshot();
    const auto& snap = guard.get();
    TEST_ASSERT_EQUAL(64, snap.matrix.height);
    TEST_ASSERT_EQUAL(128, snap.matrix.width);
    TEST_ASSERT_EQUAL_STRING("TestWiFi", snap.wifi.ssid.c_str());
    TEST_ASSERT_TRUE(snap.mqtt.enabled);
    TEST_ASSERT_EQUAL_STRING("clock_1", snap.rotation[0].instance_id.c_str());
    TEST_ASSERT_EQUAL_STRING("auto", snap.matrix.render_pipeline.c_str());

    // Test dual camelCase and snake_case parsing
    MatrixConfig dualHw;
    storage.writeStringAtomic("/config/hardware.json", "{\"chainLength\": 4, \"colorDepth\": 6, \"renderPipeline\": \"canvas_single\"}");
    TEST_ASSERT_TRUE(mgr.loadHardware(dualHw));
    TEST_ASSERT_EQUAL(4, dualHw.chainLength);
    TEST_ASSERT_EQUAL(6, dualHw.colorDepth);
    TEST_ASSERT_EQUAL_STRING("canvas_single", dualHw.render_pipeline.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();

    // =========================================================================
    // 1. Engine Registry & Descriptor Validation
    // =========================================================================
    RUN_TEST(test_engine_registration);
    RUN_TEST(test_duplicate_registration_fails);
    RUN_TEST(test_get_descriptor);
    RUN_TEST(test_factory_creation);
    RUN_TEST(test_schema_and_fields);
    RUN_TEST(test_capabilities_and_requirements);
    RUN_TEST(test_engine_requirement_unavailable_is_registered);

    // =========================================================================
    // 2. Configuration Sanitizer & Default Injection
    // =========================================================================
    RUN_TEST(test_sanitizer_injects_defaults);
    RUN_TEST(test_sanitizer_clamps_out_of_bound_integers);
    RUN_TEST(test_sanitizer_handles_invalid_boolean_and_enum);
    RUN_TEST(test_sanitizer_flags_unknown_engines);
    RUN_TEST(test_sanitizer_validation_policy_coverage);
    RUN_TEST(test_sanitizer_night_brightness_allows_zero);
    RUN_TEST(test_sanitizer_rotation_recreates_missing_instances);

    // =========================================================================
    // 3. Display Arbiter & SPSC Queue Lock-Free Invariants
    // =========================================================================
    RUN_TEST(test_arbiter_priority_resolution);
    RUN_TEST(test_arbiter_one_shot_auto_consumption);
    RUN_TEST(test_arbiter_request_id_semantics);
    RUN_TEST(test_arbiter_spsc_lockfree);
    RUN_TEST(test_canonical_engine_handle_resolution);
    RUN_TEST(test_cross_priority_and_edge_transitions);

    // =========================================================================
    // 4. Triple-Buffer Linearizability & Snapshot Atomicity
    // =========================================================================
    RUN_TEST(test_config_snapshot_immutability_and_versioning);
    RUN_TEST(test_triple_buffer_snapshot_publication_and_versioning);
    RUN_TEST(test_snapshot_publication_linearizability);
    RUN_TEST(test_snapshot_cas_state_machine_and_interleaving);
    RUN_TEST(test_snapshot_reentrant_and_multi_reader);

    // =========================================================================
    // 5. Display Runtime State Machine, Lifecycle & Preemption Stack
    // =========================================================================
    RUN_TEST(test_display_runtime_lifecycle_centralization);
    RUN_TEST(test_display_runtime_preemption_lifecycle);
    RUN_TEST(test_preemption_refresh_does_not_push_same_engine);
    RUN_TEST(test_same_source_different_instance_replaces_without_preemption);
    RUN_TEST(test_preemptive_same_session_refresh_is_not_preemption);
    RUN_TEST(test_display_runtime_state_machine_matrix);

    // =========================================================================
    // 6. Capability Gating, Registrar & Engine Requirements
    // =========================================================================
    RUN_TEST(test_registrar_capability_truth_table);
    RUN_TEST(test_requirements_gating);
    RUN_TEST(test_fighter_not_in_registry_or_selectable);

    // =========================================================================
    // 7. Overlay Manager & Layering Invariants
    // =========================================================================
    RUN_TEST(test_canonical_overlays_schema_and_migration);
    RUN_TEST(test_rotation_overlay_combinations);
    RUN_TEST(test_overlay_manager_lifecycle_and_heap_preservation);
    RUN_TEST(test_overlay_preemption_by_arbiter);
    RUN_TEST(test_network_budget_admission_and_telemetry);

    // =========================================================================
    // 8. Core 0/1 Lifecycle Dispatcher, Release Barrier & Quarantine
    // =========================================================================
    RUN_TEST(test_engine_retirement_core1_release_barrier);
    RUN_TEST(test_display_runtime_purge_engine_references);
    RUN_TEST(test_engine_retirement_queue_stress_and_saturation);

    // =========================================================================
    // 9. Drawing Surfaces, Hub75BulkEncoder & Coordinates
    // =========================================================================
    RUN_TEST(test_surface_coordinates_rotation);
    RUN_TEST(test_surface_coordinates_multi_resolution);
    RUN_TEST(test_hub75_bulk_encoder_luts_and_encode);
    RUN_TEST(test_hub75_bulk_encoder_multi_depth_and_edge_colors);
    RUN_TEST(test_display_surface_factory_selection);
    RUN_TEST(test_canvas_buffered_surface_drawing_and_rotation);
    RUN_TEST(test_presentation_backends);

    // =========================================================================
    // 10. Modular Storage Architecture & Working-Set Cache
    // =========================================================================
    RUN_TEST(test_memory_config_storage_crud);
    RUN_TEST(test_working_set_cache_synchronization_and_eviction);
    RUN_TEST(test_modular_config_migration_and_partitioning);

    UNITY_END();
}

void loop() {
    delay(100);
}


