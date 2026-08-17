#include <Arduino.h>
#include <unity.h>
#include "engines/RetroFrontendListener.h"

void setUp(void) {}
void tearDown(void) {}

void test_system_name_variants_priority(void) {
    std::vector<RetroFrontendListener::SystemVariant> variants = RetroFrontendListener::getSystemNameVariants("snes");
    TEST_ASSERT_TRUE(variants.size() > 0);
    
    // First variant must be in "system" folder
    TEST_ASSERT_EQUAL_STRING("system", variants[0].folder.c_str());

    // Check presence of expected system variants
    bool foundSystemSnes = false;
    bool foundConsoleSnes = false;
    for (const auto& v : variants) {
        if (v.folder == "system" && v.name == "snes") foundSystemSnes = true;
        if (v.folder == "console" && v.name == "snes") foundConsoleSnes = true;
    }
    TEST_ASSERT_TRUE(foundSystemSnes);
    TEST_ASSERT_TRUE(foundConsoleSnes);
}

void test_arcade_system_variants(void) {
    std::vector<RetroFrontendListener::SystemVariant> variants = RetroFrontendListener::getSystemNameVariants("mame");
    bool foundSystemArcade = false;
    for (const auto& v : variants) {
        if (v.folder == "system" && v.name == "arcade") foundSystemArcade = true;
    }
    TEST_ASSERT_TRUE(foundSystemArcade);
}

void test_custom_system_variants(void) {
    std::vector<RetroFrontendListener::SystemVariant> variants = RetroFrontendListener::getSystemNameVariants("custom_console");
    bool foundCustomSpace = false;
    for (const auto& v : variants) {
        if (v.name == "custom console") foundCustomSpace = true;
    }
    TEST_ASSERT_TRUE(foundCustomSpace);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_system_name_variants_priority);
    RUN_TEST(test_arcade_system_variants);
    RUN_TEST(test_custom_system_variants);
    UNITY_END();
}

void loop() {
    delay(100);
}
