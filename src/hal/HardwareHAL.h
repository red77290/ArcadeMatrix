#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <mutex>
#include "../include/HardwareProfile.h"

extern std::mutex g_i2cMutex;

/**
 * @enum HwProfile
 * @brief Identifies the hardware profile at compile-time.
 */
enum class HwProfile {
    ESP32_STD,
    WAVESHARE_S3
};

/**
 * @struct AudioCapabilities
 * @brief Runtime snapshot of available audio hardware capabilities.
 */
struct AudioCapabilities {
    bool input = false;          ///< I2S Microphone / ADC available
    bool output = false;         ///< I2S Speaker / DAC available
    bool fullDuplex = false;      ///< Simultaneous RX + TX supported
    uint32_t maxSampleRate = 44100;
    uint8_t maxChannels = 2;
    bool bluetoothClassic = false;
    bool psram = false;
};

/**
 * @struct HardwareCapabilities
 * @brief Runtime snapshot of available hardware capabilities.
 */
struct HardwareCapabilities {
    bool hasPsram = false;
    size_t psramBytes = 0;

    bool hasMicrophone = false;
    bool hasTempSensor = false;
    bool hasGyroscope = false;
    bool hasNetwork = true;
    bool hasSd = true;

    AudioCapabilities audio;

    HwProfile profile = HwProfile::ESP32_STD;
};

/**
 * @struct EnvironmentData
 * @brief Structure containing environmental data (temperature and humidity).
 */
struct EnvironmentData {
    bool available;      ///< True if physical sensor responded successfully
    float temperatureC;  ///< Temperature in degrees Celsius
    float temperatureF;  ///< Temperature in degrees Fahrenheit
    float humidity;      ///< Relative humidity in percentage (0-100%)
};

/**
 * @class HardwareHAL
 * @brief Hardware Abstraction Layer (HAL) for sensors and audio on ArcadeMatrix.
 */
class HardwareHAL {
public:
    HardwareHAL();
    ~HardwareHAL();

    /**
     * @brief Initializes I2C and I2S buses and performs hardware auto-detection.
     */
    void begin();

    // --- Hardware Capabilities Snapshot ---
    const HardwareCapabilities& capabilities() const { return _capabilities; }

    /**
     * @brief Indicates whether a valid environmental sensor was detected on the I2C bus.
     */
    bool isTempSensorAvailable() const { return _capabilities.hasTempSensor; }

    /**
     * @brief Reads environmental data (Celsius, Fahrenheit, Humidity).
     * @param tempOffset Calibration offset in °C
     * @return EnvironmentData structure containing values and status.
     */
    EnvironmentData readEnvironment(float tempOffset = 0.0f);

    // --- Microphone & Audio Input (I2S DMA) ---
    /**
     * @brief Indicates whether the audio / microphone peripheral is available and functional.
     */
    bool isAudioAvailable() const { return _capabilities.hasMicrophone; }

    /**
     * @brief Enables on-demand I2S DMA audio sampling (Lazy Sampling).
     *
     * Registers a standing capture intent and starts the driver when the I2S bus is
     * free. Capture is refused while AudioOutputHAL owns the bus (mic and speaker are
     * mutually exclusive); it then resumes automatically once playback is released.
     */
    void startAudioSampling();

    /**
     * @brief Disables I2S DMA audio sampling to release CPU/DMA resources.
     * @param clearIntent When true (engine deactivation), also drops the standing capture
     *        intent so sampling stays off. When false (AudioHub yielding the bus to
     *        playback), the intent is kept and capture resumes once playback ends.
     */
    void stopAudioSampling(bool clearIntent = true);

    /**
     * @brief Indicates whether I2S audio sampling is currently active.
     */
    bool isAudioSamplingActive() const { return audioActive; }

    /**
     * @brief Indicates whether an engine still needs the microphone, even if the I2S bus
     *        is currently lent to playback. Used to reclaim capture when playback stops.
     */
    bool isAudioCaptureRequested() const { return _captureRequested; }

    /**
     * @brief Calculates and returns the current sound level in decibels (dB SPL).
     * @param dbCalibration Calibration offset in dB
     */
    float getDecibels(float dbCalibration = 0.0f);

    /**
     * @brief Fills an array with FFT frequency band amplitudes (Visualizer).
     * @param bands Target array of size numBands
     * @param numBands Desired number of bands (e.g., 16, 32, 64)
     * @return true if bands were filled successfully
     */
    bool getAudioSpectrum(float* bands, size_t numBands);

    /**
     * @brief Sets microphone gain.
     */
    void setMicGain(float gain) { micGain = (gain > 0.0f) ? gain : 1.0f; }

    /**
     * @brief Returns current microphone gain.
     */
    float getMicGain() const { return micGain; }

    /**
     * @brief Updates gyroscope availability in runtime capabilities.
     */
    void setGyroscopeAvailable(bool avail) { _capabilities.hasGyroscope = avail; }

    /**
     * @brief Indicates whether an IMU / gyroscope sensor was detected on the I2C bus.
     */
    bool isGyroscopeAvailable() const { return _capabilities.hasGyroscope; }

private:
    HardwareCapabilities _capabilities;
    
    // Internal state variables (these can remain for internal workings)
    bool audioSamplingEnabled;
    bool audioActive;
    // Standing intent: an engine that needs the microphone is currently active. Kept
    // separate from audioActive so the bus can be lent to playback and reclaimed
    // afterwards without the engine having to re-arm it.
    bool _captureRequested = false;
    float micGain;

    // Last-good audio frame. An I2S read that returns no bytes is a transient DMA underrun, not
    // silence: zero-filling on that path made the visualizer collapse to a flat line and recover
    // in a loop. The cached frame is replayed with a mild decay so a genuinely dead microphone
    // still fades out instead of freezing.
    static constexpr size_t MAX_SPECTRUM_BANDS = 128;
    float _lastSpectrum[MAX_SPECTRUM_BANDS];
    size_t _lastSpectrumBands;
    float _lastDecibels;
    uint8_t _audioWarmupFrames;

    uint32_t lastTempReadTime;
    EnvironmentData cachedEnvData;

    bool probeSHTC3();
    bool probeES7210();
    bool configureES7210();
    bool readSHTC3Raw(float& tempC, float& hum);
    static uint8_t calcSensirionCRC8(const uint8_t* data, uint8_t len);
};

extern HardwareHAL hardwareHAL;

