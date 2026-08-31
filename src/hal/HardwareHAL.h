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
     */
    void startAudioSampling();

    /**
     * @brief Disables I2S DMA audio sampling to release CPU/DMA resources.
     */
    void stopAudioSampling();

    /**
     * @brief Indicates whether I2S audio sampling is currently active.
     */
    bool isAudioSamplingActive() const { return audioActive; }

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

private:
    HardwareCapabilities _capabilities;
    
    // Internal state variables (these can remain for internal workings)
    bool audioSamplingEnabled;
    bool audioActive;
    float micGain;

    uint32_t lastTempReadTime;
    EnvironmentData cachedEnvData;

    bool probeSHTC3();
    bool probeES7210();
    bool configureES7210();
    bool readSHTC3Raw(float& tempC, float& hum);
    static uint8_t calcSensirionCRC8(const uint8_t* data, uint8_t len);
};

extern HardwareHAL hardwareHAL;

