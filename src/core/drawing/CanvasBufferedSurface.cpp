/**
 * @file CanvasBufferedSurface.cpp
 * @brief Implementation of CanvasBufferedSurface.
 */
#include "CanvasBufferedSurface.h"
#include "../MatrixEngine.h"
#include "../Logger.h"
#include <string.h>

CanvasBufferedSurface::CanvasBufferedSurface(int16_t width, int16_t height,
                                             CanvasStorage storage,
                                             MatrixEngine* matrixEngine,
                                             bool singleDma)
    : IDrawingSurface(width, height, width, height)
    , _storage(storage)
    , _matrixEngine(matrixEngine)
{
    _strategy = singleDma ? PresentationStrategy::CANVAS_BURST_SINGLE : PresentationStrategy::CANVAS_BURST_DOUBLE;
    size_t pixelCount = (size_t)width * height;
    _canvasBytes = pixelCount * sizeof(uint16_t);

    if (_storage == CanvasStorage::PSRAM) {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        _canvas = static_cast<uint16_t*>(heap_caps_malloc(_canvasBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
        if (!_canvas) {
            LOGW("CanvasBufferedSurface", "PSRAM canvas allocation failed, falling back to internal SRAM");
            _canvas = static_cast<uint16_t*>(malloc(_canvasBytes));
            _storage = CanvasStorage::SRAM;
        }
    } else {
        _canvas = static_cast<uint16_t*>(malloc(_canvasBytes));
    }

    if (_canvas) {
        memset(_canvas, 0, _canvasBytes);
        LOGI("CanvasBufferedSurface", "Allocated %u KB canvas in %s (%ux%u)",
             (unsigned)(_canvasBytes / 1024),
             _storage == CanvasStorage::PSRAM ? "PSRAM" : "SRAM",
             width, height);
    } else {
        LOGE("CanvasBufferedSurface", "CRITICAL: Failed to allocate %u bytes for canvas!", (unsigned)_canvasBytes);
    }

    size_t dmaFrame = (size_t)width * height * 4;
    _dmaBytes = singleDma ? dmaFrame : (dmaFrame * 2);
}

CanvasBufferedSurface::~CanvasBufferedSurface() {
    if (_canvas) {
        free(_canvas);
        _canvas = nullptr;
    }
}

void CanvasBufferedSurface::clear(uint16_t color) {
    fillScreen(color);
}

void CanvasBufferedSurface::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!_canvas) return;
    Point p = SurfaceCoordinates::logicalToPhysical(x, y, physicalWidth(), physicalHeight(), getRotation());
    if (p.x < 0 || p.x >= physicalWidth() || p.y < 0 || p.y >= physicalHeight()) return;

    _canvas[(size_t)p.y * physicalWidth() + p.x] = color;
}

void CanvasBufferedSurface::fillScreen(uint16_t color) {
    if (!_canvas) return;
    if (color == 0) {
        memset(_canvas, 0, _canvasBytes);
    } else {
        size_t total = (size_t)physicalWidth() * physicalHeight();
        for (size_t i = 0; i < total; ++i) {
            _canvas[i] = color;
        }
    }
}

void CanvasBufferedSurface::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (h <= 0) return;
    for (int16_t i = 0; i < h; ++i) {
        drawPixel(x, y + i, color);
    }
}

void CanvasBufferedSurface::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (w <= 0) return;
    for (int16_t i = 0; i < w; ++i) {
        drawPixel(x + i, y, color);
    }
}

void CanvasBufferedSurface::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    for (int16_t row = 0; row < h; ++row) {
        for (int16_t col = 0; col < w; ++col) {
            drawPixel(x + col, y + row, color);
        }
    }
}

void CanvasBufferedSurface::blit565(const uint16_t* src, int16_t x, int16_t y,
                                    int16_t w, int16_t h, int16_t stridePixels) {
    if (!_canvas || !src || w <= 0 || h <= 0) return;
    if (stridePixels <= 0) stridePixels = w;

    int16_t pw = physicalWidth();
    int16_t ph = physicalHeight();

    // Fast path: unrotated row-by-row blit with clipping
    if (getRotation() == 0) {
        int16_t x0 = (x < 0) ? 0 : x;
        int16_t y0 = (y < 0) ? 0 : y;
        int16_t x1 = (x + w > pw) ? pw : (x + w);
        int16_t y1 = (y + h > ph) ? ph : (y + h);
        if (x0 < x1 && y0 < y1) {
            int16_t clippedW = x1 - x0;
            size_t rowBytes = (size_t)clippedW * sizeof(uint16_t);
            for (int16_t row = y0; row < y1; ++row) {
                int16_t srcY = row - y;
                int16_t srcX = x0 - x;
                const uint16_t* srcRow = src + ((size_t)srcY * stridePixels + srcX);
                uint16_t* dstRow = _canvas + ((size_t)row * pw + x0);
                memcpy(dstRow, srcRow, rowBytes);
            }
            return;
        }
    }

    for (int16_t row = 0; row < h; ++row) {
        const uint16_t* srcRow = src + (row * stridePixels);
        for (int16_t col = 0; col < w; ++col) {
            drawPixel(x + col, y + row, srcRow[col]);
        }
    }
}

CanvasView CanvasBufferedSurface::acquireCanvas() {
    _canvasBorrowed = true;
    return CanvasView{
        _canvas,
        (uint16_t)physicalWidth(),
        (uint16_t)physicalHeight(),
        (size_t)physicalWidth()
    };
}

void CanvasBufferedSurface::releaseCanvas() {
    _canvasBorrowed = false;
}

PresentationTiming CanvasBufferedSurface::present() {
    PresentationTiming timing;
    if (!_canvas || !_matrixEngine) return timing;
    uint32_t t0 = micros();

    // 1. Bulk row-burst blit into HUB75 DMA back-buffer
    _matrixEngine->blitCanvas565(_canvas, physicalWidth(), physicalHeight());
    uint32_t t1 = micros();

    // 2. Hardware presentation synchronization / buffer flip
    _matrixEngine->present();
    uint32_t t2 = micros();

    timing.encodeUs = (t1 - t0);
    timing.totalPresentUs = (t2 - t0);
    return timing;
}

void CanvasBufferedSurface::markExternalDraw() {
    if (_matrixEngine) {
        _matrixEngine->markExternalDraw();
    }
}
