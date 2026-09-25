#include "HardwareHAL.h"
#include "AudioOutputHAL.h"
#include "GyroHAL.h"
#include "../core/Logger.h"
#include "../services/FFT64.h"
#include <driver/i2s.h>
#include <math.h>

HardwareHAL hardwareHAL;
std::mutex g_i2cMutex;

#define I2S_PORT I2S_NUM_0
// The ES7210 register block below is the official 16 kHz coefficient set, and the codec requires a
// fixed 256*Fs MCLK (4.096 MHz). Changing this constant without re-deriving the register sequence
// de-tunes the ADC and the microphone goes silent.
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 512

// DMA ring geometry. i2s_read() can only hand back data once a descriptor has been completed by
// the DMA engine, so the descriptor period is the hard floor of the read latency. With the former
// 4 x 512-frame layout a descriptor took 512/16000 = 32 ms to fill, which is longer than the read
// timeout used by the visualizer: every time the ring ran dry the read returned zero bytes and the
// spectrum collapsed, then recovered once the ring refilled. Shorter descriptors make data
// available every 8 ms while keeping the same 64 ms of total buffering.
#define I2S_DMA_DESC_COUNT 8
#define I2S_DMA_FRAMES_PER_DESC 128

// Read budget on the Core 1 render path. i2s_read() returns partial data on timeout, which is
// perfectly usable for time-domain banding, so these stay well under one frame period.
#define AUDIO_SPECTRUM_READ_TIMEOUT_MS 20
#define AUDIO_DECIBEL_READ_TIMEOUT_MS 20

static_assert(SAMPLE_RATE * 256 == 4096000,
              "ES7210 requires a fixed 4.096 MHz MCLK (256 * 16 kHz); update configureES7210() "
              "register coefficients before changing SAMPLE_RATE.");

static_assert(I2S_DMA_FRAMES_PER_DESC * 1000 / SAMPLE_RATE < AUDIO_SPECTRUM_READ_TIMEOUT_MS,
              "One DMA descriptor must complete faster than the spectrum read timeout, otherwise "
              "reads periodically return zero bytes and the visualizer oscillates.");

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
// The Waveshare S3 board carries a single physical microphone capsule (ES7210 MIC1), but
// the I2S peripheral is configured I2S_CHANNEL_FMT_RIGHT_LEFT (stereo) purely so both the
// RX (mic) and TX (speaker) sides of the full-duplex bus share one MCLK/BCLK generator.
// Every consumer therefore receives interleaved [L, R, L, R, ...] 16-bit words where only
// one slot carries the real acoustic signal.
//
// Feeding that interleaved stream straight into a time-domain analysis (RMS, or worse, an
// FFT window) silently splices two unrelated "channels" together: an FFT window ends up
// with 32 real samples and 32 samples from the other slot, which corrupts every bin above
// DC. This compacts the buffer in place to the left slot only, halving the sample count.
static size_t extractMonoChannel(int16_t* buf, size_t samplesCount) {
    size_t monoCount = samplesCount / 2;
    for (size_t i = 0; i < monoCount; i++) {
        buf[i] = buf[i * 2];
    }
    return monoCount;
}
#endif

HardwareHAL::HardwareHAL() 
    : audioActive(false), 
      micGain(1.0f), lastTempReadTime(0),
      _lastSpectrumBands(0), _lastDecibels(30.0f), _audioWarmupFrames(0) {
    cachedEnvData = {false, 0.0f, 32.0f, 0.0f};
    for (size_t i = 0; i < MAX_SPECTRUM_BANDS; i++) _lastSpectrum[i] = 0.0f;
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

#if defined(HARDWARE_PROFILE_WAVESHARE_S3) && !defined(UNIT_TEST)
static TaskHandle_t s_es7210RecoveryTaskHandle = nullptr;

static void es7210RecoveryTaskFunc(void* param) {
    auto* hal = static_cast<HardwareHAL*>(param);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        hal->checkAndPerformES7210Recovery();
    }
}
#endif

