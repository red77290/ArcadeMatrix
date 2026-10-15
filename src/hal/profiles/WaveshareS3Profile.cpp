#include "WaveshareS3Profile.h"

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)

#include <SD_MMC.h>
#include "../../core/Logger.h"
#include "../../../include/HardwareProfile.h"

WaveshareS3Profile::WaveshareS3Profile() {
    m_storage.supportsSdMmc = true;
    m_storage.supportsSdSpi = false;
    m_storage.sdMounted = false;
    m_storage.defaultSckMhz = 40;

    m_display.supportsDoubleBuffering = true;
    m_display.supportsWideCanvas = true;

    refreshCapabilities();
}

void WaveshareS3Profile::refreshCapabilities() {
    m_memory.hasPsram = psramFound();
    m_memory.internalRamBytes = 512 * 1024;
    size_t detectedPsram = m_memory.hasPsram ? ESP.getPsramSize() : 0;
    m_memory.psramBytes = detectedPsram;
    uint32_t detectedFlash = ESP.getFlashChipSize();
    m_memory.flashBytes = (detectedFlash > 0) ? detectedFlash : (8 * 1024 * 1024);
    m_memory.tier = (m_memory.hasPsram && m_memory.psramBytes >= (2 * 1024 * 1024))
                        ? MemoryTier::EXPANDED
                        : MemoryTier::CONSTRAINED;

    if (m_memory.tier == MemoryTier::EXPANDED) {
        m_display.defaultColorDepth = 8;
        m_display.maxWidth = 256;
        m_display.maxHeight = 128;
        m_display.maxPanels = 4;
    } else {
        m_display.defaultColorDepth = 6;
        m_display.maxWidth = 128;
        m_display.maxHeight = 64;
        m_display.maxPanels = 2;
    }

    unsigned flashMb = (unsigned)(m_memory.flashBytes / (1024 * 1024));
    unsigned psramMb = (unsigned)((m_memory.psramBytes + (512 * 1024)) / (1024 * 1024));
#if defined(HARDWARE_VARIANT_N16R8)
    if (psramMb > 0) {
        snprintf(m_boardName, sizeof(m_boardName), "ESP32-S3 N%uR%u (%uMB Flash / %uMB PSRAM)",
                 flashMb, psramMb, flashMb, psramMb);
    } else {
        snprintf(m_boardName, sizeof(m_boardName), "ESP32-S3 N%u (%uMB Flash / No PSRAM)",
                 flashMb, flashMb);
    }
#else
    if (psramMb > 0) {
        snprintf(m_boardName, sizeof(m_boardName), "Waveshare ESP32-S3 (N%uR%u)", flashMb, psramMb);
    } else {
        snprintf(m_boardName, sizeof(m_boardName), "Waveshare ESP32-S3 (N%u)", flashMb);
    }
#endif
}

void WaveshareS3Profile::applyPowerQuirks() {
    // Refresh runtime memory capabilities once ESP-IDF heap/flash subsystems are fully initialized.
    refreshCapabilities();
#if defined(HARDWARE_VARIANT_N16R8) && !defined(UNIT_TEST)
    // Immediately turn off the blinding onboard WS2812 RGB LED on ESP32-S3 N16R8 / DevKitC-1 (GPIO 48 / GPIO 38)
    neopixelWrite(48, 0, 0, 0);
    pinMode(48, OUTPUT);
    digitalWrite(48, LOW);
    neopixelWrite(38, 0, 0, 0);
    pinMode(38, OUTPUT);
    digitalWrite(38, LOW);
#endif
}

void WaveshareS3Profile::configureWifiTxPower() {
    // Full RF power supported by S3 onboard power supply.
}

bool WaveshareS3Profile::beginStorage() {
    m_storage.sdMounted = false;
    if (!SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)) {
        LOGE("SD", "SD_MMC setPins failed on Waveshare S3.");
        return false;
    }
    // Configure SD_MMC with max_files=3 so vfs_fat_ctx_t stays strictly in internal DRAM
    if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 3)) {
        LOGW("SD", "SD_MMC mount failed on Waveshare S3. Continuing in Safe Mode.");
        return false;
    }
    m_storage.sdMounted = true;
    LOGI("SD", "SD_MMC mounted successfully at native hardware speed.");
    return true;
}

void WaveshareS3Profile::endStorage() {
    SD_MMC.end();
    m_storage.sdMounted = false;
}

bool WaveshareS3Profile::isStorageAvailable() const {
    return m_storage.sdMounted;
}

uint64_t WaveshareS3Profile::getStorageTotalBytes() {
    if (!m_storage.sdMounted) return 0;
    return SD_MMC.totalBytes();
}

uint64_t WaveshareS3Profile::getStorageFreeBytes() {
    if (!m_storage.sdMounted) return 0;
    uint64_t total = SD_MMC.totalBytes();
    uint64_t used = SD_MMC.usedBytes();
    return (total >= used) ? (total - used) : 0;
}

void WaveshareS3Profile::populateMatrixPins(HUB75_I2S_CFG::i2s_pins& pins) {
    pins.r1 = MATRIX_R1_PIN;
    pins.g1 = MATRIX_G1_PIN;
    pins.b1 = MATRIX_B1_PIN;
    pins.r2 = MATRIX_R2_PIN;
    pins.g2 = MATRIX_G2_PIN;
    pins.b2 = MATRIX_B2_PIN;
    pins.a  = MATRIX_A_PIN;
    pins.b  = MATRIX_B_PIN;
    pins.c  = MATRIX_C_PIN;
    pins.d  = MATRIX_D_PIN;
    pins.e  = MATRIX_E_PIN;
    pins.lat = MATRIX_LAT_PIN;
    pins.oe  = MATRIX_OE_PIN;
    pins.clk = MATRIX_CLK_PIN;
}

#endif // HARDWARE_PROFILE_WAVESHARE_S3
