#include <Arduino.h>
#include <unity.h>
#include "core/RotationManager.h"

void setUp(void) {}
void tearDown(void) {}

void test_parse_standard_rotation(void) {
    RotationManager rot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    rot.parseRotationString("clock,date,weather,gifs,crypto,stocks");

    const std::vector<RotationModule>& seq = rot.getSequence();
    TEST_ASSERT_EQUAL_INT(6, seq.size());
    TEST_ASSERT_EQUAL_INT(MODULE_CLOCK, seq[0]);
    TEST_ASSERT_EQUAL_INT(MODULE_DATE, seq[1]);
    TEST_ASSERT_EQUAL_INT(MODULE_WEATHER, seq[2]);
    TEST_ASSERT_EQUAL_INT(MODULE_GIFS, seq[3]);
    TEST_ASSERT_EQUAL_INT(MODULE_CRYPTO, seq[4]);
    TEST_ASSERT_EQUAL_INT(MODULE_STOCKS, seq[5]);
}

void test_parse_rotation_spaces_and_case(void) {
    RotationManager rot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    rot.parseRotationString("  CLOCK , Date , WEATHER , Crypto  ");

    const std::vector<RotationModule>& seq = rot.getSequence();
    TEST_ASSERT_EQUAL_INT(4, seq.size());
    TEST_ASSERT_EQUAL_INT(MODULE_CLOCK, seq[0]);
    TEST_ASSERT_EQUAL_INT(MODULE_DATE, seq[1]);
    TEST_ASSERT_EQUAL_INT(MODULE_WEATHER, seq[2]);
    TEST_ASSERT_EQUAL_INT(MODULE_CRYPTO, seq[3]);
}

void test_parse_invalid_module(void) {
    RotationManager rot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    rot.parseRotationString("clock,sprites_invalid,date");

    const std::vector<RotationModule>& seq = rot.getSequence();
    TEST_ASSERT_EQUAL_INT(2, seq.size());
    TEST_ASSERT_EQUAL_INT(MODULE_CLOCK, seq[0]);
    TEST_ASSERT_EQUAL_INT(MODULE_DATE, seq[1]);
}

void test_parse_empty_fallback(void) {
    RotationManager rot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    rot.parseRotationString("");

    const std::vector<RotationModule>& seq = rot.getSequence();
    TEST_ASSERT_EQUAL_INT(1, seq.size());
    TEST_ASSERT_EQUAL_INT(MODULE_CLOCK, seq[0]);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_parse_standard_rotation);
    RUN_TEST(test_parse_rotation_spaces_and_case);
    RUN_TEST(test_parse_invalid_module);
    RUN_TEST(test_parse_empty_fallback);
    UNITY_END();
}

void loop() {
    delay(100);
}
