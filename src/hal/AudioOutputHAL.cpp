#include "AudioOutputHAL.h"
#include "../core/Logger.h"
#include "../core/SDUtils.h"
#include <math.h>

AudioOutputHAL audioOutputHAL;

#define I2S_TX_PORT I2S_NUM_0
#define DEFAULT_SAMPLE_RATE 44100
#define BUFFER_SAMPLES 1024

// === ES8311 I2C helpers (must be defined before setVolume) ===
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
#include <Wire.h>

static bool writeES8311Reg(uint8_t reg, uint8_t val) {
    std::lock_guard<std::mutex> lock(g_i2cMutex);
    for (int retry = 0; retry < 3; retry++) {
        Wire.beginTransmission(0x18);
        Wire.write(reg);
        Wire.write(val);
        if (Wire.endTransmission() == 0) {
            return true;
        }
        delayMicroseconds(500);
    }
    return false;
}

static uint8_t readES8311Reg(uint8_t reg) {
    std::lock_guard<std::mutex> lock(g_i2cMutex);
    for (int retry = 0; retry < 3; retry++) {
        Wire.beginTransmission(0x18);
        Wire.write(reg);
        if (Wire.endTransmission(false) == 0) {
            if (Wire.requestFrom((uint16_t)0x18, (uint8_t)1, (bool)true) >= 1) {
                return Wire.read();
            }
        }
        delayMicroseconds(500);
    }
    return 0;
}
#endif

AudioOutputHAL::AudioOutputHAL()
    : _initialized(false), _volume(80), _volumeScale(0.8f) {
    updateVolumeScale();
}

AudioOutputHAL::~AudioOutputHAL() {
    stop();
}

void AudioOutputHAL::updateVolumeScale() {
    // Volume is now controlled by ES8311 hardware register 0x32
    // Software _volumeScale is kept only as a fallback for non-Waveshare boards
    if (_volume == 0) {
        _volumeScale = 0.0f;
    } else {
        _volumeScale = (float)_volume / 100.0f;
    }
}

void AudioOutputHAL::setVolume(uint8_t volume) {
    _volume = (volume > 100) ? 100 : volume;
    updateVolumeScale();

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    // Use ES8311 hardware DAC volume register (official Waveshare method)
    // Formula from official driver: reg32 = ((volume * 256) / 100) - 1
    uint8_t reg32 = 0;
    if (_volume > 0) {
        reg32 = (uint8_t)(((_volume * 256) / 100) - 1);
    }
    writeES8311Reg(0x32, reg32);
    writeES8311Reg(0x31, 0x00); // Ensure DAC is unmuted
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);     // Ensure PA is enabled
    LOGI("AudioOutputHAL", "Master volume set to %d%% (ES8311 Reg 0x32: 0x%02X)", _volume, reg32);
#else
    LOGI("AudioOutputHAL", "Master volume set to %d%% (Scale: %.2f)", _volume, _volumeScale);
#endif
}

void AudioOutputHAL::preparePlayback() {
    if (!_initialized) {
        begin();
        return;
    }
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    // 1. Ensure Power Amplifier is active on GPIO 11
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);

    // 2. Unmute ES8311 DAC and re-assert volume
    writeES8311Reg(0x31, 0x00);
    uint8_t reg32 = (_volume > 0) ? (uint8_t)(((_volume * 256) / 100) - 1) : 0;
    writeES8311Reg(0x32, reg32);

    // 3. Clear DMA buffer for clean playback start
    i2s_zero_dma_buffer(I2S_TX_PORT);
