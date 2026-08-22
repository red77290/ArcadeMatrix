#include <Arduino.h>
#include <unity.h>
#include "core/EngineRegistry.h"
#include "core/DisplayArbiter.h"
#include "core/OverlayManager.h"
#include "engines/EngineRegistrar.h"
#include "hal/HardwareHAL.h"

class MockContractEngine : public IEngine {
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
    desc.factory = []() { return std::unique_ptr<IEngine>(new MockContractEngine()); };
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
    RUN_TEST(test_arbiter_priority_resolution);
    RUN_TEST(test_requirements_gating);
    RUN_TEST(test_overlay_manager_lifecycle);
    RUN_TEST(test_overlay_manager_preemption_cycle);
    RUN_TEST(test_is_overlay_capability_separation);
    UNITY_END();
}

void loop() {}
