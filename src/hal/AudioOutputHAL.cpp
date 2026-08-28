#include "AudioOutputHAL.h"
#include "../core/Logger.h"
#include "../core/SDUtils.h"
#include <math.h>

AudioOutputHAL audioOutputHAL;

#define I2S_TX_PORT I2S_NUM_0
#define DEFAULT_SAMPLE_RATE 44100
#define BUFFER_SAMPLES 512

AudioOutputHAL::AudioOutputHAL()
    : _initialized(false), _volume(80), _volumeScale(0.8f) {
    updateVolumeScale();
}

AudioOutputHAL::~AudioOutputHAL() {
    stop();
}

void AudioOutputHAL::updateVolumeScale() {
    if (_volume == 0) {
        _volumeScale = 0.0f;
    } else {
        // Logarithmic volume perception: scale = (10^(vol/50) - 1) / (10^2 - 1)
        float normalized = (float)_volume / 100.0f;
        _volumeScale = normalized * normalized; // Quadratic approximation of human ear response
    }
}

void AudioOutputHAL::setVolume(uint8_t volume) {
    _volume = (volume > 100) ? 100 : volume;
    updateVolumeScale();
    LOGI("AudioOutputHAL", "Master volume set to %d%% (Scale: %.2f)", _volume, _volumeScale);
}

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
#include <Wire.h>

static bool writeES8311Reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(0x18);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

static bool configureES8311DAC() {
    Wire.beginTransmission(0x18);
    if (Wire.endTransmission() != 0) {
        LOGW("AudioOutputHAL", "ES8311 DAC not responding on I2C address 0x18.");
        return false;
    }

    uint8_t initCmds[][2] = {
        {0x00, 0x80}, // Reset ES8311
        {0x00, 0x00}, // Release Reset
        {0x01, 0x30}, // Clock Manager: Master/Slave
        {0x02, 0x00}, // Pre-scaler
        {0x03, 0x10}, // DAC SCLK/LRCK divider
        {0x04, 0x10}, // ADC SCLK/LRCK divider
        {0x05, 0x00}, // Clock Manager
        {0x06, 0x00}, // System control
        {0x07, 0x00}, // System control
        {0x08, 0xFF}, // System control
        {0x09, 0x0C}, // SDPIN: 16-bit Standard I2S
        {0x0A, 0x0C}, // SDPOUT: 16-bit Standard I2S
        {0x0D, 0x01}, // System control
        {0x0E, 0x02}, // System control
        {0x12, 0x00}, // System control
        {0x13, 0x10}, // Analog Power
        {0x14, 0x1A}, // Analog Power & VMID
        {0x31, 0x00}, // DAC Mute OFF
        {0x32, 0xBF}, // DAC Volume 0dB (0xBF = 191)
        {0x37, 0x08}  // DAC Output Power On
    };

    bool ok = true;
    for (size_t i = 0; i < sizeof(initCmds)/sizeof(initCmds[0]); i++) {
        if (!writeES8311Reg(initCmds[i][0], initCmds[i][1])) {
            ok = false;
        }
    }
    return ok;
}
#endif

bool AudioOutputHAL::begin() {
    if (_initialized) return true;

    // Check hardware profile and capabilities
    const auto& caps = hardwareHAL.capabilities();
    if (!caps.audio.output) {
        LOGW("AudioOutputHAL", "Audio output DAC not supported on this hardware configuration.");
        return false;
    }

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    if (configureES8311DAC()) {
        LOGI("AudioOutputHAL", "ES8311 DAC registers configured and unmuted.");
    }
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH); // Enable Power Amplifier
    _initialized = true;
    LOGI("AudioOutputHAL", "AudioOutputHAL initialized for Waveshare ESP32-S3 (ES8311 DAC + PA GPIO 11).");
    return true;
#else
    // Generic ESP32 standard configuration
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = DEFAULT_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    // Default pins for external DAC (MAX98357A / PCM5102A) on standard ESP32
    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = 26,  // BCLK
        .ws_io_num = 25,   // LRCK / WS
        .data_out_num = 22, // DOUT / DIN on DAC
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_TX_PORT, &i2s_config, 0, NULL);
    if (err == ESP_OK) {
        i2s_set_pin(I2S_TX_PORT, &pin_config);
        i2s_zero_dma_buffer(I2S_TX_PORT);
        i2s_start(I2S_TX_PORT);
        _initialized = true;
        LOGI("AudioOutputHAL", "AudioOutputHAL initialized on standard ESP32 (I2S TX Port %d).", I2S_TX_PORT);
        return true;
    } else {
        LOGE("AudioOutputHAL", "Failed to initialize I2S TX driver! err=%d", err);
        return false;
    }