#endif
}

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
// Coefficient table matching the official Waveshare ES8311 driver.
// For 44100 Hz with MCLK = 44100*256 = 11289600 Hz:
//   pre_div=1, pre_multi=0, adc_div=1, dac_div=1
//   fs_mode=0, lrck_h=0x00, lrck_l=0xFF, bclk_div=4
//   adc_osr=0x10, dac_osr=0x10
static bool configureES8311DAC() {
    Wire.beginTransmission(0x18);
    if (Wire.endTransmission() != 0) {
        LOGW("AudioOutputHAL", "ES8311 DAC not responding on I2C address 0x18.");
        return false;
    }

    bool ok = true;

    // === STEP 1: Soft Reset (official Waveshare sequence) ===
    ok &= writeES8311Reg(0x00, 0x1F);  // Soft reset
    delay(20);                           // Wait for reset to complete
    ok &= writeES8311Reg(0x00, 0x00);   // Release reset
    ok &= writeES8311Reg(0x00, 0x80);   // Clock auto-detection mode enable
    ok &= writeES8311Reg(0x01, 0x3F);   // Enable all clocks

    // === STEP 2: Clock register 0x06 — clear SCLK invert bit ===
    uint8_t reg06 = readES8311Reg(0x06);
    reg06 &= ~(1U << 5);
    ok &= writeES8311Reg(0x06, reg06);

    // === STEP 3: Clock coefficients for 44100 Hz, MCLK=11289600 ===
    // From official coeff_div table: {11289600, 44100, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10}
    uint8_t reg02 = readES8311Reg(0x02);
    reg02 |= (0x01 - 1) << 5;  // pre_div=1 -> (1-1)<<5 = 0
    reg02 |= 0x00 << 3;        // pre_multi=0
    ok &= writeES8311Reg(0x02, reg02);

    ok &= writeES8311Reg(0x03, (0x00 << 6) | 0x10);  // fs_mode=0 | adc_osr=0x10
    ok &= writeES8311Reg(0x04, 0x10);                  // dac_osr=0x10

    uint8_t reg05 = ((0x01 - 1) << 4) | (0x01 - 1);   // adc_div=1, dac_div=1
    ok &= writeES8311Reg(0x05, reg05);

    reg06 = readES8311Reg(0x06);
    reg06 &= 0xE0;
    reg06 |= (0x04 - 1);  // bclk_div=4 -> 4-1=3
    ok &= writeES8311Reg(0x06, reg06);

    uint8_t reg07 = readES8311Reg(0x07);
    reg07 &= 0xC0;
    reg07 |= 0x00;  // lrck_h=0x00
    ok &= writeES8311Reg(0x07, reg07);
    ok &= writeES8311Reg(0x08, 0xFF);  // lrck_l=0xFF

    // === STEP 4: I2S data format — 16-bit Standard I2S ===
    uint8_t reg09 = readES8311Reg(0x09);
    reg09 |= (3 << 2);  // 16-bit
    ok &= writeES8311Reg(0x09, reg09);

    uint8_t reg0A = readES8311Reg(0x0A);
    reg0A |= (3 << 2);  // 16-bit
    ok &= writeES8311Reg(0x0A, reg0A);

    // === STEP 5: System power & control ===
    ok &= writeES8311Reg(0x0D, 0x01);  // System control
    ok &= writeES8311Reg(0x0E, 0x02);  // System control

    // === STEP 6: DAC power up ===
    ok &= writeES8311Reg(0x12, 0x00);  // Power up DAC & system
    ok &= writeES8311Reg(0x13, 0x10);  // Analog output power (DAC enabled)

    // === STEP 7: Analog config (MISSING in our old code!) ===
    ok &= writeES8311Reg(0x1C, 0x6A);  // ADC/DAC analog config (official value)

    // === STEP 8: DAC output & volume ===
    ok &= writeES8311Reg(0x37, 0x08);  // DAC output power on & differential drive
    ok &= writeES8311Reg(0x31, 0x00);  // DAC Mute OFF (unmuted)
    ok &= writeES8311Reg(0x32, 0xFF);  // DAC Volume: 0xFF = MAXIMUM (was 0x00 = MINIMUM!)

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
    // 1. First start I2S peripheral to generate MCLK clock for ES8311
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = DEFAULT_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = DEFAULT_SAMPLE_RATE * 256
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = 12,   // GPIO 12 - MCLK
        .bck_io_num = 43,   // GPIO 43 - BCLK / SCLK
        .ws_io_num = 38,    // GPIO 38 - LRCK / WS
        .data_out_num = 21, // GPIO 21 - ES8311 DAC DOUT
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_TX_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        LOGE("AudioOutputHAL", "Failed to install I2S TX driver on Waveshare S3! err=%d", err);
        return false;
    }

    i2s_set_pin(I2S_TX_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_TX_PORT);
    i2s_start(I2S_TX_PORT);

    // 2. Wait for MCLK clock to lock on ES8311
    delay(20);

    // 3. Configure ES8311 DAC registers via I2C
    if (configureES8311DAC()) {
        LOGI("AudioOutputHAL", "ES8311 DAC registers configured and unmuted at 0dB.");
    }

    // 4. Enable Power Amplifier (PA) on GPIO 11
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);

    _initialized = true;
    LOGI("AudioOutputHAL", "AudioOutputHAL initialized for Waveshare ESP32-S3 (ES8311 DAC + PA GPIO 11 + I2S DMA TX).");
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
#endif
    i2s_stop(I2S_TX_PORT);
    i2s_driver_uninstall(I2S_TX_PORT);
    _initialized = false;
    LOGI("AudioOutputHAL", "AudioOutputHAL stopped.");
}

size_t AudioOutputHAL::writeSamples(const int16_t* samples, size_t numSamples, TickType_t timeoutTicks) {
    if (!_initialized || !samples || numSamples == 0) return 0;

    size_t totalBytesWritten = 0;
    size_t samplesRemaining = numSamples;
    const int16_t* currentPtr = samples;

    while (samplesRemaining > 0) {
        size_t chunkSamples = (samplesRemaining > (BUFFER_SAMPLES * 2)) ? (BUFFER_SAMPLES * 2) : samplesRemaining;

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
        // On Waveshare: volume is controlled by ES8311 hardware register 0x32
        // Pass PCM samples directly to I2S DMA — no software scaling needed
        // This avoids race conditions and CPU overhead
        size_t bytesWritten = 0;
        esp_err_t err = i2s_write(I2S_TX_PORT, (const void*)currentPtr, chunkSamples * sizeof(int16_t), &bytesWritten, timeoutTicks);
#else
        // Fallback: software volume scaling for generic boards without hardware DAC volume
        if (_volumeScale >= 0.99f) {
            memcpy(_scaledBuffer, currentPtr, chunkSamples * sizeof(int16_t));
        } else {
            for (size_t i = 0; i < chunkSamples; i++) {
                _scaledBuffer[i] = (int16_t)(currentPtr[i] * _volumeScale);
            }
        }

        size_t bytesWritten = 0;
        esp_err_t err = i2s_write(I2S_TX_PORT, (const void*)_scaledBuffer, chunkSamples * sizeof(int16_t), &bytesWritten, timeoutTicks);
#endif
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

    // Temporarily set hardware volume to max for diagnostic
    uint8_t savedVolume = _volume;
    setVolume(100);

    float phase = 0.0f;
    float phaseInc = 2.0f * (float)M_PI * freqHz / (float)sampleRate;

    uint32_t startMs = millis();
    while (millis() - startMs < durationMs) {
        for (size_t i = 0; i < bufferFrames; i++) {
            int16_t sample = (int16_t)(sinf(phase) * 24000.0f);
            buffer[i * 2] = sample;     // Left
            buffer[i * 2 + 1] = sample; // Right
            phase += phaseInc;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        writeSamples(buffer, bufferFrames * 2);
    }
    setVolume(savedVolume);
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
