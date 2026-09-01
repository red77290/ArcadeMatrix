#include <Arduino.h>
#include <unity.h>
#include "core/EngineRegistry.h"
#include "core/ConfigSanitizer.h"
#include "core/ConfigLoader.h"
#include "core/DisplayArbiter.h"
#include "core/DisplayRuntime.h"
#include "core/OverlayManager.h"
#include "engines/EngineRegistrar.h"
#include "hal/HardwareHAL.h"

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

void setUp(void) {
    EngineRegistry::clear();
}

void tearDown(void) {
    EngineRegistry::clear();
}

// 1. EngineRegistry & Descriptor Tests
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

// 2. ConfigSanitizer Tests
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

void test_sanitizer_flags_unknown_engines(void) {
    ConfigLoader cfg;
    cfg.addInstance("bad_inst", "non_existent_engine");

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_EQUAL(1, res.invalid_instances);
}

// 3. DisplayArbiter & OverlayManager Tests
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

void test_snapshot_publication_linearizability(void) {
    ConfigLoader cfg;
    cfg.setDefaults();

    // 50 rapid sequential mutations with checksum verification
    for (uint32_t i = 1; i <= 50; ++i) {
        cfg.mutate([i](ConfigLoader& c) {
            c.wifi.ssid = String("WiFi_Network_") + String(i);
        });

        // Core 1 reader acquire via RAII guard
        ConfigSnapshotGuard snap = cfg.acquireSnapshot();
        uint32_t expectedChecksum = (snap->version ^ 0x5A5A5A5A) + (uint32_t)snap->instances.size();
        TEST_ASSERT_EQUAL_HEX32(expectedChecksum, snap->checksum);
        TEST_ASSERT_TRUE(snap->wifi.ssid.startsWith("WiFi_Network_"));
    }
}

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
}

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
    TEST_ASSERT_EQUAL(3, cfg.rotation.size());
    TEST_ASSERT_TRUE(cfg.rotation[0].overlays.fighter);
    TEST_ASSERT_TRUE(cfg.rotation[1].overlays.fighter);
    TEST_ASSERT_FALSE(cfg.rotation[2].overlays.fighter);

    // Verify serialization produces canonical "overlays": {"fighter": true}
    String serialized = cfg.serializeToJson();
    TEST_ASSERT_TRUE(serialized.indexOf("\"overlays\":{\"fighter\":true}") >= 0);
}

void test_rotation_overlay_combinations(void) {
    ConfigLoader cfg;
    cfg.system.idle_fighter_enabled = true;

    OverlayManager overlay;
    overlay.initialize(nullptr, &cfg);

    // 1. Clock + Fighter ON
    overlay.configure(OverlayConfig{true});
    TEST_ASSERT_TRUE(overlay.isActive());

    // 2. Clock + Fighter OFF
    overlay.configure(OverlayConfig{false});
    TEST_ASSERT_FALSE(overlay.isActive());

    // 3. GIF + Fighter ON (MUST be valid with zero GIF special rule!)
    overlay.configure(OverlayConfig{true});
    TEST_ASSERT_TRUE(overlay.isActive());

    // 4. GIF + Fighter OFF
    overlay.configure(OverlayConfig{false});
    TEST_ASSERT_FALSE(overlay.isActive());

    // 5. Global master switch disabled overrides per-rotation true
    cfg.system.idle_fighter_enabled = false;
    overlay.configure(OverlayConfig{true});
    TEST_ASSERT_FALSE(overlay.isActive());
}

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

void setup() {
    delay(1000);
    UNITY_BEGIN();
    // Registry
    RUN_TEST(test_engine_registration);
    RUN_TEST(test_duplicate_registration_fails);
    RUN_TEST(test_get_descriptor);
    RUN_TEST(test_factory_creation);
    RUN_TEST(test_schema_and_fields);
    RUN_TEST(test_capabilities_and_requirements);
    // Sanitizer
    RUN_TEST(test_sanitizer_injects_defaults);
    RUN_TEST(test_sanitizer_clamps_out_of_bound_integers);
    RUN_TEST(test_sanitizer_handles_invalid_boolean_and_enum);
    RUN_TEST(test_sanitizer_flags_unknown_engines);
    // Arbiter, Overlays & Requirements
    RUN_TEST(test_arbiter_priority_resolution);
    RUN_TEST(test_arbiter_one_shot_auto_consumption);
    RUN_TEST(test_arbiter_request_id_semantics);
    RUN_TEST(test_arbiter_spsc_lockfree);
    RUN_TEST(test_canonical_engine_handle_resolution);
    RUN_TEST(test_config_snapshot_immutability_and_versioning);
    RUN_TEST(test_triple_buffer_snapshot_publication_and_versioning);
    RUN_TEST(test_snapshot_publication_linearizability);
    RUN_TEST(test_snapshot_cas_state_machine_and_interleaving);
    RUN_TEST(test_snapshot_reentrant_and_multi_reader);
    RUN_TEST(test_display_runtime_lifecycle_centralization);
    RUN_TEST(test_display_runtime_preemption_lifecycle);
    RUN_TEST(test_preemption_refresh_does_not_push_same_engine);
    RUN_TEST(test_same_source_different_instance_replaces_without_preemption);
    RUN_TEST(test_runtime_transactional_rejection_preserves_lifecycle);
    RUN_TEST(test_preemption_intermediate_expiration_unwinding);
    RUN_TEST(test_preemption_stack_overflow_rejection);
    RUN_TEST(test_preemption_replace_unwinds_orphaned_stack);
    RUN_TEST(test_arbiter_stateless_no_phantom_state_on_runtime_rejection);
    RUN_TEST(test_preemption_child_refresh_preserves_single_stack_entry);
    RUN_TEST(test_rotation_manager_bounded_lookup);
    RUN_TEST(test_runtime_resolve_does_not_create_instance);
    RUN_TEST(test_registrar_capability_truth_table);
    RUN_TEST(test_requirements_gating);
    RUN_TEST(test_fighter_not_in_registry_or_selectable);
    RUN_TEST(test_canonical_overlays_schema_and_migration);
    RUN_TEST(test_rotation_overlay_combinations);
    RUN_TEST(test_overlay_manager_lifecycle_and_heap_preservation);
    RUN_TEST(test_overlay_preemption_by_arbiter);
    UNITY_END();
}

void loop() {}

