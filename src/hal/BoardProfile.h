#pragma once
#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "HardwareHAL.h"

/**
 * @enum MemoryTier
 * @brief Execution and caching strategy for engines and runtime buffers.
 */
enum class MemoryTier : uint8_t {
    CONSTRAINED = 0, ///< Internal DRAM only (~160KB usable), zero-allocation hot paths, compact caches
    EXPANDED = 1     ///< Abundant memory (PSRAM 8MB+), expanded caches and working buffers
};

/**
 * @struct MemoryCapabilities
 * @brief Hardware memory snapshot (strictly separate from MemoryTier software strategy).
 */
struct MemoryCapabilities {
    bool hasPsram = false;
    size_t internalRamBytes = 0;
    size_t psramBytes = 0;
    size_t flashBytes = 0;
    MemoryTier tier = MemoryTier::CONSTRAINED;
};

/**
 * @struct StorageCapabilities
 * @brief Storage interface capabilities and operational status.
 */
struct StorageCapabilities {
    bool supportsSdMmc = false;
    bool supportsSdSpi = false;
    bool sdMounted = false;
    uint32_t defaultSckMhz = 25;
};

/**
 * @struct DisplayCapabilities
 * @brief HUB75 physical display capabilities and validated limits.
 */
struct DisplayCapabilities {
    uint8_t defaultColorDepth = 6; ///< Conservative default for constrained hardware (6 bits/channel = 64 levels)
    uint16_t maxWidth = 128;       ///< Validated support envelope for the profile
    uint16_t maxHeight = 64;
    uint8_t maxPanels = 2;
    bool supportsDoubleBuffering = true;
    bool supportsWideCanvas = true;
};

/**
 * @class IBoardProfile
 * @brief Abstract contract for board-specific hardware profiles.
 */
class IBoardProfile {
public:
    virtual ~IBoardProfile() = default;

    virtual HwProfile id() const = 0;
    virtual const char* name() const = 0;

    // --- Subsystem Capabilities ---
    virtual const MemoryCapabilities& memory() const = 0;
    virtual const StorageCapabilities& storage() const = 0;
    virtual const DisplayCapabilities& display() const = 0;

    // --- Power & Hardware Quirks ---
    virtual void applyPowerQuirks() = 0;
    virtual void configureWifiTxPower() = 0;

    // --- Storage Operations (Safe Mode Aware) ---
    virtual bool beginStorage() = 0;
    virtual void endStorage() = 0;
    virtual bool isStorageAvailable() const = 0;
    virtual uint64_t getStorageTotalBytes() = 0;
    virtual uint64_t getStorageFreeBytes() = 0;

    // --- HUB75 Display Matrix Pins ---
    virtual void populateMatrixPins(HUB75_I2S_CFG::i2s_pins& pins) = 0;
};

/**
 * @class BoardProfile
 * @brief Static access point for the active hardware profile.
 */
class BoardProfile {
public:
    static IBoardProfile& current();
    static HwProfile currentId();
    static MemoryTier currentMemoryTier() {
        return current().memory().tier;
    }
};
