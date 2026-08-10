#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>

void setUp(void) {}
void tearDown(void) {}

// Test helper: RGB565 conversion logic matching MessageEngine/WebServerAPI
static uint16_t hexToRGB565(const char* hexStr) {
    if (!hexStr || hexStr[0] != '#' || strlen(hexStr) < 7) return 0xFFFF; // Default white
    long number = strtol(hexStr + 1, NULL, 16);
    uint8_t r = (number >> 16) & 0xFF;
    uint8_t g = (number >> 8) & 0xFF;
    uint8_t b = number & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Test helper: direction alias mapping matching MessageEngine
static String mapDirectionAlias(String dir) {
    dir.toLowerCase();
    if (dir == "left") return "rtl";
    if (dir == "right") return "ltr";
    if (dir == "up") return "btt";
    if (dir == "down") return "ttb";
    return dir;
}

void test_color_hex_parsing(void) {
    // #FFFFFF -> RGB565 0xFFFF
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, hexToRGB565("#FFFFFF"));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, hexToRGB565("#ffffff"));
    
    // #000000 -> RGB565 0x0000
    TEST_ASSERT_EQUAL_UINT16(0x0000, hexToRGB565("#000000"));
    
    // #FF0000 (Red) -> RGB565 0xF800
    TEST_ASSERT_EQUAL_UINT16(0xF800, hexToRGB565("#FF0000"));

    // #00FF00 (Green) -> RGB565 0x07E0
    TEST_ASSERT_EQUAL_UINT16(0x07E0, hexToRGB565("#00FF00"));

    // #0000FF (Blue) -> RGB565 0x001F
    TEST_ASSERT_EQUAL_UINT16(0x001F, hexToRGB565("#0000FF"));
}

void test_direction_mapping(void) {
    TEST_ASSERT_EQUAL_STRING("rtl", mapDirectionAlias("left").c_str());
    TEST_ASSERT_EQUAL_STRING("ltr", mapDirectionAlias("right").c_str());
    TEST_ASSERT_EQUAL_STRING("btt", mapDirectionAlias("up").c_str());
    TEST_ASSERT_EQUAL_STRING("ttb", mapDirectionAlias("down").c_str());
    TEST_ASSERT_EQUAL_STRING("rtl", mapDirectionAlias("rtl").c_str());
}

void test_api_status_json_structure(void) {
    StaticJsonDocument<512> doc;
    doc["status"] = "online";
    doc["uptime"] = 123456;
    doc["free_heap"] = 180000;
    doc["min_free_heap"] = 150000;
    doc["max_alloc_heap"] = 100000;

    String jsonStr;
    serializeJson(doc, jsonStr);

    TEST_ASSERT_TRUE(jsonStr.indexOf("\"status\":\"online\"") != -1);
    TEST_ASSERT_TRUE(jsonStr.indexOf("\"free_heap\":180000") != -1);
    TEST_ASSERT_TRUE(jsonStr.indexOf("\"min_free_heap\":150000") != -1);
}

void test_api_settings_validation(void) {
    // Valid JSON settings
    const char* validJson = "{\"brightness_limit\":80, \"clock_theme\":20}";
    StaticJsonDocument<256> docValid;
    DeserializationError errValid = deserializeJson(docValid, validJson);
    TEST_ASSERT_TRUE(errValid == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_INT(80, docValid["brightness_limit"].as<int>());
    TEST_ASSERT_EQUAL_INT(20, docValid["clock_theme"].as<int>());

    // Invalid JSON payload
    const char* invalidJson = "{brightness_limit: 80, clock_theme}";
    StaticJsonDocument<256> docInvalid;
    DeserializationError errInvalid = deserializeJson(docInvalid, invalidJson);
    TEST_ASSERT_TRUE(errInvalid != DeserializationError::Ok);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_color_hex_parsing);
    RUN_TEST(test_direction_mapping);
    RUN_TEST(test_api_status_json_structure);
    RUN_TEST(test_api_settings_validation);
    UNITY_END();
}

void loop() {
    delay(100);
}
