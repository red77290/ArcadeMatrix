#include <unity.h>
#include "core/EngineRegistry.h"

// Mock Engine implementation for testing
class MockEngine : public IEngine {
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

void test_engine_registration(void) {
    EngineDescriptor desc;
    desc.metadata.id = "test.engine";
    desc.metadata.name = "Test Engine";
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockEngine()); };

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
    desc2.metadata.id = "test.engine"; // Same ID

    TEST_ASSERT_TRUE(EngineRegistry::registerEngine(desc1));
    TEST_ASSERT_FALSE(EngineRegistry::registerEngine(desc2)); // Should fail
    
    size_t count = 0;
    EngineRegistry::getAllDescriptors(count);
    TEST_ASSERT_EQUAL(1, count); // Still only 1
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
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockEngine()); };
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
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = true;
    desc.requirements.needsAudio = true;
    EngineRegistry::registerEngine(desc);

    const EngineDescriptor* found = EngineRegistry::getDescriptor("test.caps");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_TRUE(found->capabilities.realtime);
    TEST_ASSERT_FALSE(found->capabilities.allowsOverlay);
    TEST_ASSERT_TRUE(found->capabilities.selfPaced);
    TEST_ASSERT_TRUE(found->requirements.needsPsram);
    TEST_ASSERT_TRUE(found->requirements.needsAudio);
}

#if defined(ARDUINO)
#include <Arduino.h>
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_engine_registration);
    RUN_TEST(test_duplicate_registration_fails);
    RUN_TEST(test_get_descriptor);
    RUN_TEST(test_factory_creation);
    RUN_TEST(test_schema_and_fields);
    RUN_TEST(test_capabilities_and_requirements);
    UNITY_END();
}
void loop() {}
#else
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_engine_registration);
    RUN_TEST(test_duplicate_registration_fails);
    RUN_TEST(test_get_descriptor);
    RUN_TEST(test_factory_creation);
    RUN_TEST(test_schema_and_fields);
    RUN_TEST(test_capabilities_and_requirements);
    return UNITY_END();
}
#endif
