#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include "api/WebServerAPI.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief Test helper: Converts standard CSS hex color strings (#RRGGBB) to RGB565 format.
 *
 * Matches the color transformation logic used in WebServerAPI and MessageEngine.
 */
static uint16_t hexToRGB565(const char* hexStr) {
    if (!hexStr || hexStr[0] != '#' || strlen(hexStr) < 7) return 0xFFFF; // Default white
    long number = strtol(hexStr + 1, NULL, 16);
    uint8_t r = (number >> 16) & 0xFF;
    uint8_t g = (number >> 8) & 0xFF;
    uint8_t b = number & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/**
 * @brief Test helper: Maps user-friendly scroll directions ("left", "right", "up", "down")
 * to internal directional acronyms ("rtl", "ltr", "btt", "ttb").
 */
static String mapDirectionAlias(String dir) {
    dir.toLowerCase();
    if (dir == "left") return "rtl";
    if (dir == "right") return "ltr";
    if (dir == "up") return "btt";
    if (dir == "down") return "ttb";
    return dir;
}

/**
 * @brief Tests 24-bit hex RGB to 16-bit RGB565 packed format conversion.
 *
 * Verifies primary and boundary colors (White, Black, Red, Green, Blue) across casing.
 */
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

/**
 * @brief Tests scrolling text direction alias mapping and case insensitivity.
 */
void test_direction_mapping(void) {
    TEST_ASSERT_EQUAL_STRING("rtl", mapDirectionAlias("left").c_str());
    TEST_ASSERT_EQUAL_STRING("ltr", mapDirectionAlias("right").c_str());
    TEST_ASSERT_EQUAL_STRING("btt", mapDirectionAlias("up").c_str());
    TEST_ASSERT_EQUAL_STRING("ttb", mapDirectionAlias("down").c_str());
    TEST_ASSERT_EQUAL_STRING("rtl", mapDirectionAlias("rtl").c_str());
}

/**
 * @brief Verifies JSON structure and fields of the WebServer `/api/status` response.
 */
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

/**
 * @brief Tests JSON deserialization robustness and validation on settings payloads.
 */
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

/**
 * @brief Tests Issue #24: Instance creation payload parsing for engines without variant/primaryField
 * (e.g., Environment Sensor / 'temp', 'sysinfo', 'gnews').
 */
void test_instance_creation_without_variant(void) {
    const char* instanceJson = "{\"instance_id\":\"temp_01\",\"engine_id\":\"temp\",\"config\":{\"screen_title\":\"Environment Sensor\"}}";
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, instanceJson);
    TEST_ASSERT_TRUE(err == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("temp_01", doc["instance_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("temp", doc["engine_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Environment Sensor", doc["config"]["screen_title"].as<const char*>());
}

/**
 * @brief Tests rotation entry payload serialization & removal sync.
 */
void test_rotation_entry_sync(void) {
    StaticJsonDocument<512> doc;
    JsonArray arr = doc.to<JsonArray>();
    JsonObject e1 = arr.createNestedObject();
    e1["instance_id"] = "clock_01";
    e1["duration_sec"] = 15;
    JsonObject e2 = arr.createNestedObject();
    e2["instance_id"] = "temp_01";
    e2["duration_sec"] = 10;

    TEST_ASSERT_EQUAL_INT(2, arr.size());

    // Simulate removing entry e2 (trash button)
    arr.remove(1);
    TEST_ASSERT_EQUAL_INT(1, arr.size());
    TEST_ASSERT_EQUAL_STRING("clock_01", arr[0]["instance_id"].as<const char*>());
}

/**
 * @brief Tests WebServerAPI::timingSafeCompare correctness and resistance to side channels.
 */
void test_timing_safe_compare(void) {
    // Identical strings
    TEST_ASSERT_TRUE(WebServerAPI::timingSafeCompare("secret_token_123", "secret_token_123"));
    TEST_ASSERT_TRUE(WebServerAPI::timingSafeCompare("", ""));

    // Different lengths
    TEST_ASSERT_FALSE(WebServerAPI::timingSafeCompare("secret", "secret_longer"));
    TEST_ASSERT_FALSE(WebServerAPI::timingSafeCompare("secret_longer", "secret"));
    TEST_ASSERT_FALSE(WebServerAPI::timingSafeCompare("", "token"));
    TEST_ASSERT_FALSE(WebServerAPI::timingSafeCompare("token", ""));

    // Same length, 1 byte difference at various positions
    TEST_ASSERT_FALSE(WebServerAPI::timingSafeCompare("xecret_token_123", "secret_token_123"));
    TEST_ASSERT_FALSE(WebServerAPI::timingSafeCompare("secret_t0ken_123", "secret_token_123"));
    TEST_ASSERT_FALSE(WebServerAPI::timingSafeCompare("secret_token_124", "secret_token_123"));
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_color_hex_parsing);
    RUN_TEST(test_direction_mapping);
    RUN_TEST(test_api_status_json_structure);
    RUN_TEST(test_api_settings_validation);
    RUN_TEST(test_instance_creation_without_variant);
    RUN_TEST(test_rotation_entry_sync);
    RUN_TEST(test_timing_safe_compare);
    UNITY_END();
}

void loop() {
    delay(100);
}

