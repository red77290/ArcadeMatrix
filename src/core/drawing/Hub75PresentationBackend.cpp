/**
 * @file Hub75PresentationBackend.cpp
 * @brief Implementation of Hub75PresentationBackend.
 */
#include "Hub75PresentationBackend.h"
#include "DmaMemoryLayout.h"
#include "PresentationTimingModel.h"
#include "Hub75BulkEncoder.h"
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
    target.strideBytes = DmaMemoryLayout(_width, _height, _colorDepth, _doubleBuffer).stride();

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
    if (!_engine) {
        timing.result = PresentationResult::BackendUnavailable;
        return timing;
    }

    uint32_t t0 = micros();

    // 1. Flush CPU cache lines to SPIRAM DMA buffer BEFORE waiting for safe window
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

    // 2. Wait for safe presentation window immediately prior to hardware buffer flip
    uint32_t estimatedTransferUs = PresentationTimingModel::estimateTransferUs(_width, _height, _colorDepth, !_doubleBuffer);
    if (_synchronizer) {
        uint32_t t_sync = micros();
        SafeWindowResult sw = _synchronizer->waitForSafeWindow(estimatedTransferUs, policy.safeWindowTimeoutUs);
        timing.waitForSafeWindowUs = micros() - t_sync;
        if (!sw.acquired || (sw.availableWindowUs > 0 && sw.availableWindowUs < estimatedTransferUs)) {
            timing.result = PresentationResult::SafeWindowTimeout;
            timing.totalPresentUs = micros() - t0;
            return timing;
        }
    }

    // 3. Transient blanking if permitted by policy
    uint32_t blankStart = 0;
    if (policy.allowBlanking) {
        blankStart = micros();
        _engine->setBlank(true);
    }

    // 4. Hardware buffer flip
    uint32_t t_trans = micros();
    _engine->present();
    timing.transferUs = micros() - t_trans;

    // 5. Restore unblanked output and enforce blanking budget
    if (policy.allowBlanking && blankStart > 0) {
        _engine->setBlank(false);
        timing.blankUs = micros() - blankStart;
        if (policy.maxBlankUs > 0 && timing.blankUs > policy.maxBlankUs) {
            timing.result = PresentationResult::BlankBudgetExceeded;
        }
    }

    timing.totalPresentUs = (micros() - t0);
    if (timing.result == PresentationResult::Ok && policy.maxFrameUs > 0 && timing.totalPresentUs > policy.maxFrameUs) {
        timing.result = PresentationResult::FrameBudgetExceeded;
    }

    return timing;
}