#endif
}

void AudioOutputHAL::stop() {
    if (!_initialized) return;

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    pinMode(11, OUTPUT);
    digitalWrite(11, LOW); // Mute PA
#else
    i2s_stop(I2S_TX_PORT);
    i2s_driver_uninstall(I2S_TX_PORT);
#endif
    _initialized = false;
    LOGI("AudioOutputHAL", "AudioOutputHAL stopped.");
}

size_t AudioOutputHAL::writeSamples(const int16_t* samples, size_t numSamples, TickType_t timeoutTicks) {
    if (!_initialized || !samples || numSamples == 0) return 0;

    int16_t scaledBuffer[BUFFER_SAMPLES * 2];
    size_t totalBytesWritten = 0;
    size_t samplesRemaining = numSamples;
    const int16_t* currentPtr = samples;

    while (samplesRemaining > 0) {
        size_t chunkSamples = (samplesRemaining > (BUFFER_SAMPLES * 2)) ? (BUFFER_SAMPLES * 2) : samplesRemaining;

        // Apply software volume attenuation
        if (_volumeScale >= 0.99f) {
            memcpy(scaledBuffer, currentPtr, chunkSamples * sizeof(int16_t));
        } else {
            for (size_t i = 0; i < chunkSamples; i++) {
                scaledBuffer[i] = (int16_t)(currentPtr[i] * _volumeScale);
            }
        }

        size_t bytesWritten = 0;
        esp_err_t err = i2s_write(I2S_TX_PORT, (const void*)scaledBuffer, chunkSamples * sizeof(int16_t), &bytesWritten, timeoutTicks);
        if (err != ESP_OK || bytesWritten == 0) {
            break;
        }

        totalBytesWritten += bytesWritten;
        size_t samplesSent = bytesWritten / sizeof(int16_t);
        samplesRemaining -= samplesSent;
        currentPtr += samplesSent;
    }

    return totalBytesWritten;
}

void AudioOutputHAL::playSine(float freqHz, uint32_t durationMs) {
    if (!_initialized && !begin()) return;

    LOGI("AudioOutputHAL", "Playing Sine Wave POC (%.1f Hz, %u ms)...", freqHz, durationMs);

    const size_t sampleRate = 44100;
    const size_t bufferFrames = 256;
    int16_t buffer[bufferFrames * 2]; // Stereo interleaved

    float phase = 0.0f;
    float phaseInc = 2.0f * (float)M_PI * freqHz / (float)sampleRate;

    uint32_t startMs = millis();
    while (millis() - startMs < durationMs) {
        for (size_t i = 0; i < bufferFrames; i++) {
            int16_t sample = (int16_t)(sinf(phase) * 16000.0f);
            buffer[i * 2] = sample;     // Left
            buffer[i * 2 + 1] = sample; // Right
            phase += phaseInc;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        writeSamples(buffer, bufferFrames * 2);
    }
    LOGI("AudioOutputHAL", "Sine Wave POC finished.");
}

bool AudioOutputHAL::playWav(const char* filepath) {
    if (!_initialized && !begin()) return false;
    LOGI("AudioOutputHAL", "Playing WAV file: %s", filepath);

    // Simple WAV header parsing (44-byte standard PCM header)
    FsFile f = sd.open(filepath, FILE_OPEN_READ);
    if (!f) {
        LOGE("AudioOutputHAL", "Failed to open WAV file: %s", filepath);
        return false;
    }

    uint8_t header[44];
    if (f.read(header, 44) < 44) {
        f.close();
        return false;
    }

    // Verify RIFF & WAVE markers
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        LOGE("AudioOutputHAL", "Not a valid RIFF/WAVE file!");
        f.close();
        return false;
    }

    int16_t buffer[BUFFER_SAMPLES * 2];
    while (f.available()) {
        size_t bytesRead = f.read((uint8_t*)buffer, sizeof(buffer));
        if (bytesRead == 0) break;
        writeSamples(buffer, bytesRead / sizeof(int16_t));
    }

    f.close();
    LOGI("AudioOutputHAL", "WAV playback finished.");
    return true;
}
