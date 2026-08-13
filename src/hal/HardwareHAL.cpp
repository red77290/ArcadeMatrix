#include "HardwareHAL.h"
#include "../core/Logger.h"
#include <driver/i2s.h>
#include <math.h>

HardwareHAL hardwareHAL;

#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 22050
#define BUFFER_SIZE 512

HardwareHAL::HardwareHAL() 
    : tempSensorDetected(false), audioDetected(false), audioActive(false), 
      micGain(1.0f), lastTempReadTime(0) {
    cachedEnvData = {false, 0.0f, 32.0f, 0.0f};
}

HardwareHAL::~HardwareHAL() {
    if (audioActive) {
        stopAudioSampling();
    }
}

uint8_t HardwareHAL::calcSensirionCRC8(const uint8_t* data, uint8_t len) {
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

void HardwareHAL::begin() {
    LOGI("HardwareHAL", "Initializing Hardware Abstraction Layer...");

    // 1. Initialize I2C Bus
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000); // 100kHz standard I2C speed

    // 2. Probe Temperature & Humidity Sensor (SHTC3)
    tempSensorDetected = probeSHTC3();
    if (tempSensorDetected) {
        LOGI("HardwareHAL", "SHTC3 Temp/Humidity Sensor DETECTED on I2C address 0x70.");
        readEnvironment(); // Initial reading
    } else {
        LOGW("HardwareHAL", "SHTC3 Temp/Humidity Sensor NOT detected.");
    }

    // 3. Probe Audio Codec / I2S Hardware
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    audioDetected = probeES7210();
    if (audioDetected) {
        LOGI("HardwareHAL", "Waveshare ES7210 Microphone Codec DETECTED on I2C address 0x40.");
    } else {
        LOGW("HardwareHAL", "ES7210 Codec not found; checking generic I2S microphone capability...");
        audioDetected = true; // Fallback to generic I2S mic
    }
#else
    audioDetected = true; // Default ESP32 generic I2S mic profile
#endif

    LOGI("HardwareHAL", "HAL Init complete. Temp Sensor: %s, Audio Hardware: %s",
         tempSensorDetected ? "AVAILABLE" : "NOT DETECTED",
         audioDetected ? "AVAILABLE" : "NOT DETECTED");
}

bool HardwareHAL::probeSHTC3() {
    // SHTC3 Wakeup command: 0x3517
    Wire.beginTransmission(SHTC3_I2C_ADDR);
    Wire.write(0x35);
    Wire.write(0x17);
    if (Wire.endTransmission() != 0) {
        return false;
    }
    delay(1);

    // Read ID command: 0xEFC8
    Wire.beginTransmission(SHTC3_I2C_ADDR);
    Wire.write(0xEF);
    Wire.write(0xC8);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    Wire.requestFrom((uint8_t)SHTC3_I2C_ADDR, (size_t)3);
    if (Wire.available() >= 3) {
        uint8_t id1 = Wire.read();
        uint8_t id2 = Wire.read();
        uint8_t crc = Wire.read();
        
        uint8_t idBuf[2] = { id1, id2 };
        if (calcSensirionCRC8(idBuf, 2) != crc) {
            return false;
        }

        uint16_t id = (id1 << 8) | id2;
        if ((id & 0x083F) == 0x0807) {
            return true;
        }
    }
    return false;
}