PresentationTiming Hub75PresentationBackend::presentCanvas(
    const uint16_t* canvas,
    uint16_t canvasWidth,
    uint16_t canvasHeight,
    PresentationStrategy strategy,
    const PresentationPolicy& policy)
{
    PresentationTiming timing;
    if (!canvas) {
        timing.result = PresentationResult::EncodingError;
        return timing;
    }
    if (!_engine) {
        timing.result = PresentationResult::BackendUnavailable;
        return timing;
    }

    uint32_t t0 = micros();

    Hub75DmaTarget target = acquireDmaTarget();
    if (!target.rowAccessor && !target.buffer) {
        timing.result = PresentationResult::DmaTargetUnavailable;
        return timing;
    }
    if (target.width == 0 || target.height == 0 || target.rowsPerFrame == 0) {
        timing.result = PresentationResult::InvalidTarget;
        return timing;
    }

    Hub75EncodingParams params;
    params.colorDepth = target.colorDepth;
    params.rowsPerFrame = target.rowsPerFrame;
    params.width = canvasWidth;
    params.height = canvasHeight;
    params.lutR = target.lutR;
    params.lutG = target.lutG;
    params.lutB = target.lutB;
    params.rotation = 0; // Canvas is already maintained in physical orientation

    bool isSingle = (strategy == PresentationStrategy::CANVAS_BURST_SINGLE) || !_doubleBuffer;

    if (!isSingle) {
        // =========================================================================
        // DOUBLE BUFFER PIPELINE:
        // 1. Encode into inactive back-buffer (tear-free)
        // 2. Cache writeback (SPIRAM)
        // 3. Safe-window synchronization (instantaneous swap)
        // 4. Swap buffers
        // =========================================================================
        uint32_t t_enc = micros();
        Hub75BulkEncoder::encode(canvas, canvasWidth, target, params);
        timing.encodeUs = micros() - t_enc;

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

        uint32_t estimatedTransferUs = PresentationTimingModel::estimateTransferUs(_width, _height, _colorDepth, false);
        if (_synchronizer) {
            uint32_t t_sync = micros();
            SafeWindowResult sw = _synchronizer->waitForSafeWindow(estimatedTransferUs, policy.safeWindowTimeoutUs);
            timing.waitForSafeWindowUs = micros() - t_sync;
            if (!sw.acquired || (sw.availableWindowUs > 0 && sw.availableWindowUs < estimatedTransferUs)) {
                timing.result = PresentationResult::SafeWindowTimeout;
                timing.totalPresentUs = micros() - t0;
                return timing;
            }
        }

        uint32_t blankStart = 0;
        if (policy.allowBlanking) {
            blankStart = micros();
            _engine->setBlank(true);
        }

        uint32_t t_trans = micros();
        _engine->present();
        timing.transferUs = micros() - t_trans;

        if (policy.allowBlanking && blankStart > 0) {
            _engine->setBlank(false);
            timing.blankUs = micros() - blankStart;
            if (policy.maxBlankUs > 0 && timing.blankUs > policy.maxBlankUs && !policy.degradedBlankingPermitted) {
                timing.result = PresentationResult::BlankBudgetExceeded;
            }
        }
    } else {
        // =========================================================================
        // SINGLE BUFFER PIPELINE:
        // Writing directly to the active scanning buffer IS the critical section.
        // 1. Compute dynamic transfer estimate (encode + writeback)
        // 2. Pre-flight budget validation: reject BEFORE blacking out screen if budget exceeded
        // 3. Wait for safe window (V-Blank / scan pause)
        // 4. Transient blanking BEFORE writing begins (guarantees 0 tearing)
        // 5. Encode directly into DMA buffer
        // 6. Cache writeback (SPIRAM)
        // 7. Refresh / commit
        // 8. Unblank display
        // =========================================================================
        bool isSpiram = false;
#if defined(SPIRAM_DMA_BUFFER)
        isSpiram = true;
#endif
        uint32_t estimatedTransferUs = PresentationTimingModel::estimateTransferUs(
            _width, _height, _colorDepth, true, isSpiram
        );

        // Pre-flight check: in single-buffer mode, if estimated transfer (encode + writeback)
        // exceeds maxBlankUs and degraded blanking is not explicitly permitted, reject preemptively!
        if (policy.allowBlanking && policy.maxBlankUs > 0 && estimatedTransferUs > policy.maxBlankUs && !policy.degradedBlankingPermitted) {
            timing.result = PresentationResult::BlankBudgetExceeded;
            timing.totalPresentUs = micros() - t0;
            return timing;
        }

        if (_synchronizer) {
            uint32_t t_sync = micros();
            SafeWindowResult sw = _synchronizer->waitForSafeWindow(estimatedTransferUs, policy.safeWindowTimeoutUs);
            timing.waitForSafeWindowUs = micros() - t_sync;
            if (!sw.acquired || (sw.availableWindowUs > 0 && sw.availableWindowUs < estimatedTransferUs)) {
                timing.result = PresentationResult::SafeWindowTimeout;
                timing.totalPresentUs = micros() - t0;
                return timing;
            }
        }

        uint32_t blankStart = 0;
        if (policy.allowBlanking) {
            blankStart = micros();
            _engine->setBlank(true);
        }

        uint32_t t_enc = micros();
        Hub75BulkEncoder::encode(canvas, canvasWidth, target, params);
        timing.encodeUs = micros() - t_enc;

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

        uint32_t t_trans = micros();
        _engine->present();
        timing.transferUs = micros() - t_trans;

        if (policy.allowBlanking && blankStart > 0) {
            _engine->setBlank(false);
            timing.blankUs = micros() - blankStart;
            if (policy.maxBlankUs > 0 && timing.blankUs > policy.maxBlankUs) {
                timing.result = PresentationResult::BlankBudgetExceeded;
            }
        }
    }

    timing.totalPresentUs = (micros() - t0);
    if (timing.result == PresentationResult::Ok && policy.maxFrameUs > 0 && timing.totalPresentUs > policy.maxFrameUs) {
        timing.result = PresentationResult::FrameBudgetExceeded;
    }

    return timing;
}

size_t Hub75PresentationBackend::calculateDmaBytes() const {
    return DmaMemoryLayout::calculateTotalBytes(_width, _height, _colorDepth, _doubleBuffer);
}

void Hub75PresentationBackend::markExternalDraw() {
    if (_engine) {
        _engine->markExternalDraw();
    }
}
