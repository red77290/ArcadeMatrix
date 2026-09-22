#pragma once

#include <Arduino.h>
#include <array>
#include <memory>
#include "EngineRetirementQueue.h"
#include "Logger.h"

/**
 * @class Core0LifecycleDispatcher
 * @brief Coordinates asynchronous resource reclamation and engine destruction exclusively on Core 0.
 *
 * CONTRACT:
 * - Core 1 pushes retired engines to the bounded SPSC retirement queue via retire().
 * - A lightweight worker task on Core 0 wakes on notification to drain the queue.
 * - Each retired engine executes shutdownForDestruction() cooperatively on Core 0.
 * - If shutdown succeeds (tasks stopped, sockets closed), unique_ptr::reset() safely frees memory on Core 0.
 * - If shutdown times out, the engine is quarantined in a bounded list (no use-after-free, no forced kill).
 */
class Core0LifecycleDispatcher {
public:
    static Core0LifecycleDispatcher& instance() {
        static Core0LifecycleDispatcher s_instance;
        return s_instance;
    }

    /**
     * @brief Initializes the Core 0 background lifecycle worker task.
     */
    void begin();

    /**
     * @brief Handover an engine from Core 1 to Core 0 for destruction.
     * Non-blocking, allocation-free.
     * @return true if successfully queued, false if queue is full (caller must retain and retry).
     */
    bool retire(std::unique_ptr<IEngine>&& engine) {
        bool pushed = _retireQueue.push(std::move(engine));
        if (pushed && _lifecycleTaskHandle) {
            xTaskNotifyGive(_lifecycleTaskHandle);
        }
        return pushed;
    }

    /**
     * @brief Drains pending retired engines on Core 0.
     * CONTRACT: Must be executed on Core 0 only.
     */
    void processRetirements();

    /**
     * @brief Wake up Core 0 lifecycle dispatcher worker to process retirements or deferred publication.
     */
    void notify() {
        if (_lifecycleTaskHandle) {
            xTaskNotifyGive(_lifecycleTaskHandle);
        }
    }

    size_t getQuarantineCount() const { return _quarantineCount; }

private:
    Core0LifecycleDispatcher() = default;
    ~Core0LifecycleDispatcher() = default;
    Core0LifecycleDispatcher(const Core0LifecycleDispatcher&) = delete;
    Core0LifecycleDispatcher& operator=(const Core0LifecycleDispatcher&) = delete;

    static void lifecycleTaskFunc(void* param);

    EngineRetirementQueue<8> _retireQueue;
    static constexpr size_t QUARANTINE_CAPACITY = 4;
    std::array<std::unique_ptr<IEngine>, QUARANTINE_CAPACITY> _quarantine{};
    size_t _quarantineCount = 0;
    TaskHandle_t _lifecycleTaskHandle = nullptr;
};
