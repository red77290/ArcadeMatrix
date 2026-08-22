#include "MatrixEngine.h"
#include "../hal/HardwareHAL.h"
#include "Logger.h"
#include "../../include/HardwareProfile.h"

/**
 * @brief Construct a new Matrix Engine object.
 * Initializes the display pointer to nullptr.
 */
MatrixEngine::MatrixEngine() : display(nullptr) {}

/**
 * @brief Destroy the Matrix Engine object.
 * Safely deletes the display instance and frees DMA memory.
 */
MatrixEngine::~MatrixEngine() {
    if (display) {
        delete display;
    }
}

/**
 * @brief Initialize the hardware matrix panel.
 * 
 * Automatically adjusts color depth and double-buffering based on the total 
 * physical pixel count to prevent ESP32 memory limits from being exceeded.
 * 
 * @param config The MatrixConfig loaded from config.json
 * @return true if DMA allocation and initialization succeeded.
 * @return false if out of memory or initialization failed.
 */
bool MatrixEngine::begin(const MatrixConfig& config) {
    int8_t out1[3] = {MATRIX_R1_PIN, MATRIX_G1_PIN, MATRIX_B1_PIN};
    int8_t out2[3] = {MATRIX_R2_PIN, MATRIX_G2_PIN, MATRIX_B2_PIN};
    int8_t pins1[3] = {MATRIX_R1_PIN, MATRIX_G1_PIN, MATRIX_B1_PIN};
    int8_t pins2[3] = {MATRIX_R2_PIN, MATRIX_G2_PIN, MATRIX_B2_PIN};
    
    if (config.rgbSequence.length() >= 3) {
        String seq = config.rgbSequence;
        seq.toUpperCase();
        for(int i = 0; i < 3; i++) {
            if(seq[i] == 'R') { out1[0] = pins1[i]; out2[0] = pins2[i]; }
            else if(seq[i] == 'G') { out1[1] = pins1[i]; out2[1] = pins2[i]; }
            else if(seq[i] == 'B') { out1[2] = pins1[i]; out2[2] = pins2[i]; }
        }
    }

    HUB75_I2S_CFG::i2s_pins _pins = {
        out1[0], out1[1], out1[2],
        out2[0], out2[1], out2[2],
        MATRIX_A_PIN, MATRIX_B_PIN, MATRIX_C_PIN,
        MATRIX_D_PIN, MATRIX_E_PIN,
        MATRIX_LAT_PIN, MATRIX_OE_PIN, MATRIX_CLK_PIN
    };

    HUB75_I2S_CFG mxconfig(
        config.width,      // Module width
        config.height,     // Module height
        config.chainLength,// Chain length
        _pins              // Custom pin mapping
    );
    
    // Use configured per-channel color depth (2 to 8, default 8)
    int depth = config.colorDepth;
    if (depth <= 0) {
        depth = 8; // Default fallback
    } else if (depth > 8) {
        // Clamp to 8 (max supported color depth bits per channel by the library).
        // A value of 11 (legacy PWM bits) should map to 8 bits color depth, not 3.
        depth = 8;
    }
    if (depth < 2 || depth > 8) {
        depth = 8; // Safe fallback if invalid range
    }
    
    mxconfig.setPixelColorDepthBits(depth);
    mxconfig.min_refresh_rate = config.limitRefreshRateHz > 0 ? config.limitRefreshRateHz : 90;
    mxconfig.latch_blanking = config.latchBlanking > 0 ? config.latchBlanking : 8;
    mxconfig.clkphase = config.clkPhase;

    String chip = config.driverChip;
    chip.toUpperCase();
    if (chip == "FM6124") {
        mxconfig.driver = HUB75_I2S_CFG::FM6124;
    } else if (chip == "FM6126" || chip == "FM6126A") {
        mxconfig.driver = HUB75_I2S_CFG::FM6126A;
    } else if (chip == "ICN2038S" || chip == "ICN2037" || chip == "SM16208") {
        mxconfig.driver = HUB75_I2S_CFG::ICN2038S;
    } else {
        mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
    }

    // Apply double buffering if not forced to single
    mxconfig.double_buff = !config.forceSingleBuffer;
    
    // PSRAM Warning for large panels
    if (config.width * config.height * config.chainLength >= 16384) { 
        if (!hardwareHAL.capabilities().hasPsram) {
            Serial.println("WARNING: 256x64 requested but no PSRAM found! This WILL cause Out-Of-Memory bootloops on a standard ESP32 WROOM.");
            // We no longer force 3-bit color here, because the user explicitly wants 24-bit on ESP32-S3.
        } else {
            LOGI("MatrixEngine", "PSRAM found. 256x64 will use PSRAM for DMA buffering safely.");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
            LOGW("MatrixEngine", "WARNING: default HUB75 pin map uses GPIO32/33 which conflicts with ESP32-S3 octal PSRAM. Verify/adjust pin map if needed.");
#endif
        }
    }

    // Initialize display object
    display = new MatrixPanel_I2S_DMA(mxconfig);
    
    // Allocate memory and start DMA
    if (!display->begin()) {
        LOGE("MatrixEngine", "Failed to allocate memory for Matrix DMA!");
        return false;
    }

    display->setBrightness8(64); // Safe default brightness
    display->clearScreen();
    display->flipDMABuffer();
    display->clearScreen();
    display->flipDMABuffer();

    return true;
}

void MatrixEngine::clear() {
    if (display) {
        display->clearScreen();
    }
}

void MatrixEngine::setBrightness(uint8_t brightness) {
    if (display) {
        // brightness is 0-100 (percentage), setBrightness8 expects 0-255
        uint8_t scaledBrightness = (brightness * 255) / 100;
        display->setBrightness8(scaledBrightness);
    }
}

MatrixPanel_I2S_DMA* MatrixEngine::getDisplay() {
    return display;
}
