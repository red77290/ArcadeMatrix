#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include "HardwareHAL.h"

/**
 * @class AudioOutputHAL
 * @brief Hardware Abstraction Layer for I2S Digital-to-Analog Converter (DAC) Audio Output.
 * Handles PCM sample streaming, software volume attenuation, and hardware safety.
 */
class AudioOutputHAL {
public:
    AudioOutputHAL();
    ~AudioOutputHAL();

    /**
     * @brief Initializes I2S DAC output driver according to board capabilities.
     * @return true if I2S TX driver initialized successfully.
     */
    bool begin();

    /**
     * @brief Prepares hardware for audio playback (unmutes DAC, enables PA, clears DMA buffer).
     */
    void preparePlayback();

    /**
     * @brief Stops I2S DAC output and releases resources.
     */
    void stop();

    /**
     * @brief Writes raw 16-bit stereo PCM samples to the I2S DMA buffer.
     * @param samples Pointer to 16-bit interleaved stereo or mono PCM buffer.
     * @param numSamples Number of 16-bit sample words.
     * @param timeoutTicks FreeRTOS ticks timeout for I2S DMA write.
     * @return Number of bytes successfully written to DMA.
     */
    size_t writeSamples(const int16_t* samples, size_t numSamples, TickType_t timeoutTicks = pdMS_TO_TICKS(100));

    /**
     * @brief Sets master output volume (0-100%) applying a logarithmic curve.
     */
    void setVolume(uint8_t volume);

    /**
     * @brief Returns current master volume (0-100%).
     */
    uint8_t getVolume() const { return _volume; }

    /**
     * @brief Indicates if audio output DAC is available and initialized.
     */
    bool isAvailable() const { return _initialized; }

    /**
     * @brief Hardware POC Step 1: Generates and plays a pure sine wave tone.
     * @param freqHz Frequency in Hertz (e.g. 440.0f for A4)
     * @param durationMs Duration in milliseconds
     */
    void playSine(float freqHz = 440.0f, uint32_t durationMs = 1000);

    /**
     * @brief Hardware POC Step 2: Plays an uncompressed 16-bit PCM WAV file from SD/LittleFS.
     * @param filepath Path to .wav file
     * @return true if file was opened and streamed successfully.
     */
    bool playWav(const char* filepath);

private:
    bool _initialized;
    uint8_t _volume;
    float _volumeScale; // 0.0f to 1.0f (logarithmic scale)
    int16_t _scaledBuffer[1024];

    void updateVolumeScale();
};

extern AudioOutputHAL audioOutputHAL;
