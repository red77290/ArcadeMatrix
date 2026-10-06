#include "Arduino.h"
#include "core/Logger.h"
#include "hal/BoardProfile.h"
#include "hal/HardwareHAL.h"
#include "core/ConfigLoader.h"

HardwareSerial Serial;
LogLevel Logger::currentLevel = LOG_LEVEL_WARN;
std::mutex g_i2cMutex;

class MockBoardProfile : public IBoardProfile {
public:
    HwProfile profileId = HwProfile::ESP32_STD;
    HwProfile id() const override { return profileId; }
    const char* name() const override { return (profileId == HwProfile::WAVESHARE_S3) ? "WAVESHARE_S3_MOCK" : "ESP32_STD_MOCK"; }
    const MemoryCapabilities& memory() const override { static MemoryCapabilities m; return m; }
    const StorageCapabilities& storage() const override { static StorageCapabilities s; return s; }
    const DisplayCapabilities& display() const override {
        static DisplayCapabilities dStd{6, 128, 64, 2, true, true};
        static DisplayCapabilities dS3{8, 256, 64, 4, true, true};
        return (profileId == HwProfile::WAVESHARE_S3) ? dS3 : dStd;
    }
    void applyPowerQuirks() override {}
    void configureWifiTxPower() override {}
    bool beginStorage() override { return true; }
    void endStorage() override {}
    bool isStorageAvailable() const override { return true; }
    uint64_t getStorageTotalBytes() override { return 0; }
    uint64_t getStorageFreeBytes() override { return 0; }
    void populateMatrixPins(HUB75_I2S_CFG::i2s_pins&) override {}
};

static MockBoardProfile g_mockBoardProfile;

IBoardProfile& BoardProfile::current() {
    return g_mockBoardProfile;
}
HwProfile BoardProfile::currentId() {
    return g_mockBoardProfile.id();
}

void setMockProfile(HwProfile p) {
    g_mockBoardProfile.profileId = p;
}

HardwareHAL::HardwareHAL() {}
HardwareHAL::~HardwareHAL() {}

ConfigLoader::ConfigLoader() {}
ConfigSnapshotGuard ConfigLoader::acquireSnapshot() const {
    static ConfigSnapshot dummy;
    return ConfigSnapshotGuard(*this, dummy, 0);
}
void ConfigLoader::releaseSnapshot(uint8_t) const {}

ConfigLoader config;
HardwareHAL hardwareHAL;
