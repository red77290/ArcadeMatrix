#include <Arduino.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_utils_dummy(void) {
    TEST_ASSERT_TRUE(true);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_utils_dummy);
    UNITY_END();
}

void loop() {
    delay(100);
}