void HardwareHAL::begin() {
    LOGI("HardwareHAL", "Initializing Hardware Abstraction Layer...");

#if !defined(HARDWARE_PROFILE_WAVESHARE_S3)
    constexpr bool i2cConflictsWithMatrix = (I2C_SDA_PIN == MATRIX_E_PIN || I2C_SDA_PIN == MATRIX_C_PIN ||
                                             I2C_SCL_PIN == MATRIX_E_PIN || I2C_SCL_PIN == MATRIX_C_PIN);
#else
    constexpr bool i2cConflictsWithMatrix = false;
#endif

    if (!i2cConflictsWithMatrix) {
        // 0. I2C Bus Recovery: if an external peripheral (QMI8658, ES7210, SHTC3) was interrupted
        // mid-transfer during a software reset (e.g. OTA reboot), SDA may remain held LOW by the slave.
        // Pulse SCL up to 9 clock cycles to let the slave finish its byte and release SDA.
        pinMode(I2C_SDA_PIN, INPUT_PULLUP);
        pinMode(I2C_SCL_PIN, OUTPUT_OPEN_DRAIN);
        digitalWrite(I2C_SCL_PIN, HIGH);
        delayMicroseconds(10);

        if (digitalRead(I2C_SDA_PIN) == LOW) {
            LOGW("HardwareHAL", "I2C SDA line held low on boot, pulsing SCL to recover bus...");
            for (int i = 0; i < 9 && digitalRead(I2C_SDA_PIN) == LOW; i++) {
                digitalWrite(I2C_SCL_PIN, LOW);
                delayMicroseconds(5);
                digitalWrite(I2C_SCL_PIN, HIGH);
                delayMicroseconds(5);
            }
            // Generate an explicit I2C STOP condition (SDA low -> high while SCL is high)
            pinMode(I2C_SDA_PIN, OUTPUT_OPEN_DRAIN);
            digitalWrite(I2C_SDA_PIN, LOW);
            delayMicroseconds(5);
            digitalWrite(I2C_SCL_PIN, HIGH);
            delayMicroseconds(5);
            digitalWrite(I2C_SDA_PIN, HIGH);
            delayMicroseconds(5);
            if (digitalRead(I2C_SDA_PIN) == HIGH) {
                LOGI("HardwareHAL", "I2C bus recovered successfully.");
            } else {
                LOGE("HardwareHAL", "I2C bus recovery failed: SDA still held LOW.");
            }
        }

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
    } else {
        LOGI("HardwareHAL", "I2C bus skipped: GPIO %d/%d assigned to HUB75 Matrix C/E lines.",
             I2C_SDA_PIN, I2C_SCL_PIN);
        _capabilities.hasTempSensor = false;
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

#if !defined(UNIT_TEST)
    if (_capabilities.hasMicrophone && s_es7210RecoveryTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            es7210RecoveryTaskFunc,
            "ES7210Rec",
            2560,
            this,
            1,
            &s_es7210RecoveryTaskHandle,
            0 // Core 0
        );
    }
#endif
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

    // 4. Probe Gyroscope / Accelerometer (QMI8658 / MPU6050)
    if (!i2cConflictsWithMatrix) {
        gyroHAL.begin();
        _capabilities.hasGyroscope = gyroHAL.isAvailable();
    } else {
        _capabilities.hasGyroscope = false;
    }

    // Populate Capabilities Snapshot
    _capabilities.hasNetwork = true;
    _capabilities.hasSd = true;

    if (psramFound()) {
        _capabilities.hasPsram = true;
        _capabilities.psramBytes = ESP.getPsramSize();
    } else {
        _capabilities.hasPsram = false;
        _capabilities.psramBytes = 0;
    }
    _capabilities.audio.psram = _capabilities.hasPsram;
    // NOTE: mbedTLS intentionally uses the stock ESP-IDF/Arduino allocator (100% internal DRAM,
    // as in v3.1.0). Live hardware testing proved that ANY mbedTLS allocation routed to PSRAM --
    // even only the large ~16KB TLS record buffers via a size threshold -- causes the HUB75
    // matrix display to go blank within seconds. Root cause: this board's framebuffer is also
    // PSRAM-resident (build_flags: -D SPIRAM_DMA_BUFFER, required because moving it to internal
    // DRAM costs ~64KB of internal DRAM this board does not have to spare). ESP32-S3's PSRAM
    // (per ESP-IDF docs) shares its cache with large-chunk (>32KB) access causing slow/evicted
    // cache lines; mbedTLS's ~32KB combined in/out record buffers are exactly this kind of large,
    // bursty access, and contending with the HUB75 GDMA engine's continuous PSRAM reads for the
    // framebuffer corrupts/stalls the display. Display integrity takes priority over TLS
    // reliability: TLS fetches that fail due to internal DRAM pressure degrade gracefully
    // (cached values are kept, see DashboardDataProvider/YahooFinanceProvider/BinanceProvider),
    // whereas a corrupted display cannot recover without a reboot. See NetworkBudget.h for the
    // admission-control gate and ScopedTlsHandshakeLock, which mitigate internal DRAM pressure by
    // serializing TLS handshakes system-wide instead of spilling to PSRAM.

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

    delay(15); // Wait 15ms for measurement with I2C bus safely released

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
    std::lock_guard<std::mutex> lock(g_i2cMutex);
    Wire.beginTransmission(ES7210_I2C_ADDR);
    return (Wire.endTransmission() == 0);
#else
    return false;
#endif
}

