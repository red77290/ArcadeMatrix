#include <Arduino.h>
#include <unity.h>
#include "core/LayoutHelper.h"
#include "engines/clocks/ClockFaceFont.h"

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
 * @brief Tests LayoutHelper geometry functions (aspectFit, intersectScreen, clampPosition, splits) on 64x32 matrix.
 */
void test_layout_helper_centering(void) {
    Rect screen{ 0, 0, 64, 32 };

    // 48x8 banner inside 64x32 bounds: scale = min(64/48, 32/8) = 1.3333 -> outW = 64, outH = 10, centered Y = (32 - 10)/2 = 11
    Rect fitted = LayoutHelper::aspectFit(48, 8, screen);
    TEST_ASSERT_EQUAL_INT16(0, fitted.x);
    TEST_ASSERT_EQUAL_INT16(11, fitted.y);
    TEST_ASSERT_EQUAL_UINT16(64, fitted.width);
    TEST_ASSERT_EQUAL_UINT16(10, fitted.height);

    // 16x16 square inside 64x32 bounds: scale = 2.0 -> 32x32 centered at x = (64 - 32)/2 = 16, y = 0
    Rect sqFit = LayoutHelper::aspectFit(16, 16, screen);
    TEST_ASSERT_EQUAL_INT16(16, sqFit.x);
    TEST_ASSERT_EQUAL_INT16(0, sqFit.y);
    TEST_ASSERT_EQUAL_UINT16(32, sqFit.width);
    TEST_ASSERT_EQUAL_UINT16(32, sqFit.height);

    // clampPosition translates out-of-bounds rectangle inside screen
    Rect clamped = LayoutHelper::clampPosition(Rect{ 50, 25, 24, 12 }, 64, 32);
    TEST_ASSERT_EQUAL_INT16(40, clamped.x);
    TEST_ASSERT_EQUAL_INT16(20, clamped.y);
    TEST_ASSERT_EQUAL_UINT16(24, clamped.width);
    TEST_ASSERT_EQUAL_UINT16(12, clamped.height);

    // intersectScreen strictly crops rectangle protruding past right/bottom edges
    Rect cropped = LayoutHelper::intersectScreen(Rect{ 48, 20, 32, 20 }, 64, 32);
    TEST_ASSERT_EQUAL_INT16(48, cropped.x);
    TEST_ASSERT_EQUAL_INT16(20, cropped.y);
    TEST_ASSERT_EQUAL_UINT16(16, cropped.width);
    TEST_ASSERT_EQUAL_UINT16(12, cropped.height);

    // Region splits
    Rect top = LayoutHelper::splitTop(screen, 10);
    Rect bottom = LayoutHelper::splitBottom(screen, 10);
    TEST_ASSERT_EQUAL_INT16(0, top.y);
    TEST_ASSERT_EQUAL_UINT16(10, top.height);
    TEST_ASSERT_EQUAL_INT16(22, bottom.y);
    TEST_ASSERT_EQUAL_UINT16(10, bottom.height);
}

/**
 * @brief Tests RGB888 to RGB565 packing and ClockFaceFont::parseHex color conversion.
 */
void test_rgb565_color_math(void) {
    // Pure White: (255, 255, 255) -> 0xFFFF
    uint16_t white = ClockFaceFont::parseHex("#FFFFFF", 0x0000);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, white);

    // Pure Red: (255, 0, 0) -> 0xF800
    uint16_t red = ClockFaceFont::parseHex("#FF0000", 0x0000);
    TEST_ASSERT_EQUAL_HEX16(0xF800, red);

    // Pure Green: (0, 255, 0) -> 0x07E0
    uint16_t green = ClockFaceFont::parseHex("#00FF00", 0x0000);
    TEST_ASSERT_EQUAL_HEX16(0x07E0, green);

    // Pure Blue: (0, 0, 255) -> 0x001F
    uint16_t blue = ClockFaceFont::parseHex("#0000FF", 0x0000);
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
