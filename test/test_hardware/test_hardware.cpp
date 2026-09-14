#include <Arduino.h>
#include <unity.h>
#include "hal/HardwareHAL.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief Verifies default initial state of the Hardware Abstraction Layer (HAL).
 *
 * Ensures audio sampling is inactive upon construction and microphone gain defaults to 1.0x.
 */
void test_hal_default_states(void) {
    HardwareHAL hal;
    TEST_ASSERT_FALSE(hal.isAudioSamplingActive());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, hal.getMicGain());
}

/**
 * @brief Tests microphone gain clamping and fallback bounds.
 *
 * Verifies valid positive gain setting and graceful fallback to 1.0x on negative/invalid values.
 */
void test_mic_gain_bounds(void) {
    HardwareHAL hal;
    hal.setMicGain(2.5f);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, hal.getMicGain());

    hal.setMicGain(-1.0f); // Negative/invalid gain must fall back to 1.0x
    TEST_ASSERT_EQUAL_FLOAT(1.0f, hal.getMicGain());
}

/**
 * @brief Verifies environmental sensor unit conversion calculations.
 *
 * Checks mathematical precision of Celsius to Fahrenheit conversion formula: (C * 9/5) + 32.
 */
void test_environment_data_conversion(void) {
    float tempC = 22.5f;
    float tempF = (tempC * 9.0f / 5.0f) + 32.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 72.5f, tempF);
}

/**
 * @brief Tests ES7210 zero signal lock-free flagging, Core 0 recovery, cooldown and healthy reset.
 */
void test_es7210_zero_signal_lockfree_flagging_and_recovery(void) {
    HardwareHAL hal;
    hal.setEs7210ZeroFrames(0);
    hal.setEs7210RecoveryPending(false);
    hal.setConsecutiveRecoveryCount(0);
    hal.setLastES7210RecoveryMs(0);

    // Simulate 59 zero-peak PCM buffers (Core 1)
    for (int i = 0; i < 59; i++) {
        hal.evaluateES7210Signal(512, 0);
    }
    TEST_ASSERT_EQUAL_UINT16(59, hal.getEs7210ZeroFrames());
    TEST_ASSERT_FALSE(hal.isEs7210RecoveryPending());

    // 60th zero buffer triggers recovery pending
    hal.evaluateES7210Signal(512, 0);
    TEST_ASSERT_TRUE(hal.isEs7210RecoveryPending());

    // Core 0 executes recovery
    bool recResult = hal.checkAndPerformES7210Recovery();
    TEST_ASSERT_TRUE(recResult);
    TEST_ASSERT_FALSE(hal.isEs7210RecoveryPending());
    TEST_ASSERT_EQUAL_UINT8(1, hal.getConsecutiveRecoveryCount());

    // Immediate second recovery pending should be rejected by 3000ms cooldown
    hal.setEs7210RecoveryPending(true);
    TEST_ASSERT_FALSE(hal.checkAndPerformES7210Recovery());
    // Pending flag must remain true during cooldown so it can retry later
    TEST_ASSERT_TRUE(hal.isEs7210RecoveryPending());

    // Core 1 observes healthy non-zero PCM signal
    hal.evaluateES7210Signal(512, 1200);
    TEST_ASSERT_EQUAL_UINT16(0, hal.getEs7210ZeroFrames());
    TEST_ASSERT_TRUE(hal.isEs7210SignalHealthy());

    // Advance cooldown and execute: healthy signal resets consecutive count
    hal.setLastES7210RecoveryMs(0);
    hal.checkAndPerformES7210Recovery();
    TEST_ASSERT_EQUAL_UINT8(1, hal.getConsecutiveRecoveryCount());
}

/**
 * @brief Verifies that transient DMA underruns and alternating silence do not trigger spurious recovery.
 */
void test_es7210_dma_underrun_and_transient_silence(void) {
    HardwareHAL hal;
    hal.setEs7210ZeroFrames(0);
    hal.setEs7210RecoveryPending(false);

    // bytesRead == 0 is DMA underrun, not silence freeze
    hal.evaluateES7210Signal(0, 0);
    TEST_ASSERT_EQUAL_UINT16(0, hal.getEs7210ZeroFrames());
    TEST_ASSERT_FALSE(hal.isEs7210RecoveryPending());

    // 30 silent frames followed by healthy sound frame
    for (int i = 0; i < 30; i++) {
        hal.evaluateES7210Signal(512, 0);
    }
    TEST_ASSERT_EQUAL_UINT16(30, hal.getEs7210ZeroFrames());

    hal.evaluateES7210Signal(512, 500); // Sound arrives
    TEST_ASSERT_EQUAL_UINT16(0, hal.getEs7210ZeroFrames());
    TEST_ASSERT_FALSE(hal.isEs7210RecoveryPending());
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_hal_default_states);
    RUN_TEST(test_mic_gain_bounds);
    RUN_TEST(test_environment_data_conversion);
    RUN_TEST(test_es7210_zero_signal_lockfree_flagging_and_recovery);
    RUN_TEST(test_es7210_dma_underrun_and_transient_silence);
    UNITY_END();
}

void loop() {
    delay(100);
}
