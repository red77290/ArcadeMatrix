/**
 * @file CanvasBufferedSurface.h
 * @brief High-performance Canvas Buffered Drawing Surface implementation.
 */
#pragma once

#include "IDrawingSurface.h"
#include "SurfaceCoordinates.h"
#include "IPresentationBackend.h"
#include <esp_heap_caps.h>

class CanvasBufferedSurface : public IDrawingSurface {
public:
    CanvasBufferedSurface(int16_t width, int16_t height, CanvasStorage storage,
                          IPresentationBackend* backend, bool singleDma = false);
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

    uint32_t externalDrawGeneration() const override { return _externalDrawGeneration; }
    uint32_t flipCount() const override { return _flipCount; }
    bool isDoubleBuffered() const override { return _strategy == PresentationStrategy::CANVAS_BURST_DOUBLE; }

    bool isDirty() const override { return _dirty; }
    void markDirty() override { _dirty = true; }
    void clearDirty() { _dirty = false; }

    uint16_t* getRawCanvasBuffer() const { return _canvas; }

    void setPresentationBackend(IPresentationBackend* backend) override { _backend = backend; }
    IPresentationBackend* getPresentationBackend() const override { return _backend; }
    void setPresentationPolicy(const PresentationPolicy& policy) { _policy = policy; }
    const PresentationPolicy& getPresentationPolicy() const { return _policy; }

private:
    inline void markModified() noexcept { _dirty = true; }

    uint16_t* _canvas = nullptr;
    CanvasStorage _storage = CanvasStorage::SRAM;
    PresentationStrategy _strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
    IPresentationBackend* _backend = nullptr;
    PresentationPolicy _policy;
    size_t _canvasBytes = 0;
    size_t _dmaBytes = 0;
    uint32_t _flipCount = 0;
    uint32_t _externalDrawGeneration = 0;
    bool _canvasBorrowed = false;
    bool _dirty = false;
};
