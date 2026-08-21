#include <Arduino.h>
#include <unity.h>
#include "engines/FrontendSyncEngine.h"

void setUp(void) {}
void tearDown(void) {}

void test_clean_system_name(void) {
    TEST_ASSERT_EQUAL_STRING("Toaplan", FrontendSyncEngine::cleanSystemName("Arcade manufacturer Toaplan").c_str());
    TEST_ASSERT_EQUAL_STRING("Atari", FrontendSyncEngine::cleanSystemName("Arcade Manufacturer Atari").c_str());
    TEST_ASSERT_EQUAL_STRING("NeoGeo", FrontendSyncEngine::cleanSystemName("Arcade manufacturer NeoGeo").c_str());
    TEST_ASSERT_EQUAL_STRING("CPS1", FrontendSyncEngine::cleanSystemName("Arcade System CPS1").c_str());
}

void test_system_name_variants_priority(void) {
    std::vector<FrontendSyncEngine::SystemVariant> variants = FrontendSyncEngine::getSystemNameVariants("snes");
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
    std::vector<FrontendSyncEngine::SystemVariant> variants = FrontendSyncEngine::getSystemNameVariants("Arcade manufacturer Toaplan");
    bool foundDefaultToaplan = false;
    bool foundToaplan = false;
    for (const auto& v : variants) {
        if (v.folder == "console" && v.name == "default-Toaplan") foundDefaultToaplan = true;
        if (v.folder == "console" && v.name == "Toaplan") foundToaplan = true;
    }
    TEST_ASSERT_TRUE(foundDefaultToaplan);
    TEST_ASSERT_TRUE(foundToaplan);

    std::vector<FrontendSyncEngine::SystemVariant> atariVariants = FrontendSyncEngine::getSystemNameVariants("Arcade Manufacturer Atari");
    bool foundDefaultZatari = false;
    bool foundDirectAtari = false;
    for (const auto& v : atariVariants) {
        if (v.folder == "console" && v.name == "default-zatari") foundDefaultZatari = true;
        if (v.folder == "console" && v.name == "atari") foundDirectAtari = true;
    }
    TEST_ASSERT_TRUE(foundDefaultZatari);
    TEST_ASSERT_TRUE(foundDirectAtari);
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
