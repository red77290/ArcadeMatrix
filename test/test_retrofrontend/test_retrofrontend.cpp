#include <Arduino.h>
#include <unity.h>
#include "engines/RetroFrontendListener.h"

void setUp(void) {}
void tearDown(void) {}

void test_clean_system_name(void) {
    TEST_ASSERT_EQUAL_STRING("Toaplan", RetroFrontendListener::cleanSystemName("Arcade manufacturer Toaplan").c_str());
    TEST_ASSERT_EQUAL_STRING("NeoGeo", RetroFrontendListener::cleanSystemName("Arcade manufacturer NeoGeo").c_str());
    TEST_ASSERT_EQUAL_STRING("CPS1", RetroFrontendListener::cleanSystemName("Arcade System CPS1").c_str());
}

void test_system_name_variants_priority(void) {
    std::vector<RetroFrontendListener::SystemVariant> variants = RetroFrontendListener::getSystemNameVariants("snes");
    TEST_ASSERT_TRUE(variants.size() > 0);
    
    // First variant must be in "console" folder
    TEST_ASSERT_EQUAL_STRING("console", variants[0].folder.c_str());
    TEST_ASSERT_EQUAL_STRING("default-snes", variants[0].name.c_str());

    bool foundConsoleDefault = false;
    bool foundConsoleSnes = false;
    for (const auto& v : variants) {
        if (v.folder == "console" && v.name == "default-snes") foundConsoleDefault = true;
        if (v.folder == "console" && v.name == "snes") foundConsoleSnes = true;
    }
    TEST_ASSERT_TRUE(foundConsoleDefault);
    TEST_ASSERT_TRUE(foundConsoleSnes);
}

void test_arcade_manufacturer_cleaning(void) {
    std::vector<RetroFrontendListener::SystemVariant> variants = RetroFrontendListener::getSystemNameVariants("Arcade manufacturer Toaplan");
    bool foundDefaultToaplan = false;
    bool foundToaplan = false;
    for (const auto& v : variants) {
        if (v.folder == "console" && v.name == "default-Toaplan") foundDefaultToaplan = true;
        if (v.folder == "console" && v.name == "Toaplan") foundToaplan = true;
    }
    TEST_ASSERT_TRUE(foundDefaultToaplan);
    TEST_ASSERT_TRUE(foundToaplan);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_clean_system_name);
    RUN_TEST(test_system_name_variants_priority);
    RUN_TEST(test_arcade_manufacturer_cleaning);
    UNITY_END();
}

void loop() {
    delay(100);
}
