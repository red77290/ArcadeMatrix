#include "Esp32DevProfile.h"

#if defined(HARDWARE_PROFILE_ESP32_DEV)

#include <SPI.h>
#include <SdFat.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "../../core/Logger.h"
#include "../../../include/HardwareProfile.h"

extern SdFs sd;

Esp32DevProfile::Esp32DevProfile() {
    m_memory.hasPsram = false;
    m_memory.internalRamBytes = 320 * 1024;
    m_memory.psramBytes = 0;
    m_memory.flashBytes = 4 * 1024 * 1024;
    m_memory.tier = MemoryTier::CONSTRAINED;

    m_storage.supportsSdMmc = false;
    m_storage.supportsSdSpi = true;
    m_storage.sdMounted = false;
    m_storage.defaultSckMhz = 25;

    m_display.defaultColorDepth = 6; // Conservative default for constrained hardware (6 bits/channel = 64 levels)
    m_display.maxWidth = 128;
    m_display.maxHeight = 64;
    m_display.maxPanels = 2;
    m_display.supportsDoubleBuffering = true;
    m_display.supportsWideCanvas = true;
}

extern "C" void __wrap_esp_brownout_init(void) {
    // Intercept and bypass ESP-IDF early brownout detector initialization.
    // On classic ESP32 USB development boards with HUB75 panels, transient voltage dips
    // during boot trip the detector before the kernel or user application can configure power limits.
}

void Esp32DevProfile::applyPowerQuirks() {
    // Disable brownout detector on classic ESP32 to prevent spurious resets caused by
    // microsecond voltage drops when USB power is shared between HUB75 DMA panels and Wi-Fi bursts.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

void Esp32DevProfile::configureWifiTxPower() {
    // Cap Wi-Fi TX power to 17dBm to prevent instantaneous ~500mA RF surges on weak USB rails
    WiFi.setTxPower(WIFI_POWER_17dBm);
}

bool Esp32DevProfile::beginStorage() {
    m_storage.sdMounted = false;
    SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, SD_CS_PIN);
    SdSpiConfig spiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(25), &SPI);
    if (sd.begin(spiConfig)) {
        m_storage.sdMounted = true;
        m_storage.defaultSckMhz = 25;
        LOGI("SD", "SD Card mounted successfully at 25 MHz.");
        return true;
    }

    // Fallback attempt at 16 MHz if 25 MHz had signal integrity issues
    delay(10);
    SdSpiConfig fallbackConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(16), &SPI);
    if (sd.begin(fallbackConfig)) {
        m_storage.sdMounted = true;
        m_storage.defaultSckMhz = 16;
        LOGW("SD", "SD Card mounted at fallback frequency: 16 MHz.");
        return true;
    }

    LOGE("SD", "SD Card mount failed. Starting in Safe Mode (Flash defaults, Wi-Fi & WebServer active).");
    return false;
}

void Esp32DevProfile::endStorage() {
    m_storage.sdMounted = false;
}

bool Esp32DevProfile::isStorageAvailable() const {
    return m_storage.sdMounted;
}

uint64_t Esp32DevProfile::getStorageTotalBytes() {
    if (!m_storage.sdMounted || !sd.vol()) return 0;
    uint32_t clusterCount = sd.vol()->clusterCount();
    uint32_t secPerCluster = sd.vol()->sectorsPerCluster();
    return (uint64_t)clusterCount * secPerCluster * 512ULL;
}

uint64_t Esp32DevProfile::getStorageFreeBytes() {
    if (!m_storage.sdMounted || !sd.vol()) return 0;
    uint32_t volFree = sd.vol()->freeClusterCount();
    uint32_t secPerCluster = sd.vol()->sectorsPerCluster();
    return (uint64_t)volFree * secPerCluster * 512ULL;
}

void Esp32DevProfile::populateMatrixPins(HUB75_I2S_CFG::i2s_pins& pins) {
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

#endif // HARDWARE_PROFILE_ESP32_DEV
