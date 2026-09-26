/**
 * @file MockPresentationBackend.h
 * @brief Host-testable mock presentation backend for unit testing without hardware.
 */
#pragma once
#include "IPresentationBackend.h"
#include "DmaMemoryLayout.h"
#include <vector>

class MockPresentationBackend : public IPresentationBackend {
public:
    MockPresentationBackend(uint16_t width, uint16_t height, uint8_t depth = 8, bool doubleBuffer = false)
        : _width(width), _height(height), _depth(depth), _doubleBuffer(doubleBuffer) {
        _dmaBytes = DmaMemoryLayout::calculateTotalBytes(width, height, depth, doubleBuffer);
        _buffer.resize(_dmaBytes, 0);
    }
    virtual ~MockPresentationBackend() = default;

    Hub75DmaTarget acquireDmaTarget() override {
        Hub75DmaTarget target;
        if (simulateInvalidTarget) {
            return target; // Empty target
        }
        target.buffer = _buffer.data();
        target.bufferBytes = _buffer.size();
        target.strideBytes = _width * sizeof(uint16_t);
        target.width = _width;
        target.height = _height;
        target.rowsPerFrame = _height / 2;
        target.colorDepth = _depth;
        target.activeBufferIndex = 0;
        return target;
    }

    PresentationTiming commit(const PresentationPolicy& policy) override {
        PresentationTiming timing;
        _commitCount++;

        // 1. Safe window synchronization
        if (_synchronizer) {
            SafeWindowResult sw = _synchronizer->waitForSafeWindow(simulatedTransferUs, policy.maxBlankUs);
            timing.waitForSafeWindowUs = sw.waitUs;
            if (!sw.acquired || (sw.availableWindowUs > 0 && sw.availableWindowUs < simulatedTransferUs)) {
                timing.result = PresentationResult::SafeWindowTimeout;
                timing.totalPresentUs = timing.waitForSafeWindowUs;
                return timing;
            }
        }

        // 2. Blanking enforcement
        if (policy.allowBlanking && simulatedBlankUs > 0) {
            timing.blankUs = simulatedBlankUs;
            if (policy.maxBlankUs > 0 && timing.blankUs > policy.maxBlankUs) {
                timing.result = PresentationResult::BlankBudgetExceeded;
            }
        } else {
            timing.blankUs = 0;
        }

        // 3. Transfer & frame budget enforcement
        timing.transferUs = simulatedTransferUs;
        timing.totalPresentUs = (simulatedTotalUs > 0) ? simulatedTotalUs : (timing.waitForSafeWindowUs + timing.blankUs + timing.transferUs);

        if (timing.result == PresentationResult::Ok && policy.maxFrameUs > 0 && timing.totalPresentUs > policy.maxFrameUs) {
            timing.result = PresentationResult::FrameBudgetExceeded;
        }

        return timing;
    }

    size_t calculateDmaBytes() const override {
        return _dmaBytes;
    }

    IPresentationSynchronizer* getSynchronizer() override { return _synchronizer; }
    void setSynchronizer(IPresentationSynchronizer* synchronizer) override { _synchronizer = synchronizer; }

    uint32_t getCommitCount() const { return _commitCount; }
    const std::vector<uint8_t>& getBuffer() const { return _buffer; }
    std::vector<uint8_t>& getBuffer() { return _buffer; }

    // Simulation overrides for testing
    uint32_t simulatedBlankUs = 0;
    uint32_t simulatedTransferUs = 150;
    uint32_t simulatedTotalUs = 150;
    bool simulateInvalidTarget = false;

private:
    uint16_t _width;
    uint16_t _height;
    uint8_t _depth;
    bool _doubleBuffer = false;
    size_t _dmaBytes;
    std::vector<uint8_t> _buffer;
    uint32_t _commitCount = 0;
    IPresentationSynchronizer* _synchronizer = nullptr;
};