bool HardwareHAL::configureES7210() {
#if defined(ES7210_I2C_ADDR)
    std::lock_guard<std::mutex> lock(g_i2cMutex);

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

    // 2. Official esp_codec_dev / Waveshare ES7210 Register sequence for 16kHz, 16-bit, I2S Master/Slave
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
        {0x07, 0x20}, // OSR
        {0x02, 0xC1}, // MAINCLK: adc_div(1) | doubler(1<<6) | dll(1<<7) = 0xC1
        {0x04, 0x01}, // LRCK_DIVH
        {0x05, 0x00}, // LRCK_DIVL
        
        // Power down DLL: matches the v3.1.0 sequence verified on hardware. 0x00 (full
        // power on of every block) was tried during the audio refactor and is the
        // regression that silenced the microphone: 0x04 is required.
        {0x06, 0x04},

        // Power on MIC1-4 bias & ADC1-4 & PGA1-4 Power: matches v3.1.0. The refactor's
        // 0x00 here left the analog front-end powered down, so the ADC digitized
        // near-silence: the spectrum was technically "reading" but had nothing to show,
        // which is exactly the "flat from boot" symptom.
        {0x4B, 0x0F},
        {0x4C, 0x0F},

        // Disable ADC automute and force both channel pairs unmuted. This is a SEPARATE,
        // real fix from the one above, not a leftover of the same regression: the ES7210
        // hardware-mutes its digital output to a flat zero after it decides the input has
        // been "quiet" for a while (its own automute heuristic, independent of the
        // 0x4B/0x4C bias/PGA power state). That is the exact failure mode reported live
        // ("mic works for a while, then goes flat while DMA/I2S keeps reporting healthy
        // reads") and is exactly why commit 6ee7d4c originally added these three writes,
        // labeling them CRITICAL after observing a "digital zero freeze". Reverting them
        // to match v3.1.0 in an earlier pass of this fix was a mistake: v3.1.0 likely had
        // this same automute freeze, which is presumably why they were added in the first
        // place. Disabling automute at init is the root-cause fix; it replaces the old
        // runtime checkAndRecoverES7210() watchdog (removed), which only patched the
        // symptom after ~1s of silence instead of preventing it.
        {0x13, 0x00}, // Automute disabled
        {0x14, 0x00}, // ADC34 unmuted
        {0x15, 0x00}, // ADC12 unmuted

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

void HardwareHAL::evaluateES7210Signal(size_t bytesRead, int16_t maxPeak) {
#if defined(HARDWARE_PROFILE_WAVESHARE_S3) || defined(UNIT_TEST)
    if (bytesRead == 0) {
        // Transient DMA underrun, not an ES7210 silent-freeze failure
        return;
    }
    if (maxPeak > 0) {
        _es7210ZeroFrames.store(0, std::memory_order_relaxed);
        _es7210SignalHealthy.store(true, std::memory_order_release);
        return;
    }
    const uint16_t frames = _es7210ZeroFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    if (frames >= 60) {
        _es7210ZeroFrames.store(0, std::memory_order_relaxed);
        _es7210RecoveryPending.store(true, std::memory_order_release);
#if defined(HARDWARE_PROFILE_WAVESHARE_S3) && !defined(UNIT_TEST)
        if (s_es7210RecoveryTaskHandle != nullptr) {
            xTaskNotifyGive(s_es7210RecoveryTaskHandle);
        }
#endif
    }
#endif
}

bool HardwareHAL::checkAndPerformES7210Recovery() {
#if defined(HARDWARE_PROFILE_WAVESHARE_S3) || defined(UNIT_TEST)
    // 1. Reset consecutive attempt counter upon observing confirmed healthy signal
    if (_es7210SignalHealthy.exchange(false, std::memory_order_acquire)) {
        _consecutiveRecoveryCount = 0;
    }

    // 2. Fast check: is recovery pending?
    if (!_es7210RecoveryPending.load(std::memory_order_relaxed)) {
        return false;
    }

    // 3. Cooldown check: minimum 3000 ms between recovery attempts
    uint32_t now = millis();
    if (now - _lastES7210RecoveryMs < 3000) {
        return false; // Leave pending = true so Core 0 retries once cooldown expires
    }

    // 4. Consume pending flag
    if (!_es7210RecoveryPending.exchange(false, std::memory_order_acquire)) {
        return false;
    }

    _lastES7210RecoveryMs = now;
    _consecutiveRecoveryCount++;

#ifdef UNIT_TEST
    es7210I2cRecoveryCalls.fetch_add(1, std::memory_order_relaxed);
#endif

    LOGW("HardwareHAL", "ES7210 digital zero freeze detected! Performing Core 0 I2C recovery #%u...",
         _consecutiveRecoveryCount);

    if (_consecutiveRecoveryCount >= 3) {
        LOGW("HardwareHAL", "3 consecutive ES7210 recoveries failed. Triggering full configureES7210()...");
#ifdef UNIT_TEST
        es7210FullConfigCalls.fetch_add(1, std::memory_order_relaxed);
#endif
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
        configureES7210(); // configureES7210 handles g_i2cMutex internally
#endif
        _consecutiveRecoveryCount = 0;
        return true;
    }

    // Fixed un-mute sequence from commit 6ee7d4c:
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    {
        std::lock_guard<std::mutex> lock(g_i2cMutex);
        static const uint8_t unmuteCmds[][2] = {
            {0x13, 0x00}, // Automute disabled
            {0x14, 0x00}, // ADC34 unmuted
            {0x15, 0x00}, // ADC12 unmuted
            {0x00, 0x41}  // Device enable
        };
        for (size_t i = 0; i < sizeof(unmuteCmds)/sizeof(unmuteCmds[0]); i++) {
            Wire.beginTransmission(ES7210_I2C_ADDR);
            Wire.write(unmuteCmds[i][0]);
            Wire.write(unmuteCmds[i][1]);
            Wire.endTransmission();
        }
    }
#endif

    return true;
#else
    return false;
#endif
}

void HardwareHAL::startAudioSampling() {
    if (!_capabilities.hasMicrophone) return;

    // Record the standing intent even if the bus is busy, so capture can be reclaimed
    // automatically as soon as playback releases it.
    _captureRequested = true;
    if (audioActive) return;

    // Fast-path state check on Core 1: mic capture is deferred only while playback is actively playing sound
    if (audioOutputHAL.isPlaying()) {
        static unsigned long lastRefusalLog = 0;
        if (millis() - lastRefusalLog > 5000) {
            lastRefusalLog = millis();
            LOGW("HardwareHAL", "Audio capture deferred: playback is actively streaming sound.");
        }
        return;
    }

    // A restart must not replay the spectrum captured before the driver was torn down.
    _lastSpectrumBands = 0;
    _lastDecibels = 30.0f;
    _audioWarmupFrames = 0;
    for (size_t i = 0; i < MAX_SPECTRUM_BANDS; i++) _lastSpectrum[i] = 0.0f;
    _es7210ZeroFrames.store(0, std::memory_order_relaxed);
    _es7210RecoveryPending.store(false, std::memory_order_relaxed);

    // 0. Enable the Audio Power Rail on GPIO 11 for microphone session.
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);
    LOGI("HardwareHAL", "Audio power rail enabled on GPIO 11 for microphone session.");
#endif

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    // 1. Full-duplex I2S config (TX+RX) - matches official Waveshare BSP
    //    The ES7210 (ADC) and ES8311 (DAC) share the same I2S bus.
    //    Both channels must be active for proper clock generation and stable 4.096 MHz MCLK.
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,  // 16000 Hz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = I2S_DMA_DESC_COUNT,
        .dma_buf_len = I2S_DMA_FRAMES_PER_DESC,
        .use_apll = true,       // APLL for precise MCLK generation
        .tx_desc_auto_clear = true,
        .fixed_mclk = SAMPLE_RATE * 256  // 4.096 MHz fixed MCLK required by ES7210
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_MCLK_PIN,     // GPIO 12 - MCLK
        .bck_io_num = I2S_SCLK_PIN,     // GPIO 43 - BCLK
        .ws_io_num = I2S_LRCK_PIN,      // GPIO 38 - LRCK
        .data_out_num = 21,             // GPIO 21 - ES8311 DAC DOUT
        .data_in_num = I2S_ASDOUT_PIN   // GPIO 39 - ES7210 ADC DSIN
    };
