#include <Arduino.h>
#include <unity.h>
#include "core/EngineRegistry.h"
#include "core/ConfigSanitizer.h"
#include "core/ConfigLoader.h"

// Mock Engine implementation for testing
class MockSanitizerEngine : public IEngine {
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

void test_sanitizer_injects_defaults(void) {
    EngineDescriptor desc;
    desc.metadata.id = "clock";
    desc.schema.fields = {
        ConfigField("theme", ConfigType::ENUM, "Theme", "Visual theme", "nintendo", false, "nintendo,capcom,sega", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("speed", ConfigType::INTEGER, "Speed", "Speed", "5", false, "1", "10", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockSanitizerEngine()); };
    EngineRegistry::registerEngine(desc);

    ConfigLoader cfg;
    EngineInstance* inst = cfg.addInstance("clock_main", "clock");
    // Empty config - missing fields

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
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockSanitizerEngine()); };
    EngineRegistry::registerEngine(desc);

    ConfigLoader cfg;
    EngineInstance* inst = cfg.addInstance("clock_main", "clock");
    inst->config.setInt("speed", 50); // > max 10

    SanitizeResult res = ConfigSanitizer::sanitizeInstances(cfg);
    TEST_ASSERT_TRUE(res.modified);
    TEST_ASSERT_EQUAL(1, res.values_clamped);
    TEST_ASSERT_EQUAL(10, inst->config.getInt("speed"));

    inst->config.setInt("speed", -5); // < min 1
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
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockSanitizerEngine()); };
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

void setup() {
    delay(1000);
    UNITY_BEGIN();
    RUN_TEST(test_sanitizer_injects_defaults);
    RUN_TEST(test_sanitizer_clamps_out_of_bound_integers);
    RUN_TEST(test_sanitizer_handles_invalid_boolean_and_enum);
    RUN_TEST(test_sanitizer_flags_unknown_engines);
    UNITY_END();
}

void loop() {}
