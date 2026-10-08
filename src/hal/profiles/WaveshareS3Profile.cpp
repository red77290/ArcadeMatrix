#include "WaveshareS3Profile.h"

#if defined(HARDWARE_PROFILE_WAVESHARE_S3)

#include <SD_MMC.h>
#include "../../core/Logger.h"
#include "../../../include/HardwareProfile.h"

WaveshareS3Profile::WaveshareS3Profile() {
    m_memory.hasPsram = psramFound();
    m_memory.internalRamBytes = 512 * 1024;
    size_t detectedPsram = psramFound() ? ESP.getPsramSize() : 0;
    m_memory.psramBytes = (detectedPsram > 0) ? detectedPsram : (8 * 1024 * 1024);
    m_memory.flashBytes = 16 * 1024 * 1024;
    m_memory.tier = MemoryTier::EXPANDED;

    m_storage.supportsSdMmc = true;
    m_storage.supportsSdSpi = false;
    m_storage.sdMounted = false;
    m_storage.defaultSckMhz = 40;

    m_display.defaultColorDepth = 8; // 8-bit default on Waveshare S3
    m_display.maxWidth = 256;
    m_display.maxHeight = 128;
    m_display.maxPanels = 4;
    m_display.supportsDoubleBuffering = true;
    m_display.supportsWideCanvas = true;
}

void WaveshareS3Profile::applyPowerQuirks() {
    // Waveshare S3 integrates high-efficiency onboard DC-DC power converter; no brownout quirk needed.
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
