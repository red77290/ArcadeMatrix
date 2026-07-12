#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <unity.h>
#include "ConfigLoader.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_default_values(void) {
    ConfigLoader config;
    // Test if defaults are properly set
    TEST_ASSERT_EQUAL_UINT16(64, config.matrix.width);
    TEST_ASSERT_EQUAL_UINT16(32, config.matrix.height);
    TEST_ASSERT_EQUAL_STRING("P3", config.matrix.panelType.c_str());
    TEST_ASSERT_EQUAL_UINT8(2, config.matrix.chainLength);
    TEST_ASSERT_EQUAL_UINT8(24, config.matrix.colorDepth);
    TEST_ASSERT_FALSE(config.matrix.forceSingleBuffer);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", config.time.ntpServer.c_str());
}

void test_parse_valid_ini(void) {
    ConfigLoader config;
    const char* iniData = 
        "; This is a comment\n"
        "[WIFI]\n"
        "SSID=\"MyNetwork\"\n"
        "PASSWORD=\"Secret123\"\n"
        "\n"
        "[MATRIX]\n"
        "WIDTH=256\n"
        "HEIGHT=64\n"
        "CHAIN=2\n"
        "BRIGHTNESS_LIMIT=40\n"
        "COLOR_DEPTH=16\n"
        "\n"
        "[MQTT]\n"
        "ENABLED=true\n"
        "BROKER=\"192.168.1.100\"\n"
        "PORT=1883\n"
        "USER=\"mqtt_user\"\n"
        "PASS=\"mqtt_pass\"\n"
        "DEVICE_NAME=\"ArcadeMatrix\"\n"
        "\n"
        "[TIME]\n"
        "NTP_SERVER=\"time.google.com\"\n"
        "FORMAT_24H=false\n";

    bool success = config.parseFromString(iniData);
    TEST_ASSERT_TRUE(success);

    // Assert WiFi
    TEST_ASSERT_EQUAL_STRING("MyNetwork", config.wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("Secret123", config.wifi.password.c_str());

    // Assert Matrix
    TEST_ASSERT_EQUAL_UINT16(256, config.matrix.width);
    TEST_ASSERT_EQUAL_UINT16(64, config.matrix.height);
    TEST_ASSERT_EQUAL_UINT8(2, config.matrix.chainLength);
    TEST_ASSERT_EQUAL_UINT8(40, config.matrix.powerLimitPercent);
    TEST_ASSERT_EQUAL_UINT8(16, config.matrix.colorDepth);

    // Assert MQTT
    TEST_ASSERT_TRUE(config.mqtt.enabled);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", config.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_UINT16(1883, config.mqtt.port);
    TEST_ASSERT_EQUAL_STRING("mqtt_user", config.mqtt.user.c_str());
    TEST_ASSERT_EQUAL_STRING("ArcadeMatrix", config.mqtt.deviceName.c_str());

    // Assert Time
    TEST_ASSERT_EQUAL_STRING("time.google.com", config.time.ntpServer.c_str());
    TEST_ASSERT_FALSE(config.time.format24h);
}

void test_parse_malformed_ini(void) {
    ConfigLoader config;
    const char* malformedData = 
        "Random line without section\n"
        "SSID=ShouldBeIgnored\n"
        "[WIFI]\n"
        "SSID=\"ValidNetwork\"\n"
        "MISSING_EQUAL_SIGN\n"
        "PASSWORD = \"Spaces Before And After\"\n";

    config.parseFromString(malformedData);

    TEST_ASSERT_EQUAL_STRING("ValidNetwork", config.wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("Spaces Before And After", config.wifi.password.c_str());
}

void setup() {
    delay(2000); // Give time for monitor to connect
    UNITY_BEGIN();
    RUN_TEST(test_default_values);
    RUN_TEST(test_parse_valid_ini);
    RUN_TEST(test_parse_malformed_ini);
    UNITY_END();
}

void loop() {
    delay(100);
}
