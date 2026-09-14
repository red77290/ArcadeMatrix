#pragma once

#include <Arduino.h>
#include <array>
#include <atomic>
#include <memory>
#include "../../include/core/EngineContract.h"

/**
 * @class EngineRetirementQueue
 * @brief Lock-free, bounded Single-Producer Single-Consumer (SPSC) queue
 *        transferring retired engine instances from Core 1 (producer) to Core 0 (consumer).
 *
 * CONTRACT:
 * - Fixed capacity (8 slots). Zero dynamic memory allocation after construction.
 * - Producer (Core 1): Non-blocking push(). If the queue is full, returns false
 *   without blocking Core 1's 60 FPS hot path. The caller retains ownership and retries.
 * - Consumer (Core 0): Drains retired engines for cooperative shutdown and destruction.
 * - Memory ordering: release-acquire synchronization guarantees slot validity before tail publication.
 */
template <size_t Capacity = 8>
class EngineRetirementQueue {
public:
    static constexpr size_t BUFFER_SIZE = Capacity + 1; // 1 slot kept empty to distinguish full vs empty

    EngineRetirementQueue() : _head(0), _tail(0) {}

    /**
     * @brief Pushes a retired engine into the queue from Core 1.
     * @param engine Rvalue reference to the engine unique_ptr.
     * @return true if successfully enqueued, false if full (non-blocking).
     */
    bool push(std::unique_ptr<IEngine>&& engine) {
        if (!engine) return true;

        const size_t tail = _tail.load(std::memory_order_relaxed);
        const size_t nextTail = (tail + 1) % BUFFER_SIZE;

        if (nextTail == _head.load(std::memory_order_acquire)) {
            // Queue full: do NOT block Core 1. Return false so caller preserves engine for next cycle.
            return false;
        }

        _slots[tail] = std::move(engine);
        _tail.store(nextTail, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops a retired engine from the queue on Core 0.
     * @param outEngine Output destination for the retired engine unique_ptr.
     * @return true if an engine was popped, false if queue was empty.
     */
    bool pop(std::unique_ptr<IEngine>& outEngine) {
        const size_t head = _head.load(std::memory_order_relaxed);

        if (head == _tail.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }

        outEngine = std::move(_slots[head]);
        _head.store((head + 1) % BUFFER_SIZE, std::memory_order_release);
        return true;
    }

    bool isEmpty() const {
        return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
    }

private:
    std::array<std::unique_ptr<IEngine>, BUFFER_SIZE> _slots;
    std::atomic<size_t> _head;
    std::atomic<size_t> _tail;
};
