#include "HardwareHAL.h"
#include "../core/Logger.h"
#include <driver/i2s.h>
#include <math.h>

HardwareHAL hardwareHAL;
std::mutex g_i2cMutex;

#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 22050
#define BUFFER_SIZE 512

HardwareHAL::HardwareHAL() 
    : audioActive(false), 
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

    // 1. Initialize I2C Bus & Scan Devices
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000); // 100kHz standard I2C speed
    Wire.setTimeOut(25);   // 25ms timeout to prevent peripheral lockups

    String i2cLog = "I2C Bus Scan: ";
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            i2cLog += "0x" + String(addr, HEX) + " ";
        }
    }
    LOGI("HardwareHAL", "%s", i2cLog.c_str());

    // 2. Probe Temperature & Humidity Sensor (SHTC3)
    _capabilities.hasTempSensor = probeSHTC3();
    if (_capabilities.hasTempSensor) {
        LOGI("HardwareHAL", "SHTC3 Temp/Humidity Sensor DETECTED on I2C address 0x70.");
        readEnvironment(); // Initial reading
    } else {
        LOGW("HardwareHAL", "SHTC3 Temp/Humidity Sensor NOT detected.");
    }

    // 3. Probe Audio Codec / I2S Hardware
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    _capabilities.hasMicrophone = probeES7210();
    if (_capabilities.hasMicrophone) {
        LOGI("HardwareHAL", "Waveshare ES7210 Microphone Codec DETECTED on I2C address 0x40.");
    } else {
        LOGW("HardwareHAL", "ES7210 Codec not found; checking generic I2S microphone capability...");
        _capabilities.hasMicrophone = true; // Fallback to generic I2S mic
    }
    _capabilities.audio.input = _capabilities.hasMicrophone;
    _capabilities.audio.output = true; // ES8311 DAC on GPIO 21
    _capabilities.audio.fullDuplex = true;
    _capabilities.audio.maxSampleRate = 44100;
    _capabilities.audio.maxChannels = 2;
    _capabilities.audio.bluetoothClassic = false; // S3 is BLE only
#else
    _capabilities.hasMicrophone = true; // Default ESP32 generic I2S mic profile
    _capabilities.audio.input = true;
    _capabilities.audio.output = true; // External I2S DAC (MAX98357A / PCM5102A)
    _capabilities.audio.fullDuplex = false;
    _capabilities.audio.maxSampleRate = 44100;
    _capabilities.audio.maxChannels = 2;
    _capabilities.audio.bluetoothClassic = true; // ESP32 Standard supports Classic BT A2DP Sink
#endif

    LOGI("HardwareHAL", "HAL Init complete. Temp Sensor: %s, Audio Input: %s, Audio Output: %s (Full-Duplex: %s)",
         _capabilities.hasTempSensor ? "AVAILABLE" : "NOT DETECTED",
         _capabilities.audio.input ? "YES" : "NO",
         _capabilities.audio.output ? "YES" : "NO",
         _capabilities.audio.fullDuplex ? "YES" : "NO");

    // Populate Capabilities Snapshot
    _capabilities.hasGyroscope = false;

    if (psramFound()) {
        _capabilities.hasPsram = true;
        _capabilities.psramBytes = ESP.getPsramSize();
    } else {
        _capabilities.hasPsram = false;
        _capabilities.psramBytes = 0;
    }
    _capabilities.audio.psram = _capabilities.hasPsram;

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    _capabilities.profile = HwProfile::WAVESHARE_S3;
#else
    _capabilities.profile = HwProfile::ESP32_STD;
#endif
}

bool HardwareHAL::probeSHTC3() {
    std::lock_guard<std::mutex> lock(g_i2cMutex);
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
    {
        std::lock_guard<std::mutex> lock(g_i2cMutex);
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
    }

    delay(15); // Wait 15ms for measurement (mutex released so other I2C users can proceed)

    uint8_t t1, t2, tempCrc, h1, h2, humCrc;
    {
        std::lock_guard<std::mutex> lock(g_i2cMutex);
        Wire.requestFrom((uint8_t)SHTC3_I2C_ADDR, (size_t)6);
        if (Wire.available() < 6) {
            return false;
        }

        t1 = Wire.read();
        t2 = Wire.read();
        tempCrc = Wire.read();

        h1 = Wire.read();
        h2 = Wire.read();
        humCrc = Wire.read();

        // Sleep SHTC3 to conserve power
        Wire.beginTransmission(SHTC3_I2C_ADDR);
        Wire.write(0xB0);
        Wire.write(0x98);
        Wire.endTransmission();
    }

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

    return true;
}

