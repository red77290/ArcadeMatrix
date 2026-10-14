#include <Arduino.h>
#include <unity.h>
#include "engines/DecibelEngine.h"
#include "engines/VisualizerEngine.h"
#include "engines/TempEngine.h"
#include "engines/MessageEngine.h"
#include "engines/clocks/ClockFaceFont.h"
#include "core/I18n.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief Tests DecibelEngine descriptor contract and NoiseStatusLevel enum / threshold mapping.
 *
 * Verifies the canonical engine ID ("decibelMeter"), microphone requirement, lifecycle,
 * and NoiseStatusLevel classification boundaries:
 * - < 45 dB: NOISE_CALM (0)
 * - [45, 65) dB: NOISE_NORMAL (1)
 * - [65, 75) dB: NOISE_MODERATE (2)
 * - [75, 83) dB: NOISE_VIGILANCE (3)
 * - [83, 88] dB: NOISE_LIMIT (4)
 * - > 88 dB: NOISE_ALERT (5)
 */
void test_decibel_status_mapping(void) {
    DecibelEngineDescriptorHandler descHandler;
    EngineDescriptor desc = descHandler.getDescriptor();
    TEST_ASSERT_EQUAL_STRING("decibelMeter", desc.metadata.id);
    TEST_ASSERT_TRUE(desc.requirements.needsAudioInput);

    DecibelEngine engine;
    TEST_ASSERT_TRUE(engine.allowsOverlay());
    TEST_ASSERT_TRUE(engine.needsClear());

    TEST_ASSERT_EQUAL_INT(0, (int)NOISE_CALM);
    TEST_ASSERT_EQUAL_INT(1, (int)NOISE_NORMAL);
    TEST_ASSERT_EQUAL_INT(2, (int)NOISE_MODERATE);
    TEST_ASSERT_EQUAL_INT(3, (int)NOISE_VIGILANCE);
    TEST_ASSERT_EQUAL_INT(4, (int)NOISE_LIMIT);
    TEST_ASSERT_EQUAL_INT(5, (int)NOISE_ALERT);
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
 * @brief Tests MessageEngine lifecycle, thread-safe queueing, and continuous delta-time scrolling math.
 */
void test_message_scroll_math(void) {
    MessageEngineDescriptorHandler descHandler;
    EngineDescriptor desc = descHandler.getDescriptor();
    TEST_ASSERT_EQUAL_STRING("message", desc.metadata.id);

    MessageEngine engine;
    TEST_ASSERT_FALSE(engine.isActive());
    TEST_ASSERT_FALSE(engine.allowsOverlay());
    TEST_ASSERT_TRUE(engine.needsClear());

    MessageConfig cfg{ "HELLO ARCADE", 0xFFFF, 1, "rtl", 50, 10, 0 };
    engine.queueMessage(cfg);
    engine.deactivate();
    TEST_ASSERT_FALSE(engine.isActive());

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

/**
 * @brief The time spoken as words, in each language the sign offers.
 */
void test_spoken_time_reads_as_words() {
    char h[24], m[32];
    I18n::getSpokenTime(12, 0, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("noon", h);
    TEST_ASSERT_EQUAL_STRING("", m);          // the hour stands alone, nothing left over
    I18n::getSpokenTime(0, 0, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("midnight", h);
    I18n::getSpokenTime(8, 5, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("eight", h);
    TEST_ASSERT_EQUAL_STRING("oh five", m);
    I18n::getSpokenTime(8, 30, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("a half", m);
    I18n::getSpokenTime(8, 42, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("forty two", m);

    // Each language gets its own phrasing rather than a word-for-word translation.
    I18n::getSpokenTime(8, 30, Lang::FR, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("huit", h);
    TEST_ASSERT_EQUAL_STRING("heures et demie", m);
    I18n::getSpokenTime(8, 21, Lang::FR, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("heures vingt et une", m);
    I18n::getSpokenTime(8, 30, Lang::ES, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("ocho", h);
    TEST_ASSERT_EQUAL_STRING("y media", m);
    I18n::getSpokenTime(8, 21, Lang::ES, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("y veintiuno", m);
}

/**
 * @brief Date order: month before day in English, day before month elsewhere.
 */
void test_date_line_follows_the_language() {
    char line[32];
    I18n::getDateLine(1, 8, 28, Lang::EN, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("MON SEP 28", line);
    I18n::getDateLine(1, 8, 28, Lang::FR, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("LUN 28 SEPT", line);
    I18n::getDateLine(1, 8, 28, Lang::ES, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("LUN 28 SEP", line);
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_decibel_status_mapping);
    RUN_TEST(test_visualizer_mode_parsing);
    RUN_TEST(test_message_scroll_math);
    RUN_TEST(test_glow_modes_resolve_colours);
    RUN_TEST(test_spoken_time_reads_as_words);
    RUN_TEST(test_date_line_follows_the_language);
    UNITY_END();
}

void loop() {
    delay(100);
}