#else
    // ESP32 Standard: Pure RX Mono (e.g. INMP441 on GPIO 32, 14, 15)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,  // 16000 Hz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = I2S_DMA_DESC_COUNT,
        .dma_buf_len = I2S_DMA_FRAMES_PER_DESC,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_SCLK_PIN,     // GPIO 14
        .ws_io_num = I2S_LRCK_PIN,      // GPIO 15
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_ASDOUT_PIN   // GPIO 32
    };
#endif

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err == ESP_OK) {
        err = i2s_set_pin(I2S_PORT, &pin_config);
        if (err != ESP_OK) {
            LOGE("HardwareHAL", "i2s_set_pin failed: %d", err);
        }
        i2s_zero_dma_buffer(I2S_PORT);
        i2s_start(I2S_PORT);
        audioActive = true;
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
        LOGI("HardwareHAL", "I2S DMA Audio Sampling STARTED (Waveshare S3 Full-Duplex, 4.096 MHz MCLK on GPIO %d).", I2S_MCLK_PIN);
#else
        LOGI("HardwareHAL", "I2S DMA Audio Sampling STARTED (ESP32 Standard RX Mono).");
#endif
        // Let the MCLK signal settle on the ES7210 pins before any I2C write. Writing registers
        // while MCLK is still unstable silently mis-latches them and the ADC stays muted.
        delay(50);

        // Configure & Power up ES7210 ADC registers NOW while MCLK is active!
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
        if (configureES7210()) {
            LOGI("HardwareHAL", "ES7210 Microphone ADC configured and powered up successfully.");
        } else {
            LOGW("HardwareHAL", "ES7210 I2C config failed, running generic I2S audio mode.");
        }
#endif
        // Flush initial stale DMA data captured while the ES7210 registers were still settling.
        // Bounded so activation never stalls the render loop for more than ~40 ms.
        int16_t dummyBuf[BUFFER_SIZE];
        size_t dummyRead = 0;
        for (int i = 0; i < 4; i++) {
            i2s_read(I2S_PORT, (void*)dummyBuf, sizeof(dummyBuf), &dummyRead, pdMS_TO_TICKS(10));
        }
        i2s_zero_dma_buffer(I2S_PORT);
        LOGI("HardwareHAL", "I2S DMA buffer flushed (%d dummy reads, %d x %d frame descriptors).",
             4, I2S_DMA_DESC_COUNT, I2S_DMA_FRAMES_PER_DESC);
    } else {
        LOGE("HardwareHAL", "Failed to install I2S driver! err=%d", err);
    }
}

