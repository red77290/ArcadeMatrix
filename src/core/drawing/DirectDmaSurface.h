/**
 * @file DirectDmaSurface.h
 * @brief Direct DMA Drawing Surface implementation (v3 backward-compatible mode).
 */
#pragma once

#include "IDrawingSurface.h"
#include "SurfaceCoordinates.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

class MatrixEngine;

class DirectDmaSurface : public IDrawingSurface {
public:
    DirectDmaSurface(MatrixPanel_I2S_DMA* matrix, int16_t width, int16_t height, bool singleBuffer = false, MatrixEngine* engine = nullptr);
    virtual ~DirectDmaSurface() = default;

    void setRotation(uint8_t r) override;
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
    bool hasCanvas() const override { return false; }

    PresentationTiming present() override;
    void markExternalDraw() override;

    PresentationStrategy presentationStrategy() const override { return _strategy; }
    CanvasStorage canvasStorage() const override { return CanvasStorage::NONE; }
    size_t memoryUsageBytes() const override { return _dmaBytes; }

    MatrixPanel_I2S_DMA* getUnderlyingMatrix() const { return _matrix; }

private:
    MatrixPanel_I2S_DMA* _matrix = nullptr;
    MatrixEngine* _matrixEngine = nullptr;
    PresentationStrategy _strategy = PresentationStrategy::DIRECT_DMA_DOUBLE;
    size_t _dmaBytes = 0;
};
