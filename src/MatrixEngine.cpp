#include "MatrixEngine.h"

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
 * @param config The MatrixConfig loaded from conf.ini
 * @return true if DMA allocation and initialization succeeded.
 * @return false if out of memory or initialization failed.
 */
bool MatrixEngine::begin(const MatrixConfig& config) {
    // User's specific hardware pin mapping extracted from Retro_Pixel_LED_4_0_0.ino
    HUB75_I2S_CFG::i2s_pins _pins = {
        25, 26, 27,   // R1, G1, B1
        14, 12, 13,   // R2, G2, B2
        33, 32, 22,   // A, B, C
        17, 18,       // D, E (18 is standard for E on 64x64. Change to 21 if your shield uses 21)
        4, 15, 16     // LAT, OE, CLK
    };

    HUB75_I2S_CFG mxconfig(
        config.width,      // Module width
        config.height,     // Module height
        config.chainLength,// Chain length
        _pins              // Custom pin mapping
    );
    
    // Increase minimum refresh rate to eliminate flickering
    mxconfig.min_refresh_rate = 120;
    mxconfig.latch_blanking = 8; mxconfig.clkphase = false; // Fixes green pixel flickering in corners on some panels

    // Apply double buffering if not forced to single
    mxconfig.double_buff = !config.forceSingleBuffer;
    
    // PSRAM Warning for large panels
    if (config.width * config.height * config.chainLength >= 16384) { 
        if (!psramFound()) {
            Serial.println("WARNING: 256x64 requested but no PSRAM found! This WILL cause Out-Of-Memory bootloops on a standard ESP32 WROOM.");
            // We no longer force 3-bit color here, because the user explicitly wants 24-bit on ESP32-S3.
        } else {
            Serial.println("PSRAM found. 256x64 will use PSRAM for DMA buffering safely.");
        }
    }

    // Initialize display object
    display = new MatrixPanel_I2S_DMA(mxconfig);
    
    // Allocate memory and start DMA
    if (!display->begin()) {
        Serial.println("Error: Failed to allocate memory for Matrix DMA!");
        return false;
    }

    display->setBrightness8(64); // Safe default brightness
    display->clearScreen();

    return true;
}

void MatrixEngine::clear() {
    if (display) {
        display->clearScreen();
    }
}

void MatrixEngine::setBrightness(uint8_t brightness) {
    if (display) {
        display->setBrightness8(brightness);
    }
}

MatrixPanel_I2S_DMA* MatrixEngine::getDisplay() {
    return display;
}