EnvironmentData HardwareHAL::readEnvironment(float tempOffset) {
    if (!_capabilities.hasTempSensor) {
        cachedEnvData.available = false;
        return cachedEnvData;
    }

    // Rate-limit physical I2C reads to once every 2 seconds to avoid self-heating
    if (lastTempReadTime == 0 || (millis() - lastTempReadTime >= 2000)) {
        float t = 0.0f, h = 0.0f;
        if (readSHTC3Raw(t, h)) {
            cachedEnvData.available = true;
            cachedEnvData.temperatureC = t; // Store raw temperature
            cachedEnvData.humidity = h;
            lastTempReadTime = millis();
        } else {
            cachedEnvData.available = false;
        }
    }

    // Apply offset on a copy to prevent cumulative drifting in cache
    EnvironmentData result = cachedEnvData;
    if (result.available) {
        result.temperatureC += tempOffset;
        result.temperatureF = (result.temperatureC * 9.0f / 5.0f) + 32.0f;
    }
    return result;

}

bool HardwareHAL::probeES7210() {
#if defined(ES7210_I2C_ADDR)
    Wire.beginTransmission(ES7210_I2C_ADDR);
    return (Wire.endTransmission() == 0);
#else
    return false;
#endif
}

bool HardwareHAL::configureES7210() {
#if defined(ES7210_I2C_ADDR)
    // 0. If ES8311 DAC is present at I2C address 0x18, power up its I2S clock interface to release shared bus
    Wire.beginTransmission(0x18);
    if (Wire.endTransmission() == 0) {
        LOGI("HardwareHAL", "ES8311 DAC detected on I2C address 0x18; powering up shared clock bus...");
        Wire.beginTransmission(0x18);
        Wire.write(0x00);
        Wire.write(0x80); // Reset ES8311
        Wire.endTransmission();
        delay(5);
        Wire.beginTransmission(0x18);
        Wire.write(0x00);
        Wire.write(0x00); // Exit Reset & Power Up ES8311 shared clocks
        Wire.endTransmission();
        delay(5);
    }

    // 1. Soft Reset ES7210
    Wire.beginTransmission(ES7210_I2C_ADDR);
    Wire.write(0x00);
    Wire.write(0xFF);
    Wire.endTransmission();
    delay(10);

    Wire.beginTransmission(ES7210_I2C_ADDR);
    Wire.write(0x00);
    Wire.write(0x32);
    Wire.endTransmission();
    delay(10);

    // 2. Official esp_codec_dev ES7210 Register sequence for 16kHz, 16-bit, I2S Master/Slave
    uint8_t initCmds[][2] = {
        // Initialization time
        {0x09, 0x30}, // TIME_CONTROL0
        {0x0A, 0x30}, // TIME_CONTROL1
        
        // HPF Configuration
        {0x23, 0x2A}, // ADC12_HPF1
        {0x22, 0x0A}, // ADC12_HPF2
        {0x21, 0x2A}, // ADC34_HPF1
        {0x20, 0x0A}, // ADC34_HPF2
        
        // I2S format (16-bit, standard, TDM disabled)
        {0x11, 0x62}, // 0x60 (16-bit) | 0x02 (Standard I2S)
        {0x12, 0x00}, // TDM disabled
        
        // Analog power and VMID voltage
        {0x40, 0xC3},
        
        // MIC bias 2.87V
        {0x41, 0x70},
        {0x42, 0x70},
        
        // MIC gain 30dB (0x0A | 0x10 = 0x1A)
        {0x43, 0x1A},
        {0x44, 0x1A},
        {0x45, 0x1A},
        {0x46, 0x1A},
        
        // Power on MIC1-4
        {0x47, 0x08},
        {0x48, 0x08},
        {0x49, 0x08},
        {0x4A, 0x08},
        
        // Set ADC sample rate (16kHz, mclk=16000*256=4096000)
        // From es7210 coeff div table: osr=0x20, adc_div=1, doubler=1, dll=1, lrck_h=1, lrck_l=0
        {0x07, 0x20}, // OSR
        {0x02, 0xC1}, // MAINCLK: adc_div(1) | doubler(1<<6) | dll(1<<7) = 0xC1
        {0x04, 0x01}, // LRCK_DIVH
        {0x05, 0x00}, // LRCK_DIVL
        
        // Power down DLL
        {0x06, 0x04},
        
        // Power on MIC1-4 bias & ADC1-4 & PGA1-4 Power
        {0x4B, 0x0F},
        {0x4C, 0x0F},
        
        // Volume 0dB (191 = 0xBF)
        {0x1B, 0xBF},
        {0x1C, 0xBF},
        {0x1D, 0xBF},
        {0x1E, 0xBF},
        
        // Enable device
        {0x00, 0x71},
        {0x00, 0x41}
    };

    bool success = true;
    for (size_t i = 0; i < sizeof(initCmds)/sizeof(initCmds[0]); i++) {
        uint8_t reg = initCmds[i][0];
        uint8_t val = initCmds[i][1];

        Wire.beginTransmission(ES7210_I2C_ADDR);
        Wire.write(reg);
        Wire.write(val);
        if (Wire.endTransmission() != 0) {
            success = false;
            LOGE("HardwareHAL", "ES7210 Reg 0x%02X write FAIL!", reg);
        }
    }
    return success;
#else
    return false;
#endif
}

