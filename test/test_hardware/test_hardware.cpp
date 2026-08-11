#include <Arduino.h>
#include <unity.h>
#include "hal/HardwareHAL.h"

void setUp(void) {}
void tearDown(void) {}

void test_hal_default_states(void) {
    HardwareHAL hal;
    TEST_ASSERT_FALSE(hal.isAudioSamplingActive());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, hal.getMicGain());
}

void test_mic_gain_bounds(void) {
    HardwareHAL hal;
    hal.setMicGain(2.5f);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, hal.getMicGain());

    hal.setMicGain(-1.0f); // Invalid gain fallback to 1.0
    TEST_ASSERT_EQUAL_FLOAT(1.0f, hal.getMicGain());
}

void test_environment_data_conversion(void) {
    float tempC = 22.5f;
    float tempF = (tempC * 9.0f / 5.0f) + 32.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 72.5f, tempF);
}

void setup() {
    delay(1000);
    UNITY_BEGIN();
    RUN_TEST(test_hal_default_states);
    RUN_TEST(test_mic_gain_bounds);
    RUN_TEST(test_environment_data_conversion);
    UNITY_END();
}

void loop() {
    delay(100);
}
