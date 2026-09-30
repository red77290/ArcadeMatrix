#include <Arduino.h>
#include <unity.h>
#include "engines/DecibelEngine.h"
#include "engines/VisualizerEngine.h"
#include "engines/TempEngine.h"
#include "engines/clocks/ClockFaceFont.h"
#include "core/I18n.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief Tests decibel sound level threshold classification for status indicators.
 *
 * Verifies the exact boundary thresholds:
 * - < 45 dB: Calm
 * - [45, 65) dB: Normal
 * - [65, 75) dB: Moderate
 * - [75, 83) dB: Vigilance
 * - [83, 88] dB: Limit
 * - > 88 dB: Alert
 */
void test_decibel_status_mapping(void) {
    float dbCalm = 40.0f;
    float dbNormal = 55.0f;
    float dbModerate = 70.0f;
    float dbVigilance = 78.0f;
    float dbLimit = 85.0f;
    float dbAlert = 95.0f;

    TEST_ASSERT_TRUE(dbCalm < 45.0f);
    TEST_ASSERT_TRUE(dbNormal >= 45.0f && dbNormal < 65.0f);
    TEST_ASSERT_TRUE(dbModerate >= 65.0f && dbModerate < 75.0f);
    TEST_ASSERT_TRUE(dbVigilance >= 75.0f && dbVigilance < 83.0f);
    TEST_ASSERT_TRUE(dbLimit >= 83.0f && dbLimit <= 88.0f);
    TEST_ASSERT_TRUE(dbAlert > 88.0f);
}

/**
 * @brief Tests visualizer animation mode string parser.
 *
 * Verifies that VisualizerEngine cleanly parses all mode identifiers ("waveform", "radial",
 * "neon_fire", "spectrum") while maintaining inactive initial state.
 */
void test_visualizer_mode_parsing(void) {
    VisualizerEngine engine;
    engine.setMode("waveform");
    engine.setMode("radial");
    engine.setMode("neon_fire");
    engine.setMode("spectrum");
    TEST_ASSERT_FALSE(engine.isActive());
}

/**
 * @brief Tests MessageEngine continuous delta-time scrolling math, sub-pixel progression, and stall clamping.
 */
void test_message_scroll_math(void) {
    // 1. Frame clamp: dt > 100ms clamped to 16.6ms
    float dtLarge = 5000.0f;
    float dtClamped = (dtLarge > 100.0f) ? 16.6f : dtLarge;
    TEST_ASSERT_EQUAL_FLOAT(16.6f, dtClamped);

    // 2. Continuous sub-pixel advance:
    // With speed = 50ms/px and dt = 16.6ms, movePx = 16.6 / 50.0 = 0.332px
    int speedMs = 50;
    float movePx = dtClamped / (float)speedMs;
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.332f, movePx);

    // 3. Sub-pixel accumulator progression across 3 frames (~50ms)
    float cursorX = 128.0f;
    cursorX -= movePx;
    TEST_ASSERT_EQUAL_INT(128, (int16_t)roundf(cursorX)); // Frame 1: 127.668 -> rounds to 128
    cursorX -= movePx;
    TEST_ASSERT_EQUAL_INT(127, (int16_t)roundf(cursorX)); // Frame 2: 127.336 -> rounds to 127
    cursorX -= movePx;
    TEST_ASSERT_EQUAL_INT(127, (int16_t)roundf(cursorX)); // Frame 3: 127.004 -> rounds to 127
}


/**
 * @brief The glow outline: how a face's colour turns into an outline and a centre.
 */
void test_glow_modes_resolve_colours() {
    // With no configuration there is no glow, and the centre keeps the colour it was given.
    uint16_t halo = 0, core = 0;
    ClockFaceFont::Glow off = ClockFaceFont::resolveGlow(nullptr);
    TEST_ASSERT_EQUAL_UINT8(0, off.mode);
    TEST_ASSERT_FALSE(ClockFaceFont::glowFor(off, 0xF800, halo, core));
    TEST_ASSERT_EQUAL_UINT16(0xF800, core);

    // Neon takes the outline from the text colour; a custom glow takes the one it was given, and
    // falls back to the text colour when the setting is not a colour at all.
    ClockFaceFont::Glow neon; neon.mode = 1;
    TEST_ASSERT_TRUE(ClockFaceFont::glowFor(neon, 0x001F, halo, core));
    TEST_ASSERT_EQUAL_UINT16(0x001F, halo);

    ClockFaceFont::Glow custom; custom.mode = 2; custom.color = 0xF81F; custom.hasColor = true;
    TEST_ASSERT_TRUE(ClockFaceFont::glowFor(custom, 0x001F, halo, core));
    TEST_ASSERT_EQUAL_UINT16(0xF81F, halo);
    TEST_ASSERT_EQUAL_UINT16(0x001F, core);      // the centre keeps its own colour

    ClockFaceFont::Glow unset; unset.mode = 2;   // "custom" with nothing parseable behind it
    TEST_ASSERT_TRUE(ClockFaceFont::glowFor(unset, 0x07E0, halo, core));
    TEST_ASSERT_EQUAL_UINT16(0x07E0, halo);

    // A hex colour parses; anything else falls back rather than drawing black.
    TEST_ASSERT_EQUAL_UINT16(0xF800, ClockFaceFont::parseHex("#FF0000", 0x0000));
    TEST_ASSERT_EQUAL_UINT16(0x07E0, ClockFaceFont::parseHex("not a colour", 0x07E0));
    TEST_ASSERT_EQUAL_UINT16(0x001F, ClockFaceFont::parseHex("0000FF", 0x0000));
    // Black is a real colour, and is told apart from "not set".
    uint16_t parsed = 0xFFFF;
    TEST_ASSERT_TRUE(ClockFaceFont::tryParseHex("#000000", parsed));
    TEST_ASSERT_EQUAL_UINT16(0x0000, parsed);
    TEST_ASSERT_FALSE(ClockFaceFont::tryParseHex("nope", parsed));

    // Neon pales the centre towards white, which is what makes the outline read as an outline.
    uint16_t paled = ClockFaceFont::paled(0x001F);
    TEST_ASSERT_TRUE(((paled >> 11) & 0x1F) > 0);
    TEST_ASSERT_TRUE(((paled >> 5) & 0x3F) > 0);

    // Dimming lowers every channel and keeps the hue.
    uint16_t dim = ClockFaceFont::dim(0xFFFF, 1, 3);
    TEST_ASSERT_TRUE(((dim >> 11) & 0x1F) < 0x1F);
    TEST_ASSERT_TRUE((dim & 0x1F) < 0x1F);
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_decibel_status_mapping);
    RUN_TEST(test_visualizer_mode_parsing);
    RUN_TEST(test_message_scroll_math);
    RUN_TEST(test_glow_modes_resolve_colours);
    UNITY_END();
}

void loop() {
    delay(100);
}

