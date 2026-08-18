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
    TEST_ASSERT_EQUAL_INT(128, config.matrix.width);
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
        "VISUALIZER_MODE=waveform\n"
        "MIC_GAIN=1.8\n"
        "DB_CALIBRATION=2.0\n"
        "\n"
        "[MQTT]\n"
        "ENABLED=true\n"
        "BROKER=192.168.1.100\n"
        "PORT=1883\n"
        "USER=mqtt_user\n"
        "DEVICE_NAME=ArcadeMatrix\n"
        "\n"
        "[TIME]\n"
        "NTP_SERVER=time.google.com\n"
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
    TEST_ASSERT_EQUAL_INT(256, config.matrix.width);
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
    
    // 1. Wifi
    original.wifi.ssid = "RoundTripSSID";
    original.wifi.password = "RoundTripPass";
    original.wifi.hostname = "roundtrip-matrix";

    // 2. Matrix
    original.matrix.width = 128;
    original.matrix.height = 64;
    original.matrix.panelType = "ICN2038S";
    original.matrix.chainLength = 3;
    original.matrix.powerLimitPercent = 85;
    original.matrix.width = 128;
    original.matrix.forceSingleBuffer = true;
    original.matrix.rgbSequence = "BGR";
    original.matrix.limitRefreshRateHz = 120;
    original.matrix.driverChip = "FM6126A";

    // 3. Mqtt
    original.mqtt.enabled = true;
    original.mqtt.broker = "10.0.0.50";
    original.mqtt.port = 8883;
    original.mqtt.user = "admin";
    original.mqtt.pass = "secret";
    original.mqtt.deviceName = "RTMatrix";
    original.mqtt.topic_batocera = "bat/playing";
    original.mqtt.topic_recalbox = "rec/playing";

    // 4. Time
    original.time.ntpServer = "time.cloudflare.com";
    original.time.timezone = "EST5EDT";
    original.time.format24h = false;
    original.time.clock_font = 2;
    original.time.clock_size = 3;
    original.time.clock_theme = 18;
    original.time.clock_offset_x = -2;
    original.time.clock_offset_y = 4;
    original.time.clock_color_1 = "#123456";
    original.time.clock_color_2 = "#654321";
    original.time.clock_font_path = "/fonts/clock.font";

    // 5. Idle
    original.idle.rotation = "clock,crypto,date,stock,temp,decibel";
    original.idle.clock_duration_sec = 25;
    original.idle.date_duration_sec = 12;
    original.idle.weather_duration_sec = 14;
    original.idle.temp_duration_sec = 9;
    original.idle.decibel_duration_sec = 11;
    original.idle.gifs_count = 7;
    original.idle.fighter_enabled = false;
    original.idle.fighter_interval_sec = 20;

    // 6. Environment
    original.env.unit = "F";
    original.env.temp_offset = -1.25f;

    // 7. Audio
    original.audio.visualizer_enabled = true;
    original.audio.visualizer_mode = "radial";
    original.audio.mic_gain = 2.5f;
    original.audio.db_calibration = 3.5f;

    // 8. DateSettings
    original.dateSettings.theme = 2;
    original.dateSettings.background_sprite = "bg_test.raw";
    original.dateSettings.format = "MM/DD";
    original.dateSettings.date_font = 1;
    original.dateSettings.date_size = 2;
    original.dateSettings.date_offset_x = 3;
    original.dateSettings.date_offset_y = -1;
    original.dateSettings.date_color_1 = "#ABCDEF";
    original.dateSettings.date_color_2 = "#FEDCBA";
    original.dateSettings.date_font_path = "/fonts/date.font";

    // 9. Weather
    original.weather.api_key = "test_api_key_123";
    original.weather.city = "Lyon,FR";
    original.weather.lang = "fr";
    original.weather.weather_offset_x = 1;
    original.weather.weather_offset_y = -2;

    // 10. Standby
    original.standby.night_mode_enabled = true;
    original.standby.turn_off_at = "22:30";
    original.standby.wake_up_at = "06:30";
    original.standby.night_brightness = 5;

    // 11. Fonts
    original.fonts.custom_font_path = "/fonts/custom.bdf";

    // 12. Crypto
    original.crypto.enabled = true;
    original.crypto.symbols = "BTC,ETH,SOL";
    original.crypto.duration_sec = 8;
    original.crypto.cache_ttl_min = 2;
    original.crypto.currency = "EUR";

    // 13. Stock
    original.stock.enabled = true;
    original.stock.symbols = "AAPL,MSFT";
    original.stock.duration_sec = 6;
    original.stock.cache_ttl_min = 3;

    // Production serialize -> parse
    String serialized = original.serializeToString();
    ConfigLoader reloaded;
    bool parseSuccess = reloaded.parseFromString(serialized.c_str());
    TEST_ASSERT_TRUE(parseSuccess);

    // Assert ALL 13 structs field-by-field
    // 1. Wifi
    TEST_ASSERT_EQUAL_STRING(original.wifi.ssid.c_str(), reloaded.wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(original.wifi.password.c_str(), reloaded.wifi.password.c_str());
    TEST_ASSERT_EQUAL_STRING(original.wifi.hostname.c_str(), reloaded.wifi.hostname.c_str());

    // 2. Matrix
    TEST_ASSERT_EQUAL_INT(original.matrix.width, reloaded.matrix.width);
    TEST_ASSERT_EQUAL_INT(original.matrix.height, reloaded.matrix.height);
    TEST_ASSERT_EQUAL_STRING(original.matrix.panelType.c_str(), reloaded.matrix.panelType.c_str());
    TEST_ASSERT_EQUAL_INT(original.matrix.chainLength, reloaded.matrix.chainLength);
    TEST_ASSERT_EQUAL_INT(original.matrix.powerLimitPercent, reloaded.matrix.powerLimitPercent);
    TEST_ASSERT_EQUAL_INT(original.matrix.width, reloaded.matrix.width);
    TEST_ASSERT_EQUAL(original.matrix.forceSingleBuffer, reloaded.matrix.forceSingleBuffer);
    TEST_ASSERT_EQUAL_STRING(original.matrix.rgbSequence.c_str(), reloaded.matrix.rgbSequence.c_str());
    TEST_ASSERT_EQUAL_INT(original.matrix.limitRefreshRateHz, reloaded.matrix.limitRefreshRateHz);
    TEST_ASSERT_EQUAL_STRING(original.matrix.driverChip.c_str(), reloaded.matrix.driverChip.c_str());

    // 3. Mqtt
    TEST_ASSERT_EQUAL(original.mqtt.enabled, reloaded.mqtt.enabled);
    TEST_ASSERT_EQUAL_STRING(original.mqtt.broker.c_str(), reloaded.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_INT(original.mqtt.port, reloaded.mqtt.port);
    TEST_ASSERT_EQUAL_STRING(original.mqtt.user.c_str(), reloaded.mqtt.user.c_str());
    TEST_ASSERT_EQUAL_STRING(original.mqtt.pass.c_str(), reloaded.mqtt.pass.c_str());
    TEST_ASSERT_EQUAL_STRING(original.mqtt.deviceName.c_str(), reloaded.mqtt.deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING(original.mqtt.topic_batocera.c_str(), reloaded.mqtt.topic_batocera.c_str());
    TEST_ASSERT_EQUAL_STRING(original.mqtt.topic_recalbox.c_str(), reloaded.mqtt.topic_recalbox.c_str());

    // 4. Time
    TEST_ASSERT_EQUAL_STRING(original.time.ntpServer.c_str(), reloaded.time.ntpServer.c_str());
    TEST_ASSERT_EQUAL_STRING(original.time.timezone.c_str(), reloaded.time.timezone.c_str());
    TEST_ASSERT_EQUAL(original.time.format24h, reloaded.time.format24h);
    TEST_ASSERT_EQUAL_INT(original.time.clock_font, reloaded.time.clock_font);
    TEST_ASSERT_EQUAL_INT(original.time.clock_size, reloaded.time.clock_size);
    TEST_ASSERT_EQUAL_INT(original.time.clock_theme, reloaded.time.clock_theme);
    TEST_ASSERT_EQUAL_INT(original.time.clock_offset_x, reloaded.time.clock_offset_x);
    TEST_ASSERT_EQUAL_INT(original.time.clock_offset_y, reloaded.time.clock_offset_y);
    TEST_ASSERT_EQUAL_STRING(original.time.clock_color_1.c_str(), reloaded.time.clock_color_1.c_str());
    TEST_ASSERT_EQUAL_STRING(original.time.clock_color_2.c_str(), reloaded.time.clock_color_2.c_str());
    TEST_ASSERT_EQUAL_STRING(original.time.clock_font_path.c_str(), reloaded.time.clock_font_path.c_str());

    // 5. Idle
    TEST_ASSERT_EQUAL_STRING(original.idle.rotation.c_str(), reloaded.idle.rotation.c_str());
    TEST_ASSERT_EQUAL_INT(original.idle.clock_duration_sec, reloaded.idle.clock_duration_sec);
    TEST_ASSERT_EQUAL_INT(original.idle.date_duration_sec, reloaded.idle.date_duration_sec);
    TEST_ASSERT_EQUAL_INT(original.idle.weather_duration_sec, reloaded.idle.weather_duration_sec);
    TEST_ASSERT_EQUAL_INT(original.idle.temp_duration_sec, reloaded.idle.temp_duration_sec);
    TEST_ASSERT_EQUAL_INT(original.idle.decibel_duration_sec, reloaded.idle.decibel_duration_sec);
    TEST_ASSERT_EQUAL_INT(original.idle.gifs_count, reloaded.idle.gifs_count);
    TEST_ASSERT_EQUAL(original.idle.fighter_enabled, reloaded.idle.fighter_enabled);
    TEST_ASSERT_EQUAL_INT(original.idle.fighter_interval_sec, reloaded.idle.fighter_interval_sec);

    // 6. Env
    TEST_ASSERT_EQUAL_STRING(original.env.unit.c_str(), reloaded.env.unit.c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, original.env.temp_offset, reloaded.env.temp_offset);

    // 7. Audio
    TEST_ASSERT_EQUAL(original.audio.visualizer_enabled, reloaded.audio.visualizer_enabled);
    TEST_ASSERT_EQUAL_STRING(original.audio.visualizer_mode.c_str(), reloaded.audio.visualizer_mode.c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, original.audio.mic_gain, reloaded.audio.mic_gain);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, original.audio.db_calibration, reloaded.audio.db_calibration);

    // 8. DateSettings
    TEST_ASSERT_EQUAL_INT(original.dateSettings.theme, reloaded.dateSettings.theme);
    TEST_ASSERT_EQUAL_STRING(original.dateSettings.background_sprite.c_str(), reloaded.dateSettings.background_sprite.c_str());
    TEST_ASSERT_EQUAL_STRING(original.dateSettings.format.c_str(), reloaded.dateSettings.format.c_str());
    TEST_ASSERT_EQUAL_INT(original.dateSettings.date_font, reloaded.dateSettings.date_font);
    TEST_ASSERT_EQUAL_INT(original.dateSettings.date_size, reloaded.dateSettings.date_size);
    TEST_ASSERT_EQUAL_INT(original.dateSettings.date_offset_x, reloaded.dateSettings.date_offset_x);
    TEST_ASSERT_EQUAL_INT(original.dateSettings.date_offset_y, reloaded.dateSettings.date_offset_y);
    TEST_ASSERT_EQUAL_STRING(original.dateSettings.date_color_1.c_str(), reloaded.dateSettings.date_color_1.c_str());
    TEST_ASSERT_EQUAL_STRING(original.dateSettings.date_color_2.c_str(), reloaded.dateSettings.date_color_2.c_str());
    TEST_ASSERT_EQUAL_STRING(original.dateSettings.date_font_path.c_str(), reloaded.dateSettings.date_font_path.c_str());

    // 9. Weather
    TEST_ASSERT_EQUAL_STRING(original.weather.api_key.c_str(), reloaded.weather.api_key.c_str());
    TEST_ASSERT_EQUAL_STRING(original.weather.city.c_str(), reloaded.weather.city.c_str());
    TEST_ASSERT_EQUAL_STRING(original.weather.lang.c_str(), reloaded.weather.lang.c_str());
    TEST_ASSERT_EQUAL_INT(original.weather.weather_offset_x, reloaded.weather.weather_offset_x);
    TEST_ASSERT_EQUAL_INT(original.weather.weather_offset_y, reloaded.weather.weather_offset_y);

    // 10. Standby
    TEST_ASSERT_EQUAL(original.standby.night_mode_enabled, reloaded.standby.night_mode_enabled);
    TEST_ASSERT_EQUAL_STRING(original.standby.turn_off_at.c_str(), reloaded.standby.turn_off_at.c_str());
    TEST_ASSERT_EQUAL_STRING(original.standby.wake_up_at.c_str(), reloaded.standby.wake_up_at.c_str());
    TEST_ASSERT_EQUAL_INT(original.standby.night_brightness, reloaded.standby.night_brightness);

    // 11. Fonts
    TEST_ASSERT_EQUAL_STRING(original.fonts.custom_font_path.c_str(), reloaded.fonts.custom_font_path.c_str());

    // 12. Crypto
    TEST_ASSERT_EQUAL(original.crypto.enabled, reloaded.crypto.enabled);
    TEST_ASSERT_EQUAL_STRING(original.crypto.symbols.c_str(), reloaded.crypto.symbols.c_str());
    TEST_ASSERT_EQUAL_INT(original.crypto.duration_sec, reloaded.crypto.duration_sec);
    TEST_ASSERT_EQUAL_INT(original.crypto.cache_ttl_min, reloaded.crypto.cache_ttl_min);
    TEST_ASSERT_EQUAL_STRING(original.crypto.currency.c_str(), reloaded.crypto.currency.c_str());

    // 13. Stock
    TEST_ASSERT_EQUAL(original.stock.enabled, reloaded.stock.enabled);
    TEST_ASSERT_EQUAL_STRING(original.stock.symbols.c_str(), reloaded.stock.symbols.c_str());
    TEST_ASSERT_EQUAL_INT(original.stock.duration_sec, reloaded.stock.duration_sec);
    TEST_ASSERT_EQUAL_INT(original.stock.cache_ttl_min, reloaded.stock.cache_ttl_min);
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
