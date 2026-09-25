/**
 * @file CanvasBufferedSurface.h
 * @brief High-performance Canvas Buffered Drawing Surface implementation.
 */
#pragma once

#include "IDrawingSurface.h"
#include "SurfaceCoordinates.h"
#include <esp_heap_caps.h>

class MatrixEngine;

class CanvasBufferedSurface : public IDrawingSurface {
public:
    CanvasBufferedSurface(int16_t width, int16_t height, CanvasStorage storage,
                          MatrixEngine* matrixEngine, bool singleDma = false);
    virtual ~CanvasBufferedSurface();

    void clear(uint16_t color = 0) override;
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;

    void blit565(const uint16_t* src, int16_t x, int16_t y,
                 int16_t w, int16_t h, int16_t stridePixels = -1) override;

    CanvasView acquireCanvas() override;
    void releaseCanvas() override;
    bool hasCanvas() const override { return _canvas != nullptr; }

    PresentationTiming present() override;
    void markExternalDraw() override;

    PresentationStrategy presentationStrategy() const override { return _strategy; }
    CanvasStorage canvasStorage() const override { return _storage; }
    size_t memoryUsageBytes() const override { return _canvasBytes + _dmaBytes; }

    uint16_t* getRawCanvasBuffer() const { return _canvas; }

private:
    uint16_t* _canvas = nullptr;
    CanvasStorage _storage = CanvasStorage::SRAM;
    PresentationStrategy _strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
    MatrixEngine* _matrixEngine = nullptr;
    size_t _canvasBytes = 0;
    size_t _dmaBytes = 0;
    bool _canvasBorrowed = false;
};
