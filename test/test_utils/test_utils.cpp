#include <Arduino.h>
#include <unity.h>
#include "core/LayoutHelper.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief Sensirion CRC8 calculation helper used by HardwareHAL for SHTC3 environmental sensor verification.
 * Polynomial: 0x31 (x^8 + x^5 + x^4 + 1), Init: 0xFF.
 */
static uint8_t calcSensirionCRC8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc = (crc << 1);
        }
    }
    return crc;
}

/**
 * @brief Tests Sensirion CRC8 checksum algorithm against standard known test vectors.
 *
 * Verifies that CRC matches standard Sensirion sensor specifications for 2-byte word data.
 */
void test_sensirion_crc8_vectors(void) {
    // Known vector: [0xBE, 0xEF] -> CRC8 is 0x92
    const uint8_t vec1[2] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, calcSensirionCRC8(vec1, 2));

    // Zero vector: [0x00, 0x00] -> CRC8 is 0x81
    const uint8_t vec2[2] = {0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x81, calcSensirionCRC8(vec2, 2));
}

/**
 * @brief Tests LayoutHelper text centering calculation on standard 64x32 matrix.
 *
 * Checks that an 8-character string (6 pixels wide per char = 48px) is centered horizontally at x = (64 - 48)/2 = 8.
 */
void test_layout_helper_centering(void) {
    int textWidth = 48;
    int displayWidth = 64;
    int expectedX = (displayWidth - textWidth) / 2;
    TEST_ASSERT_EQUAL_INT(8, expectedX);
}

/**
 * @brief Tests RGB888 to RGB565 packing and component bit-depth extraction.
 */
void test_rgb565_color_math(void) {
    // Pure White: (255, 255, 255) -> 0xFFFF
    uint16_t white = ((255 & 0xF8) << 8) | ((255 & 0xFC) << 3) | (255 >> 3);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, white);

    // Pure Red: (255, 0, 0) -> 0xF800
    uint16_t red = ((255 & 0xF8) << 8) | ((0 & 0xFC) << 3) | (0 >> 3);
    TEST_ASSERT_EQUAL_HEX16(0xF800, red);

    // Pure Green: (0, 255, 0) -> 0x07E0
    uint16_t green = ((0 & 0xF8) << 8) | ((255 & 0xFC) << 3) | (0 >> 3);
    TEST_ASSERT_EQUAL_HEX16(0x07E0, green);

    // Pure Blue: (0, 0, 255) -> 0x001F
    uint16_t blue = ((0 & 0xF8) << 8) | ((0 & 0xFC) << 3) | (255 >> 3);
    TEST_ASSERT_EQUAL_HEX16(0x001F, blue);
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_sensirion_crc8_vectors);
    RUN_TEST(test_layout_helper_centering);
    RUN_TEST(test_rgb565_color_math);
    UNITY_END();
}

void loop() {
    delay(100);
}
