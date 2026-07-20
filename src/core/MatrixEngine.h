/**
 * @file MatrixEngine.h
 * @brief Manages the HUB75 hardware initialization and DMA buffering.
 * 
 * This class abstracts the ESP32-HUB75-MatrixPanel-I2S-DMA library, providing
 * dynamic memory configuration based on panel size to prevent Out-Of-Memory (OOM) crashes.
 */
#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ConfigLoader.h"

/**
 * @class MatrixEngine
 * @brief Wrapper for the HUB75 I2S DMA Matrix Panel.
 */
class MatrixEngine {
public:
    /**
     * @brief Construct a new Matrix Engine object.
     */
    MatrixEngine();
    
    /**
     * @brief Destroy the Matrix Engine object and free DMA memory.
     */
    ~MatrixEngine();

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
    bool begin(const MatrixConfig& config);
    
    /**
     * @brief Clear the entire matrix screen.
     */
    void clear();
    
    /**
     * @brief Set the global hardware brightness of the matrix.
     * 
     * @param brightness 0-255 brightness level.
     */
    void setBrightness(uint8_t brightness);
    
    /**
     * @brief Get the underlying DMA display object.
     * 
     * Direct access is provided for fast drawing operations required by 
     * the GifEngine and FighterEngine.
     * 
     * @return MatrixPanel_I2S_DMA* Pointer to the display instance.
     */
    MatrixPanel_I2S_DMA* getDisplay();

private:
    MatrixPanel_I2S_DMA* display; ///< Pointer to the underlying DMA library instance
};

