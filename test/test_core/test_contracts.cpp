#include <Arduino.h>
#include <unity.h>
#include "core/EngineRegistry.h"
#include "core/DisplayArbiter.h"
#include "core/OverlayManager.h"
#include "engines/EngineRegistrar.h"
#include "hal/HardwareHAL.h"

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

void test_overlay_manager_lifecycle(void) {
    ConfigLoader cfg;
    EngineInstance* inst = cfg.addInstance("fighter_main", "fighter");
    inst->config.setBool("enabled", true);

    OverlayManager overlay;
    overlay.initialize(nullptr, &cfg);

    // If source does not allow overlay, overlay should remain inactive
    overlay.process(false);
    TEST_ASSERT_FALSE(overlay.hasActiveOverlay());

    overlay.deactivate();
    TEST_ASSERT_FALSE(overlay.hasActiveOverlay());
}

void setup() {
    delay(1000);
    UNITY_BEGIN();
    RUN_TEST(test_arbiter_priority_resolution);
    RUN_TEST(test_requirements_gating);
    RUN_TEST(test_overlay_manager_lifecycle);
    UNITY_END();
}

void loop() {}