void HardwareHAL::stopAudioSampling(bool clearIntent) {
    if (clearIntent) _captureRequested = false;
    if (!audioActive) return;

    i2s_stop(I2S_PORT);
    i2s_driver_uninstall(I2S_PORT);
    audioActive = false;
    _es7210ZeroFrames.store(0, std::memory_order_relaxed);
    _es7210RecoveryPending.store(false, std::memory_order_relaxed);
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    // Leave the PA in the state playback expects: off if not playing.
    if (!audioOutputHAL.isPlaying()) {
        pinMode(11, OUTPUT);
        digitalWrite(11, LOW);
    }
#endif
    // Logged at INFO because an unexpected teardown (I2S_NUM_0 is shared with AudioOutputHAL)
    // silently starves the visualizer and is otherwise invisible in a field log.
    LOGI("HardwareHAL", "I2S DMA Audio Sampling STOPPED (driver uninstalled, intent %s).",
         _captureRequested ? "kept" : "cleared");
}

float HardwareHAL::getDecibels(float dbCalibration) {
    // Only re-arm on a standing intent, and never while playback is actively outputting sound.
    if (!audioActive) {
        if (!_captureRequested || audioOutputHAL.isPlaying()) {
            return _lastDecibels + dbCalibration;
        }
        startAudioSampling();
        if (!audioActive) return _lastDecibels + dbCalibration;
    }

    int16_t sampleBuf[BUFFER_SIZE];
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, (void*)sampleBuf, sizeof(sampleBuf), &bytesRead,
             pdMS_TO_TICKS(AUDIO_DECIBEL_READ_TIMEOUT_MS));

    if (bytesRead == 0) {
        return _lastDecibels + dbCalibration; // Transient DMA underrun: hold the last measurement
    }

    size_t samplesCount = bytesRead / sizeof(int16_t);
    if (samplesCount == 0) return _lastDecibels + dbCalibration;
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    samplesCount = extractMonoChannel(sampleBuf, samplesCount);
    if (samplesCount == 0) return _lastDecibels + dbCalibration;
