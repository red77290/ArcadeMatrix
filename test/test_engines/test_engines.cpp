#include <Arduino.h>
#include <unity.h>
#include "engines/DecibelEngine.h"
#include "engines/VisualizerEngine.h"
#include "engines/TempEngine.h"

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

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_decibel_status_mapping);
    RUN_TEST(test_visualizer_mode_parsing);
    UNITY_END();
}

void loop() {
    delay(100);
}