bool HardwareHAL::readSHTC3Raw(float& tempC, float& hum) {
    // Wakeup SHTC3
    Wire.beginTransmission(SHTC3_I2C_ADDR);
    Wire.write(0x35);
    Wire.write(0x17);
    if (Wire.endTransmission() != 0) return false;
    delay(1);

    // Send Measurement command (Clock Stretching disabled, Normal mode, T first): 0x7866
    Wire.beginTransmission(SHTC3_I2C_ADDR);
    Wire.write(0x78);
    Wire.write(0x66);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    delay(15); // Wait 15ms for measurement

    Wire.requestFrom((uint8_t)SHTC3_I2C_ADDR, (size_t)6);
    if (Wire.available() < 6) {
        return false;
    }

    uint8_t t1 = Wire.read();
    uint8_t t2 = Wire.read();
    uint8_t tempCrc = Wire.read();

    uint8_t h1 = Wire.read();
    uint8_t h2 = Wire.read();
    uint8_t humCrc = Wire.read();

    // Verify CRC8 for temperature and humidity
    uint8_t tData[2] = { t1, t2 };
    if (calcSensirionCRC8(tData, 2) != tempCrc) return false;

    uint8_t hData[2] = { h1, h2 };
    if (calcSensirionCRC8(hData, 2) != humCrc) return false;

    uint16_t rawTemp = (t1 << 8) | t2;
    uint16_t rawHum = (h1 << 8) | h2;

    // Convert raw values according to Sensirion datasheet
    tempC = -45.0f + 175.0f * ((float)rawTemp / 65535.0f);
    hum = 100.0f * ((float)rawHum / 65535.0f);

    if (hum < 0.0f) hum = 0.0f;
    if (hum > 100.0f) hum = 100.0f;

    // Sleep SHTC3 to conserve power
    Wire.beginTransmission(SHTC3_I2C_ADDR);
    Wire.write(0xB0);
    Wire.write(0x98);
    Wire.endTransmission();

    return true;
}

EnvironmentData HardwareHAL::readEnvironment(float tempOffset) {
    if (!tempSensorDetected) {
        cachedEnvData.available = false;
        return cachedEnvData;
    }

    // Cache reading for 2 seconds to prevent excessive I2C traffic
    if (lastTempReadTime > 0 && (millis() - lastTempReadTime < 2000)) {
        cachedEnvData.temperatureC += tempOffset;
        cachedEnvData.temperatureF = (cachedEnvData.temperatureC * 9.0f / 5.0f) + 32.0f;
        return cachedEnvData;
    }

    float t = 0.0f, h = 0.0f;
    if (readSHTC3Raw(t, h)) {
        cachedEnvData.available = true;
        cachedEnvData.temperatureC = t + tempOffset;
        cachedEnvData.temperatureF = (cachedEnvData.temperatureC * 9.0f / 5.0f) + 32.0f;
        cachedEnvData.humidity = h;
        lastTempReadTime = millis();
    } else {
        cachedEnvData.available = false;
    }
    return cachedEnvData;
}

bool HardwareHAL::probeES7210() {
#if defined(ES7210_I2C_ADDR)
    // 1. Soft Reset
    Wire.beginTransmission(ES7210_I2C_ADDR);
    Wire.write(0x01);
    Wire.write(0x41);
    Wire.endTransmission();
    delay(5);

    Wire.beginTransmission(ES7210_I2C_ADDR);
    Wire.write(0x01);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(5);

    // 2. Register configuration for ES7210 2-channel ADC (Power Up & Gain +30dB)
    uint8_t initCmds[][2] = {
        {0x02, 0xC0}, // Power up ANALOG & ADC
        {0x06, 0x04}, // MCLK/SCLK ratio
        {0x07, 0x00},
        {0x08, 0x00}, // I2S format 16-bit
        {0x09, 0x02}, // I2S mode
        {0x41, 0x70}, // PGA gain +30dB for MIC1/MIC2
        {0x42, 0x70}, // PGA gain +30dB
        {0x43, 0x1E}, // Digital Volume
        {0x44, 0x1E},
        {0x00, 0x01}  // Start ADC conversion
    };

    bool success = true;
    for (size_t i = 0; i < sizeof(initCmds)/sizeof(initCmds[0]); i++) {
        Wire.beginTransmission(ES7210_I2C_ADDR);
        Wire.write(initCmds[i][0]);
        Wire.write(initCmds[i][1]);
        if (Wire.endTransmission() != 0) {
            success = false;
        }
    }
    return success;
#else
    return false;
#endif
}

