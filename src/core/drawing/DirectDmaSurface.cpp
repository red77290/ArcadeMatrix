/**
 * @file DirectDmaSurface.cpp
 * @brief Implementation of DirectDmaSurface.
 */
#include "DirectDmaSurface.h"

DirectDmaSurface::DirectDmaSurface(MatrixPanel_I2S_DMA* matrix, int16_t width, int16_t height, bool singleBuffer)
    : IDrawingSurface(width, height, width, height)
    , _matrix(matrix)
    , _strategy(singleBuffer ? PresentationStrategy::DIRECT_DMA_SINGLE : PresentationStrategy::DIRECT_DMA_DOUBLE)
{
    // Estimated DMA buffer size (2 buffers if double-buffered)
    size_t singleBuf = (size_t)width * height * 4;
    _dmaBytes = singleBuffer ? singleBuf : (singleBuf * 2);
}

void DirectDmaSurface::setRotation(uint8_t r) {
    IDrawingSurface::setRotation(r);
    if (_matrix) {
        _matrix->setRotation(r);
    }
}

void DirectDmaSurface::clear(uint16_t color) {
    fillScreen(color);
}

void DirectDmaSurface::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!_matrix) return;
    _matrix->drawPixel(x, y, color);
}

void DirectDmaSurface::fillScreen(uint16_t color) {
    if (_matrix) _matrix->fillScreen(color);
}

void DirectDmaSurface::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (_matrix) _matrix->drawFastVLine(x, y, h, color);
}

void DirectDmaSurface::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (_matrix) _matrix->drawFastHLine(x, y, w, color);
}

void DirectDmaSurface::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (_matrix) _matrix->fillRect(x, y, w, h, color);
}

void DirectDmaSurface::blit565(const uint16_t* src, int16_t x, int16_t y,
                              int16_t w, int16_t h, int16_t stridePixels) {
    if (!_matrix || src == nullptr || w <= 0 || h <= 0) return;
    if (stridePixels <= 0) stridePixels = w;

    for (int16_t row = 0; row < h; ++row) {
        const uint16_t* srcRow = src + (row * stridePixels);
        for (int16_t col = 0; col < w; ++col) {
            drawPixel(x + col, y + row, srcRow[col]);
        }
    }
}

CanvasView DirectDmaSurface::acquireCanvas() {
    return CanvasView{nullptr, 0, 0, 0};
}

void DirectDmaSurface::releaseCanvas() {
}

PresentationTiming DirectDmaSurface::present() {
    PresentationTiming timing;
    if (!_matrix) return timing;
    uint32_t t0 = micros();
    
    if (_strategy == PresentationStrategy::DIRECT_DMA_DOUBLE) {
        _matrix->flipDMABuffer();
    }
    
    timing.totalPresentUs = (micros() - t0);
    return timing;
}
