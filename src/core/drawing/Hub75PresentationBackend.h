/**
 * @file Hub75PresentationBackend.h
 * @brief Concrete presentation backend interfacing with MatrixEngine HUB75 DMA.
 */
#pragma once
#include "IPresentationBackend.h"

class MatrixEngine;

class Hub75PresentationBackend : public IPresentationBackend {
public:
    Hub75PresentationBackend(MatrixEngine* engine, uint16_t width, uint16_t height,
                             uint8_t colorDepth = 8, bool doubleBuffer = true);
    virtual ~Hub75PresentationBackend() = default;

    Hub75DmaTarget acquireDmaTarget() override;
    PresentationTiming commit(const PresentationPolicy& policy) override;
    size_t calculateDmaBytes() const override;

    MatrixEngine* getMatrixEngine() const { return _engine; }

    IPresentationSynchronizer* getSynchronizer() override { return _synchronizer; }
    void setSynchronizer(IPresentationSynchronizer* synchronizer) override { _synchronizer = synchronizer; }

private:
    MatrixEngine* _engine = nullptr;
    IPresentationSynchronizer* _synchronizer = nullptr;
    uint16_t _width = 64;
    uint16_t _height = 32;
    uint8_t _colorDepth = 8;
    bool _doubleBuffer = true;
};
