#include <Arduino.h>
#include <unity.h>
#include "core/RotationManager.h"

void setUp(void) {}
void tearDown(void) {}

void test_rotation_initialization(void) {
    // Basic test
    TEST_ASSERT_TRUE(true);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_rotation_initialization);
    UNITY_END();
}

void loop() {
    delay(100);
}
