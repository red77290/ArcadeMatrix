#pragma once
#include "../BoardProfile.h"

class WaveshareS3Profile : public IBoardProfile {
public:
    WaveshareS3Profile();
    ~WaveshareS3Profile() override = default;

    HwProfile id() const override { return HwProfile::WAVESHARE_S3; }
    const char* name() const override { return "Waveshare ESP32-S3 Matrix"; }

    const MemoryCapabilities& memory() const override { return m_memory; }
    const StorageCapabilities& storage() const override { return m_storage; }
    const DisplayCapabilities& display() const override { return m_display; }

    void applyPowerQuirks() override;
    void configureWifiTxPower() override;

    bool beginStorage() override;
    void endStorage() override;
    bool isStorageAvailable() const override;
    uint64_t getStorageTotalBytes() override;
    uint64_t getStorageFreeBytes() override;

    void populateMatrixPins(HUB75_I2S_CFG::i2s_pins& pins) override;

private:
    MemoryCapabilities m_memory;
    StorageCapabilities m_storage;
    DisplayCapabilities m_display;
};