void HardwareHAL::startAudioSampling() {
    if (audioActive || !audioDetected) return;

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = true,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
#if defined(I2S_MCLK_PIN)
        .mck_io_num = I2S_MCLK_PIN,
#else
        .mck_io_num = I2S_PIN_NO_CHANGE,
#endif
        .bck_io_num = I2S_SCLK_PIN,
        .ws_io_num = I2S_LRCK_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_ASDOUT_PIN
    };

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) == ESP_OK) {
        i2s_set_pin(I2S_PORT, &pin_config);
        i2s_start(I2S_PORT);
        audioActive = true;
        LOGI("HardwareHAL", "I2S DMA Audio Sampling STARTED (MCLK Output Active).");

        // Wait 10ms for MCLK clock signal to stabilize on ES7210 hardware pins
        delay(10);

        // Configure & Power up ES7210 ADC registers NOW while MCLK is active!
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
        if (probeES7210()) {
            LOGI("HardwareHAL", "ES7210 Microphone ADC configured and powered up successfully.");
        } else {
            LOGW("HardwareHAL", "ES7210 I2C config failed, running generic I2S audio mode.");
        }
#endif
    } else {
        LOGE("HardwareHAL", "Failed to install I2S driver!");
    }
}

void HardwareHAL::stopAudioSampling() {
    if (!audioActive) return;

    i2s_stop(I2S_PORT);
    i2s_driver_uninstall(I2S_PORT);
    audioActive = false;
    LOGI("HardwareHAL", "I2S DMA Audio Sampling STOPPED (Lazy Sampling).");
}

float HardwareHAL::getDecibels(float dbCalibration) {
    if (!audioActive) {
        startAudioSampling();
    }

    int16_t sampleBuf[BUFFER_SIZE];
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT, (void*)sampleBuf, sizeof(sampleBuf), &bytesRead, pdMS_TO_TICKS(50));

    if (err != ESP_OK || bytesRead == 0) {
        return 30.0f + dbCalibration; // Silence / fallback minimum
    }

    size_t samplesCount = bytesRead / sizeof(int16_t);
    if (samplesCount == 0) return 30.0f + dbCalibration;

    double sumSquares = 0.0;
    int16_t maxPeak = 0;

    for (size_t i = 0; i < samplesCount; i++) {
        int16_t sVal = sampleBuf[i];
        int16_t absVal = (sVal < 0) ? -sVal : sVal;
        if (absVal > maxPeak) {
            maxPeak = absVal;
        }
        float sample = (float)sVal * micGain;
        sumSquares += ((double)sample * (double)sample);
    }

    float rms = sqrtf((float)(sumSquares / (double)samplesCount));

    // Combine 60% RMS + 40% Peak for instant hand-clap responsiveness
    float peakRatio = (float)maxPeak / 32768.0f;
    float rmsRatio = rms / 32768.0f;
    float combinedRatio = (rmsRatio * 0.6f) + (peakRatio * 0.4f);

    // Convert combined amplitude ratio to calibrated dB SPL range
    float db = 20.0f * log10f(1500.0f * combinedRatio + 1.0f) + 30.0f + dbCalibration;

    if (db < 30.0f) db = 30.0f;
    if (db > 110.0f) db = 110.0f;

    return db;
}

bool HardwareHAL::getAudioSpectrum(float* bands, size_t numBands) {
    if (!bands || numBands == 0) return false;

    if (!audioActive) {
        startAudioSampling();
    }

    int16_t sampleBuf[BUFFER_SIZE];
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, (void*)sampleBuf, sizeof(sampleBuf), &bytesRead, pdMS_TO_TICKS(20));

    size_t samplesCount = bytesRead / sizeof(int16_t);
    if (samplesCount == 0) {
        for (size_t i = 0; i < numBands; i++) bands[i] = 0.0f;
        return false;
    }

    // Partition samples into frequency bands using energy distribution
    size_t samplesPerBand = samplesCount / numBands;
    if (samplesPerBand < 1) samplesPerBand = 1;

    for (size_t b = 0; b < numBands; b++) {
        double bandEnergy = 0.0;
        size_t start = b * samplesPerBand;
        size_t end = (b + 1) * samplesPerBand;
        if (end > samplesCount) end = samplesCount;

        for (size_t i = start; i < end; i++) {
            float val = fabsf((float)sampleBuf[i]) * micGain;
            bandEnergy += val;
        }

        float avgEnergy = (end > start) ? (float)(bandEnergy / (end - start)) : 0.0f;
        // Normalized amplitude (0.0 to 1.0)
        float norm = avgEnergy / 4000.0f;
        if (norm > 1.0f) norm = 1.0f;
        bands[b] = norm;
    }

    return true;
}
