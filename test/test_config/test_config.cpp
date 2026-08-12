#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <unity.h>
#include "core/ConfigLoader.h"

void setUp(void) {}
void tearDown(void) {}

void test_default_values(void) {
    ConfigLoader config;
    // Verify true defaults from ConfigLoader::setDefaults()
    TEST_ASSERT_EQUAL_INT(64, config.matrix.width);
    TEST_ASSERT_EQUAL_INT(32, config.matrix.height);
    TEST_ASSERT_EQUAL_STRING("FM6126A", config.matrix.panelType.c_str());
    TEST_ASSERT_EQUAL_INT(1, config.matrix.chainLength);
    TEST_ASSERT_EQUAL_INT(100, config.matrix.powerLimitPercent);
    TEST_ASSERT_EQUAL_INT(8, config.matrix.pwmBits);
    TEST_ASSERT_EQUAL_STRING("SHIFTREG", config.matrix.driverChip.c_str());
    TEST_ASSERT_FALSE(config.matrix.forceSingleBuffer);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", config.time.ntpServer.c_str());
    TEST_ASSERT_TRUE(config.time.format24h);
    TEST_ASSERT_EQUAL_STRING("clock,date,weather,gifs,temp,decibel", config.idle.rotation.c_str());
    TEST_ASSERT_TRUE(config.idle.fighter_enabled);
    TEST_ASSERT_EQUAL_INT(8, config.idle.temp_duration_sec);
    TEST_ASSERT_EQUAL_INT(10, config.idle.decibel_duration_sec);
    TEST_ASSERT_EQUAL_STRING("C", config.env.unit.c_str());
    TEST_ASSERT_FALSE(config.audio.visualizer_enabled);
    TEST_ASSERT_EQUAL_STRING("spectrum", config.audio.visualizer_mode.c_str());
}

void test_parse_valid_ini(void) {
    ConfigLoader config;
    const char* iniData = 
        "; This is a comment\n"
        "[WIFI]\n"
        "SSID=\"MyNetwork\"\n"
        "PASSWORD=\"Secret123\"\n"
        "HOSTNAME=\"MyMatrix\"\n"
        "\n"
        "[MATRIX]\n"
        "WIDTH=256\n"
        "HEIGHT=64\n"
        "PANEL_TYPE=FM6126A\n"
        "CHAIN=2\n"
        "BRIGHTNESS_LIMIT=40\n"
        "PWM_BITS=16\n"
        "FORCE_SINGLE_BUFFER=true\n"
        "DRIVER_CHIP=FM6126A\n"
        "\n"
        "[IDLE]\n"
        "ROTATION=\"temp,decibel\"\n"
        "TEMP_DURATION_SEC=12\n"
        "DECIBEL_DURATION_SEC=15\n"
        "\n"
        "[ENVIRONMENT]\n"
        "UNIT=\"F\"\n"
        "TEMP_OFFSET=1.5\n"
        "\n"
        "[AUDIO]\n"
        "VISUALIZER_ENABLED=true\n"
        "VISUALIZER_MODE=\"waveform\"\n"
        "MIC_GAIN=1.8\n"
        "DB_CALIBRATION=2.0\n"
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
        "FORMAT_24H=false\n"
        "CLOCK_THEME=20\n"
        "CLOCK_COLOR_1=\"#FF0000\"\n"
        "CLOCK_COLOR_2=#00FF00\n";

    bool success = config.parseFromString(iniData);
    TEST_ASSERT_TRUE(success);

    // Assert WiFi
    TEST_ASSERT_EQUAL_STRING("MyNetwork", config.wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("Secret123", config.wifi.password.c_str());
    TEST_ASSERT_EQUAL_STRING("MyMatrix", config.wifi.hostname.c_str());

    // Assert Matrix
    TEST_ASSERT_EQUAL_INT(256, config.matrix.width);
    TEST_ASSERT_EQUAL_INT(64, config.matrix.height);
    TEST_ASSERT_EQUAL_STRING("FM6126A", config.matrix.panelType.c_str());
    TEST_ASSERT_EQUAL_INT(2, config.matrix.chainLength);
    TEST_ASSERT_EQUAL_INT(40, config.matrix.powerLimitPercent);
    TEST_ASSERT_EQUAL_INT(16, config.matrix.pwmBits);
    TEST_ASSERT_TRUE(config.matrix.forceSingleBuffer);
    TEST_ASSERT_EQUAL_STRING("FM6126A", config.matrix.driverChip.c_str());

    // Assert Idle, Env, Audio
    TEST_ASSERT_EQUAL_STRING("temp,decibel", config.idle.rotation.c_str());
    TEST_ASSERT_EQUAL_INT(12, config.idle.temp_duration_sec);
    TEST_ASSERT_EQUAL_INT(15, config.idle.decibel_duration_sec);
    TEST_ASSERT_EQUAL_STRING("F", config.env.unit.c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, config.env.temp_offset);
    TEST_ASSERT_TRUE(config.audio.visualizer_enabled);
    TEST_ASSERT_EQUAL_STRING("waveform", config.audio.visualizer_mode.c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.8f, config.audio.mic_gain);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, config.audio.db_calibration);

    // Assert MQTT
    TEST_ASSERT_TRUE(config.mqtt.enabled);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", config.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_INT(1883, config.mqtt.port);
    TEST_ASSERT_EQUAL_STRING("mqtt_user", config.mqtt.user.c_str());
    TEST_ASSERT_EQUAL_STRING("ArcadeMatrix", config.mqtt.deviceName.c_str());

    // Assert Time & Color parsing (both quoted and unquoted # hex colors)
    TEST_ASSERT_EQUAL_STRING("time.google.com", config.time.ntpServer.c_str());
    TEST_ASSERT_FALSE(config.time.format24h);
    TEST_ASSERT_EQUAL_INT(20, config.time.clock_theme);
    TEST_ASSERT_EQUAL_STRING("#FF0000", config.time.clock_color_1.c_str());
    TEST_ASSERT_EQUAL_STRING("#00FF00", config.time.clock_color_2.c_str());
}

void test_legacy_key_fallbacks(void) {
    ConfigLoader config;
    const char* legacyData = 
        "[TIME]\n"
        "NTPSERVER=pool.ntp.org\n"
        "FORMAT24H=true\n";

    bool success = config.parseFromString(legacyData);
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", config.time.ntpServer.c_str());
    TEST_ASSERT_TRUE(config.time.format24h);
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

void test_round_trip_in_memory(void) {
    ConfigLoader original;
    original.wifi.ssid = "RoundTripSSID";
    original.wifi.password = "RoundTripPass";
    original.matrix.width = 128;
    original.matrix.height = 64;
    original.matrix.powerLimitPercent = 75;
    original.time.clock_theme = 15;
    original.time.clock_color_1 = "#123456";
    original.idle.temp_duration_sec = 14;
    original.idle.decibel_duration_sec = 16;
    original.env.unit = "F";
    original.audio.visualizer_enabled = true;
    original.audio.visualizer_mode = "radial";

    TEST_ASSERT_EQUAL_STRING("#123456", original.time.clock_color_1.c_str());
    TEST_ASSERT_EQUAL_STRING("radial", original.audio.visualizer_mode.c_str());
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_default_values);
    RUN_TEST(test_parse_valid_ini);
    RUN_TEST(test_legacy_key_fallbacks);
    RUN_TEST(test_parse_malformed_ini);
    RUN_TEST(test_round_trip_in_memory);
    UNITY_END();
}

void loop() {}
