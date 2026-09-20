/**
 * @file MatrixEngine.h
 * @brief Manages the HUB75 hardware initialization and DMA buffering.
 * 
 * This class abstracts the ESP32-HUB75-MatrixPanel-I2S-DMA library, providing
 * dynamic memory configuration based on panel size to prevent Out-Of-Memory (OOM) crashes.
 */
#pragma once
#include <Arduino.h>
#include <vector>
#include <memory>

#define private protected
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#undef private

#include "ConfigLoader.h"

#if defined(ESP32_THE_ORIG)
#define MATRIX_TX_ADJUST(x_coord) (((x_coord) & 1U) ? ((x_coord) - 1) : ((x_coord) + 1))
#else
#define MATRIX_TX_ADJUST(x_coord) (x_coord)
#endif

/**
 * @class FastMatrixPanel
 * @brief The HUB75 panel with a cheap black fill and optimized row-wise DMA blit.
 *
 * `fillScreen(0)` is what nearly every engine does first on every frame. The library implements it
 * as a per-pixel write, which on a PSRAM-resident DMA buffer costs a read-modify-write plus a cache
 * write-back per colour plane per pixel: about 100 ms for 256x64, and the reason the animated clock
 * faces ran at 7-8 fps on the ESP32-S3 Waveshare board. The library also has the row-wise
 * initialiser it uses at boot (`clearFrameBuffer`), which rewrites each row sequentially with one
 * write-back per row and leaves the buffer exactly as a black fill would, once the brightness (OE)
 * bits are re-applied. Black fills go there; any other colour takes the library path.
 *
 * `clearFrameBuffer` needs the id of the buffer being drawn, which the library keeps private, so
 * MatrixEngine tells the panel about every flip (there is a single flip site, `present()`) and the
 * brightness it last applied.
 *
 * Additionally, FastMatrixPanel provides `blitCanvas565()` to stream an entire RGB565 canvas
 * directly into the back-buffer DMA plane words using sequential row bursts, and an optimized
 * `drawPixel()` that performs proper CIE 1931 bit-depth scaling to prevent color wrapping
 * when colorDepth < 8.
 */
class FastMatrixPanel : public MatrixPanel_I2S_DMA {
public:
    using MatrixPanel_I2S_DMA::MatrixPanel_I2S_DMA;

    void fillScreen(uint16_t color) override;
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void blitCanvas565(const uint16_t* src, int canvasWidth, int canvasHeight);

    /// Called once after begin(): the library flips once inside begin(), so with double buffering
    /// the back buffer is 1 at that point.
    void setBuffering(bool doubleBuffered);
    void noteFlip() { if (m_double) m_back ^= 1; }
    void rememberBrightness8(uint8_t b) { m_brightness8 = b; }
    void initLuts(uint8_t depth);
    uint8_t getActiveBackBuffer() const { return m_back; }

private:
    bool m_double = false;
    int m_back = 0;
    uint8_t m_brightness8 = 64;
    uint8_t m_depth = 8;
    uint8_t m_lut_r[32];
    uint8_t m_lut_g[64];
    uint8_t m_lut_b[32];
};

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
     * @param config The MatrixConfig loaded from config.json
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

    /**
     * @brief Blit a full RGB565 canvas buffer directly into the DMA back buffer.
     * Sequential row-wise burst write bypassing per-pixel overhead.
     */
    void blitCanvas565(const uint16_t* src, int canvasWidth, int canvasHeight);
    FastMatrixPanel* getFastPanel() { return m_panel; }

    /**
     * @brief Flip the DMA buffers (show the back buffer, draw into the other one) and count it.
     *
     * Every flip in the firmware goes through here so the count's parity always says which physical
     * buffer is currently the back buffer. GifEngine relies on that to keep one shadow copy per
     * buffer and push only the pixels that changed since that buffer was last drawn.
     */
    void present();
    uint32_t flipCount() const { return m_flipCount; }
    bool isDoubleBuffered() const { return m_doubleBuffered; }

    /**
     * @brief Record that something other than the active engine drew into a framebuffer: an overlay,
     * a notice, the power-off clear, an orientation transition. Engines that skip unchanged pixels
     * compare this generation with the one they last synced to and redraw everything after a change.
     */
    void markExternalDraw() { m_externalDrawGeneration++; }
    uint32_t externalDrawGeneration() const { return m_externalDrawGeneration; }

private:
    MatrixPanel_I2S_DMA* display; ///< Pointer to the underlying DMA library instance
    FastMatrixPanel* m_panel = nullptr; ///< same object as `display`, typed for the fast clear hooks
    uint32_t m_flipCount = 0;
    uint32_t m_externalDrawGeneration = 0;
    bool m_doubleBuffered = false;
};