void HardwareHAL::startAudioSampling() {
    if (audioActive || !_capabilities.hasMicrophone) return;

    // 0. Enable Power Amplifier circuit on GPIO 11 (shared audio power rail on Waveshare board)
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);
    delay(10);
    LOGI("HardwareHAL", "Audio PA enabled on GPIO 11.");
#endif

    // 1. Full-duplex I2S config (TX+RX) - matches official Waveshare BSP
    //    The ES7210 (ADC) and ES8311 (DAC) share the same I2S bus.
    //    Both channels must be active for proper clock generation.
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = 16000,  // Match BSP default: 16kHz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = true,       // APLL for precise MCLK generation
        .tx_desc_auto_clear = true,
        .fixed_mclk = 16000 * 256  // 4.096 MHz MCLK
    };

    i2s_pin_config_t pin_config = {
#if defined(I2S_MCLK_PIN)
        .mck_io_num = I2S_MCLK_PIN,     // GPIO 12
#else
        .mck_io_num = I2S_PIN_NO_CHANGE,
#endif
        .bck_io_num = I2S_SCLK_PIN,     // GPIO 43
        .ws_io_num = I2S_LRCK_PIN,      // GPIO 38
        .data_out_num = 21,              // GPIO 21 - ES8311 DAC data (BSP_I2S_DOUT)
        .data_in_num = I2S_ASDOUT_PIN   // GPIO 39 - ES7210 ADC data (BSP_I2S_DSIN)
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err == ESP_OK) {
        err = i2s_set_pin(I2S_PORT, &pin_config);
        if (err != ESP_OK) {
            LOGE("HardwareHAL", "i2s_set_pin failed: %d", err);
        }
        i2s_zero_dma_buffer(I2S_PORT);
        i2s_start(I2S_PORT);
        audioActive = true;
#if defined(I2S_MCLK_PIN)
        LOGI("HardwareHAL", "I2S DMA Audio STARTED (Full-Duplex TX+RX, APLL, MCLK on GPIO %d).", I2S_MCLK_PIN);
#else
        LOGI("HardwareHAL", "I2S DMA Audio STARTED (Full-Duplex TX+RX, APLL).");
#endif
        // Wait for MCLK to stabilize
        delay(50);

        // Configure & Power up ES7210 ADC registers NOW while MCLK is active!
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
        if (configureES7210()) {
            LOGI("HardwareHAL", "ES7210 Microphone ADC configured and powered up successfully.");
        } else {
            LOGW("HardwareHAL", "ES7210 I2C config failed, running generic I2S audio mode.");
        }
#endif
        // Flush initial stale DMA data
        int16_t dummyBuf[BUFFER_SIZE];
        size_t dummyRead = 0;
        for (int i = 0; i < 8; i++) {
            i2s_read(I2S_PORT, (void*)dummyBuf, sizeof(dummyBuf), &dummyRead, pdMS_TO_TICKS(20));
        }
        LOGI("HardwareHAL", "I2S DMA buffer flushed (%d dummy reads).", 8);
    } else {
        LOGE("HardwareHAL", "Failed to install I2S driver! err=%d", err);
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
    i2s_read(I2S_PORT, (void*)sampleBuf, sizeof(sampleBuf), &bytesRead, pdMS_TO_TICKS(50));

    if (bytesRead == 0) {
        return 30.0f + dbCalibration; // Silence / fallback minimum when no bytes read
    }

    size_t samplesCount = bytesRead / sizeof(int16_t);
    if (samplesCount == 0) return 30.0f + dbCalibration;
    double sum = 0.0;
    double sumSquares = 0.0;
    int16_t maxPeak = 0;

    // First pass: find DC offset (mean)
    for (size_t i = 0; i < samplesCount; i++) {
        sum += sampleBuf[i];
    }
    float dcOffset = sum / samplesCount;

    // Second pass: remove DC offset, compute RMS and maxPeak
    for (size_t i = 0; i < samplesCount; i++) {
        float sample = ((float)sampleBuf[i] - dcOffset) * micGain;
        float absVal = fabsf(sample);
        
        if (absVal > maxPeak) {
            maxPeak = (int16_t)absVal;
        }
        sumSquares += (sample * sample);
    }

    static int warmupFrames = 0;
    if (warmupFrames < 4) {
        warmupFrames++;
        return 30.0f + dbCalibration;
    }

    float rms = sqrtf((float)(sumSquares / (double)samplesCount));

    // True decibel conversion relative to 16-bit Full Scale (32768)
    // 20 * log10(RMS / 32768) gives a range from -90 dB (silence) to 0 dB (clipping)
    // We shift this so that clipping is around 120 dB SPL.
    float db = 30.0f; // default silence
    if (rms > 1.0f) {
        // Offset of 120dB for Full Scale
        db = 20.0f * log10f(rms / 32768.0f) + 120.0f;
    }
    
    // Apply user calibration
    db += dbCalibration;

    // Floor the output to 30dB (absolute silence in a quiet room)
    if (db < 30.0f) db = 30.0f;
    if (db > 110.0f) db = 110.0f;

    static unsigned long lastAudioLog = 0;
    if (millis() - lastAudioLog > 1000) {
        lastAudioLog = millis();
        LOGI("HardwareHAL", "I2S Audio Debug: bytesRead=%d, maxPeak=%d, rms=%.1f, calcDb=%.1f dB | PCM: [%d, %d, %d, %d, %d, %d, %d, %d]",
             (int)bytesRead, (int)maxPeak, rms, db,
             sampleBuf[0], sampleBuf[1], sampleBuf[2], sampleBuf[3],
             sampleBuf[4], sampleBuf[5], sampleBuf[6], sampleBuf[7]);
    }

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

    // First pass: find DC offset
    double sum = 0.0;
    for (size_t i = 0; i < samplesCount; i++) {
        sum += sampleBuf[i];
    }
    float dcOffset = sum / samplesCount;

    // Partition samples into frequency bands using energy distribution
    size_t samplesPerBand = samplesCount / numBands;
    if (samplesPerBand < 1) samplesPerBand = 1;

    for (size_t b = 0; b < numBands; b++) {
        double bandEnergy = 0.0;
        size_t start = b * samplesPerBand;
        size_t end = (b + 1) * samplesPerBand;
        if (end > samplesCount) end = samplesCount;

        for (size_t i = start; i < end; i++) {
            float val = fabsf((float)sampleBuf[i] - dcOffset) * micGain;
            bandEnergy += val;
        }

        float avgEnergy = (end > start) ? (float)(bandEnergy / (end - start)) : 0.0f;
        // Dynamic amplitude normalization (0.0 to 1.0)
        float norm = avgEnergy / 1500.0f;
        if (norm > 1.0f) norm = 1.0f;
        bands[b] = norm;
    }

    return true;
}
