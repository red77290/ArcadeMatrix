/**
 * @file MockPresentationBackend.h
 * @brief Host-testable mock presentation backend for unit testing without hardware.
 */
#pragma once
#include "IPresentationBackend.h"
#include <vector>

class MockPresentationBackend : public IPresentationBackend {
public:
    MockPresentationBackend(uint16_t width, uint16_t height, uint8_t depth = 8)
        : _width(width), _height(height), _depth(depth) {
        _dmaBytes = (size_t)width * height * 4;
        _buffer.resize(_dmaBytes, 0);
    }
    virtual ~MockPresentationBackend() = default;

    Hub75DmaTarget acquireDmaTarget() override {
        Hub75DmaTarget target;
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
        timing.totalPresentUs = 150;
        _commitCount++;
        return timing;
    }

    size_t calculateDmaBytes() const override {
        return _dmaBytes;
    }

    uint32_t getCommitCount() const { return _commitCount; }

private:
    uint16_t _width;
    uint16_t _height;
    uint8_t _depth;
    size_t _dmaBytes;
    std::vector<uint8_t> _buffer;
    uint32_t _commitCount = 0;
};
