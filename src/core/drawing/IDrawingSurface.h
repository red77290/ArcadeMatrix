/**
 * @file IDrawingSurface.h
 * @brief Canonical Hardware-Agnostic Drawing SPI for ArcadeMatrix v4.
 *
 * Decouples display engines from physical hardware buffering, DMA topologies,
 * bitplane modulation, and PSRAM memory configurations by providing an abstract
 * Adafruit_GFX-compatible rendering surface.
 */
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

/**
 * @enum CanvasStorage
 * @brief Memory tier backing the intermediate rasterization canvas.
 */
enum class CanvasStorage : uint8_t {
    NONE,  ///< Direct rendering to hardware display buffer (zero intermediate RAM)
    SRAM,  ///< Intermediate 16-bit RGB565 canvas allocated in internal DRAM
    PSRAM  ///< Intermediate 16-bit RGB565 canvas allocated in external SPIRAM
};

/**
 * @enum PresentationStrategy
 * @brief Hardware display synchronization and presentation mechanism.
 */
enum class PresentationStrategy : uint8_t {
    DIRECT_DMA_DOUBLE,   ///< Direct rendering to HUB75 DMA back-buffer + flipDMABuffer()
    DIRECT_DMA_SINGLE,   ///< Direct rendering to single DMA buffer (minimal memory, unmanaged tearing)
    CANVAS_BURST_SINGLE, ///< Canvas + Hub75BulkEncoder to single DMA buffer with presentation synchronization
    CANVAS_BURST_DOUBLE  ///< Canvas + Hub75BulkEncoder to inactive DMA back-buffer + flipDMABuffer()
};

/**
 * @struct CanvasView
 * @brief Scoped borrowed view of a linear 16-bit RGB565 canvas.
 *
 * CONTRACT: The raw pixel pointer is valid strictly during the active render phase.
 * Display engines must NEVER store, cache, or retain this pointer across frames.
 */
struct CanvasView {
    uint16_t* data = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    size_t stridePixels = 0;

    CanvasView() = default;
    CanvasView(uint16_t* d, uint16_t w, uint16_t h, size_t s = 0)
        : data(d), width(w), height(h), stridePixels(s) {}

    inline bool isValid() const { return data != nullptr; }
};

/**
 * @struct PresentationTiming
 * @brief Comprehensive telemetry capturing presentation and synchronization latency.
 */
struct PresentationTiming {
    uint32_t waitForSafeWindowUs = 0; ///< Duration spent waiting for safe DMA scan window
    uint32_t encodeUs = 0;            ///< Duration spent in Hub75BulkEncoder bitplane packing
    uint32_t transferUs = 0;          ///< Duration spent writing to DMA / CPU cache writeback
    uint32_t blankUs = 0;             ///< Duration Output Enable (OE) was held blanked
    uint32_t totalPresentUs = 0;      ///< Total presentation duration
};

/**
 * @class IDrawingSurface
 * @brief Abstract graphical SPI extending Adafruit_GFX.
 */
class IDrawingSurface : public Adafruit_GFX {
public:
    IDrawingSurface(int16_t logicalW, int16_t logicalH, int16_t physW, int16_t physH)
        : Adafruit_GFX(logicalW, logicalH), m_physW(physW), m_physH(physH) {}
    virtual ~IDrawingSurface() = default;

    // --- Presentation Lifecycle ---
    virtual void clear(uint16_t color = 0) = 0;
    virtual PresentationTiming present() = 0;
    virtual void markExternalDraw() {}

    // --- Fast-Path Memory Primitives ---
    /**
     * @brief Blits a rectangular RGB565 buffer directly onto the surface.
     * @param src Source pixel buffer
     * @param x Target destination X
     * @param y Target destination Y
     * @param w Blit width
     * @param h Blit height
     * @param stridePixels Pitch of source buffer in pixels (-1 = contiguous w)
     */
    virtual void blit565(
        const uint16_t* src,
        int16_t x,
        int16_t y,
        int16_t w,
        int16_t h,
        int16_t stridePixels = -1
    ) = 0;

    // --- Scoped Borrowed Canvas View (Optional Performance Escape Hatch) ---
    virtual CanvasView acquireCanvas() = 0;
    virtual void releaseCanvas() = 0;
    virtual bool hasCanvas() const = 0;

    // --- Telemetry & Capabilities ---
    virtual CanvasStorage canvasStorage() const = 0;
    virtual PresentationStrategy presentationStrategy() const = 0;
    virtual size_t memoryUsageBytes() const = 0;

    // Invariant physical dimensions (independent of orientation)
    inline int16_t physicalWidth() const { return m_physW; }
    inline int16_t physicalHeight() const { return m_physH; }

    // Backward-compatible color helper (5-bit R, 6-bit G, 5-bit B)
    static inline constexpr uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

protected:
    int16_t m_physW;
    int16_t m_physH;
};