#endif
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

    evaluateES7210Signal(bytesRead, maxPeak);

    // Warm-up is per sampling session, not per boot: a function-local static would have stayed
    // latched after the very first four reads and skipped the settle window on every restart.
    if (_audioWarmupFrames < 4) {
        _audioWarmupFrames++;
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

    _lastDecibels = db - dbCalibration;

    static unsigned long lastAudioLog = 0;
    if (millis() - lastAudioLog > 2000) {
        lastAudioLog = millis();
        LOGD("HardwareHAL", "I2S Audio: bytesRead=%d, maxPeak=%d, rms=%.1f, db=%.1f dB",
             (int)bytesRead, (int)maxPeak, rms, db);
    }

    return db;
}

bool HardwareHAL::getAudioSpectrum(float* bands, size_t numBands) {
    if (!bands || numBands == 0) return false;
    if (numBands > MAX_SPECTRUM_BANDS) numBands = MAX_SPECTRUM_BANDS;

    // Only re-arm on a standing intent, and never while playback is actively outputting sound.
    if (!audioActive) {
        if (!_captureRequested || audioOutputHAL.isPlaying()) {
            for (size_t i = 0; i < numBands; i++) {
                float v = (_lastSpectrumBands == numBands) ? (_lastSpectrum[i] * 0.85f) : 0.0f;
                if (v < 0.002f) v = 0.0f;
                _lastSpectrum[i] = v;
                bands[i] = v;
            }
            return false;
        }
        startAudioSampling();
        if (!audioActive) {
            for (size_t i = 0; i < numBands; i++) bands[i] = 0.0f;
            return false;
        }
    }

    int16_t sampleBuf[BUFFER_SIZE];
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, (void*)sampleBuf, sizeof(sampleBuf), &bytesRead,
             pdMS_TO_TICKS(AUDIO_SPECTRUM_READ_TIMEOUT_MS));

    // Sampling health probe: distinguishes "the DMA ring is starving the visualizer" from
    // "the Core 1 render loop itself stalled". Throttled to one line every 2 s.
    static uint32_t probeCalls = 0, probeUnderruns = 0, probeMinBytes = 0xFFFFFFFF, probeMaxBytes = 0;
    static unsigned long probeWindowStart = 0;
    probeCalls++;
    if (bytesRead == 0) probeUnderruns++;
    if (bytesRead < probeMinBytes) probeMinBytes = bytesRead;
    if (bytesRead > probeMaxBytes) probeMaxBytes = bytesRead;
    unsigned long nowMs = millis();
    if (probeWindowStart == 0) probeWindowStart = nowMs;
    if (nowMs - probeWindowStart >= 2000) {
        LOGI("HardwareHAL", "MIC health: %u reads/2s (%.1f fps), %u underruns (%.0f%%), bytes min=%u max=%u",
             (unsigned)probeCalls, probeCalls / 2.0f, (unsigned)probeUnderruns,
             probeCalls ? (100.0f * probeUnderruns / probeCalls) : 0.0f,
             (unsigned)(probeMinBytes == 0xFFFFFFFF ? 0 : probeMinBytes), (unsigned)probeMaxBytes);
        probeCalls = probeUnderruns = probeMaxBytes = 0;
        probeMinBytes = 0xFFFFFFFF;
        probeWindowStart = nowMs;
    }

    size_t samplesCount = bytesRead / sizeof(int16_t);
    if (samplesCount == 0) {
        // A dry read is a DMA underrun, not silence. Replaying the previous frame with a decay
        // keeps the bars continuous across underruns while still fading out a dead microphone.
        if (_lastSpectrumBands == numBands) {
            for (size_t i = 0; i < numBands; i++) {
                _lastSpectrum[i] *= 0.85f;
                if (_lastSpectrum[i] < 0.002f) _lastSpectrum[i] = 0.0f;
                bands[i] = _lastSpectrum[i];
            }
        } else {
            for (size_t i = 0; i < numBands; i++) bands[i] = 0.0f;
        }
        return false;
    }

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)
    // See extractMonoChannel(): the RX stream is interleaved [L, R, ...] with only one
    // slot carrying the real microphone signal. Splicing both slots into the same FFT
    // window would corrupt the frequency axis, so this must run before the DC/FFT passes.
    samplesCount = extractMonoChannel(sampleBuf, samplesCount);
    if (samplesCount == 0) {
        for (size_t i = 0; i < numBands; i++) bands[i] = 0.0f;
        return false;
    }
