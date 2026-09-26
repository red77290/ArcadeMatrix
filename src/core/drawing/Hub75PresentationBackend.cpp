/**
 * @file Hub75PresentationBackend.cpp
 * @brief Implementation of Hub75PresentationBackend.
 */
#include "Hub75PresentationBackend.h"
#include "../MatrixEngine.h"

#if defined(SPIRAM_DMA_BUFFER)
#include "rom/cache.h"
#endif

Hub75PresentationBackend::Hub75PresentationBackend(MatrixEngine* engine, uint16_t width, uint16_t height,
                                                   uint8_t colorDepth, bool doubleBuffer)
    : _engine(engine), _width(width), _height(height), _colorDepth(colorDepth), _doubleBuffer(doubleBuffer) {
}

Hub75DmaTarget Hub75PresentationBackend::acquireDmaTarget() {
    Hub75DmaTarget target;
    target.width = _width;
    target.height = _height;
    target.rowsPerFrame = _height / 2;
    target.colorDepth = _colorDepth;
    target.activeBufferIndex = _engine ? (uint8_t)(_engine->flipCount() & 1u) : 0;
    target.bufferBytes = calculateDmaBytes();
    target.strideBytes = _width * sizeof(uint16_t);

    FastMatrixPanel* panel = _engine ? _engine->getFastPanel() : nullptr;
    if (panel) {
        target.rowAccessor = [](void* ctx, uint8_t row, uint8_t plane) -> uint16_t* {
            auto* p = static_cast<FastMatrixPanel*>(ctx);
            return p ? p->getBackbufferRowPlane(row, plane) : nullptr;
        };
        target.accessorCtx = panel;
        target.buffer = reinterpret_cast<uint8_t*>(panel->getBackbufferRowPlane(0, 0));
        target.lutR = panel->getLutR();
        target.lutG = panel->getLutG();
        target.lutB = panel->getLutB();
    }
    return target;
}

PresentationTiming Hub75PresentationBackend::commit(const PresentationPolicy& policy) {
    PresentationTiming timing;
    if (!_engine) return timing;

    uint32_t t0 = micros();

    // 1. Wait for safe presentation window if synchronizer attached
    if (_synchronizer) {
        uint32_t t_sync = micros();
        _synchronizer->waitForSafeWindow(policy.maxBlankUs);
        timing.waitForSafeWindowUs = micros() - t_sync;
    }

    // 2. Perform cache write-back if PSRAM DMA buffer is active
#if defined(SPIRAM_DMA_BUFFER)
    FastMatrixPanel* panel = _engine->getFastPanel();
    if (panel) {
        uint16_t rpf = _height / 2;
        for (uint8_t y = 0; y < rpf; y++) {
            for (uint8_t p = 0; p < _colorDepth; p++) {
                uint16_t* dmaRow = panel->getBackbufferRowPlane(y, p);
                if (dmaRow) {
                    Cache_WriteBack_Addr((uint32_t)dmaRow, (uint32_t)_width * sizeof(uint16_t));
                }
            }
        }
    }
#endif

    // 3. Hardware buffer flip
    uint32_t t_trans = micros();
    _engine->present();
    timing.transferUs = micros() - t_trans;

    timing.totalPresentUs = (micros() - t0);
    return timing;
}

size_t Hub75PresentationBackend::calculateDmaBytes() const {
    size_t singleFrame = (size_t)_width * _height * 4;
    return _doubleBuffer ? (singleFrame * 2) : singleFrame;
}
