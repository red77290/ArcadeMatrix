#include <Arduino.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_retrofrontend_dummy(void) {
    TEST_ASSERT_TRUE(true);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_retrofrontend_dummy);
    UNITY_END();
}

void loop() {
    delay(100);
}