#endif

    // First pass: DC offset and peak. The ES7210 carries a large DC bias; leaving it in
    // would dump all the energy into bin 0 and flatten every other bar.
    double sum = 0.0;
    int16_t maxPeak = 0;
    for (size_t i = 0; i < samplesCount; i++) {
        sum += sampleBuf[i];
        int16_t absVal = abs(sampleBuf[i]);
        if (absVal > maxPeak) maxPeak = absVal;
    }
    float dcOffset = (float)(sum / samplesCount);

    // Lock-free watchdog evaluation: evaluated strictly ONCE per physical PCM capture buffer on Core 1
    evaluateES7210Signal(bytesRead, maxPeak);

    // Second pass: Welch-averaged 64-point FFT.
    //
    // This is the regression that made the visualizer look "flat": the previous code
    // sliced the buffer into contiguous TIME windows and plotted the average amplitude
    // of each slice. For any steady signal every slice carries the same energy, so all
    // the bars ended up at the same height regardless of the actual audio content. It
    // was never a frequency transform.
    //
    // A single 64-sample window is only 4 ms at 16 kHz and is far too noisy on its own,
    // so the magnitude spectra of every non-overlapping window in the captured buffer
    // are averaged (Welch's method). The buffer is already paid for and the cost stays
    // bounded: BUFFER_SIZE / 64 transforms per frame, no allocation and no mutex.
    float fftIn[arcade_audio::FFT_SIZE];
    float fftMag[arcade_audio::FFT_BINS];
    float magAccum[arcade_audio::FFT_BINS] = {0.0f};

    size_t windows = samplesCount / arcade_audio::FFT_SIZE;
    for (size_t w = 0; w < windows; w++) {
        const int16_t* src = &sampleBuf[w * arcade_audio::FFT_SIZE];
        for (size_t i = 0; i < arcade_audio::FFT_SIZE; i++) {
            fftIn[i] = ((float)src[i] - dcOffset) / 32768.0f;
        }
        arcade_audio::computeFFT64(fftIn, fftMag);
        for (size_t k = 0; k < arcade_audio::FFT_BINS; k++) magAccum[k] += fftMag[k];
    }

    if (windows == 0) {
        // Buffer shorter than one transform: zero-pad a single window rather than
        // falling back to a non-spectral approximation.
        for (size_t i = 0; i < arcade_audio::FFT_SIZE; i++) {
            fftIn[i] = (i < samplesCount) ? (((float)sampleBuf[i] - dcOffset) / 32768.0f) : 0.0f;
        }
        arcade_audio::computeFFT64(fftIn, magAccum);
        windows = 1;
    } else {
        for (size_t k = 0; k < arcade_audio::FFT_BINS; k++) magAccum[k] /= (float)windows;
    }

    // Map the FFT bins onto the requested bar count using the shared log-spaced recipe,
    // then apply the same exponential moving average as the streamed-audio path so the
    // microphone and the radio visualizers behave identically.
    bool bandCountChanged = (_lastSpectrumBands != numBands);
    for (size_t b = 0; b < numBands; b++) {
        float rawVal = magAccum[arcade_audio::bandToBin(b, numBands)] * 4.0f * micGain;
        if (rawVal > 1.0f) rawVal = 1.0f;
        float prev = bandCountChanged ? 0.0f : _lastSpectrum[b];
        float smoothed = (prev * 0.65f) + (rawVal * 0.35f);
        bands[b] = smoothed;
        _lastSpectrum[b] = smoothed;
    }
    _lastSpectrumBands = numBands;

    static unsigned long lastSpectrumLog = 0;
    if (millis() - lastSpectrumLog > 2000) {
        lastSpectrumLog = millis();
        LOGD("HardwareHAL", "MIC Spectrum: bytes=%d, peak=%d, dc=%.1f, windows=%u, b0=%.2f, gain=%.1f",
             (int)bytesRead, maxPeak, dcOffset, (unsigned)windows, bands[0], micGain);
    }

    return true;
}
