#include <Arduino.h>
#include <unity.h>
#include "core/EngineRegistry.h"
#include "core/ConfigSanitizer.h"
#include "core/ConfigLoader.h"
#include "core/DisplayArbiter.h"
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
    desc.capabilities.allowsOverlay = false;
    desc.capabilities.isOverlay = false;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = true;
    desc.requirements.needsAudio = true;
    EngineRegistry::registerEngine(desc);

    const EngineDescriptor* found = EngineRegistry::getDescriptor("test.caps");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_TRUE(found->capabilities.realtime);
    TEST_ASSERT_FALSE(found->capabilities.allowsOverlay);
    TEST_ASSERT_FALSE(found->capabilities.isOverlay);
    TEST_ASSERT_TRUE(found->capabilities.selfPaced);
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
    EngineInstance* inst = cfg.addInstance("clock_main", "clock");

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(2, res.defaults_injected);
    TEST_ASSERT_EQUAL_STRING("nintendo", inst->config.getString("theme").c_str());
    TEST_ASSERT_EQUAL(5, inst->config.getInt("speed"));
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
    EngineInstance* inst = cfg.addInstance("clock_main", "clock");
    inst->config.setInt("speed", 50);

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(1, res.values_clamped);
    TEST_ASSERT_EQUAL(10, inst->config.getInt("speed"));

    inst->config.setInt("speed", -5);
    res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(1, inst->config.getInt("speed"));
}

void test_sanitizer_handles_invalid_boolean_and_enum(void) {
    EngineDescriptor desc;
    desc.metadata.id = "clock";
    desc.schema.fields = {
        ConfigField("show_seconds", ConfigType::BOOLEAN, "Show Sec", "Show Sec", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("theme", ConfigType::ENUM, "Theme", "Theme", "nintendo", false, "nintendo,capcom,sega", "", "", "", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockTestEngine()); };
    EngineRegistry::registerEngine(desc);

    ConfigLoader cfg;
    EngineInstance* inst = cfg.addInstance("clock_main", "clock");
    inst->config.setString("show_seconds", "not_a_bool");
    inst->config.setString("theme", "unknown_theme_val");

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(2, res.values_fallback);
    TEST_ASSERT_EQUAL_STRING("true", inst->config.getString("show_seconds").c_str());
    TEST_ASSERT_EQUAL_STRING("nintendo", inst->config.getString("theme").c_str());
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

    DisplayRequest reqRot{"ROTATION", DisplayPriority::ROTATION, RequestLifecycle::PERSISTENT, false, "", 0, millis()};
    DisplayRequest reqMarq{"MARQUEE", DisplayPriority::MARQUEE, RequestLifecycle::ONE_SHOT, true, "", 0, millis()};
    DisplayRequest reqMqtt{"MESSAGE", DisplayPriority::MQTT, RequestLifecycle::ONE_SHOT, true, "", 0, millis()};

    arbiter.submitRequest(reqRot);
    TEST_ASSERT_EQUAL_STRING("ROTATION", arbiter.evaluate().source.c_str());

    arbiter.submitRequest(reqMarq);
    TEST_ASSERT_EQUAL_STRING("MARQUEE", arbiter.evaluate().source.c_str());

    arbiter.submitRequest(reqMqtt);
    TEST_ASSERT_EQUAL_STRING("MESSAGE", arbiter.evaluate().source.c_str());

    arbiter.cancelRequest("MESSAGE");
    TEST_ASSERT_EQUAL_STRING("MARQUEE", arbiter.evaluate().source.c_str());

    arbiter.cancelRequest("MARQUEE");
    TEST_ASSERT_EQUAL_STRING("ROTATION", arbiter.evaluate().source.c_str());
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

void test_overlay_manager_preemption_cycle(void) {
    ConfigLoader cfg;
    EngineInstance* inst = cfg.addInstance("fighter_main", "fighter");
    inst->config.setBool("enabled", true);

    EngineDescriptor desc;
    desc.metadata.id = "fighter";
    desc.capabilities.isOverlay = true;
    desc.capabilities.allowsOverlay = false;
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockTestEngine()); };
    EngineRegistry::registerEngine(desc);

    OverlayManager overlay;
    overlay.initialize(nullptr, &cfg);

    // 1. Rotation active with allowsOverlay=true -> overlay becomes active
    overlay.process(true);
    TEST_ASSERT_TRUE(overlay.hasActiveOverlay());

    // 2. Preempted by MQTT / GIF (allowsOverlay=false or deactivate called)
    overlay.deactivate();
    TEST_ASSERT_FALSE(overlay.hasActiveOverlay());

    // 3. Resumed back to Rotation with allowsOverlay=true -> overlay cleanly re-activates
    overlay.process(true);
    TEST_ASSERT_TRUE(overlay.hasActiveOverlay());

    overlay.deactivate();
    TEST_ASSERT_FALSE(overlay.hasActiveOverlay());
}

void test_is_overlay_capability_separation(void) {
    EngineDescriptor descFighter;
    descFighter.metadata.id = "fighter";
    descFighter.capabilities.isOverlay = true;
    descFighter.capabilities.allowsOverlay = false;
    EngineRegistry::registerEngine(descFighter);

    EngineDescriptor descClock;
    descClock.metadata.id = "clock";
    descClock.capabilities.isOverlay = false;
    descClock.capabilities.allowsOverlay = true;
    EngineRegistry::registerEngine(descClock);

    const EngineDescriptor* f = EngineRegistry::getDescriptor("fighter");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(f->capabilities.isOverlay);
    TEST_ASSERT_FALSE(f->capabilities.allowsOverlay);

    const EngineDescriptor* c = EngineRegistry::getDescriptor("clock");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_FALSE(c->capabilities.isOverlay);
    TEST_ASSERT_TRUE(c->capabilities.allowsOverlay);
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
    RUN_TEST(test_requirements_gating);
    RUN_TEST(test_overlay_manager_preemption_cycle);
    RUN_TEST(test_is_overlay_capability_separation);
    UNITY_END();
}

void loop() {}
