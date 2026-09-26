/**
 * @file Hub75PresentationBackend.cpp
 * @brief Implementation of Hub75PresentationBackend.
 */
#include "Hub75PresentationBackend.h"
#include "../MatrixEngine.h"

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
    }
    return target;
}

PresentationTiming Hub75PresentationBackend::commit(const PresentationPolicy& policy) {
    PresentationTiming timing;
    if (!_engine) return timing;

    uint32_t t0 = micros();
    _engine->present();
    uint32_t t1 = micros();

    timing.totalPresentUs = (t1 - t0);
    return timing;
}

size_t Hub75PresentationBackend::calculateDmaBytes() const {
    size_t singleFrame = (size_t)_width * _height * 4;
    return _doubleBuffer ? (singleFrame * 2) : singleFrame;
}
