#include "Core0Lifecycle.h"
#include "ConfigLoader.h"

void Core0LifecycleDispatcher::lifecycleTaskFunc(void* param) {
    auto* self = static_cast<Core0LifecycleDispatcher*>(param);
    while (true) {
        // Sleep until notified by Core 1 or check every 1000ms
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        self->processRetirements();
        extern ConfigLoader config;
        config.checkDeferredPublish();
    }
}

void Core0LifecycleDispatcher::begin() {
    if (!_lifecycleTaskHandle) {
        BaseType_t ret = xTaskCreatePinnedToCore(
            lifecycleTaskFunc,
            "Lifecycle0",
            3072,
            this,
            1,
            &_lifecycleTaskHandle,
            0 // Core 0
        );
        if (ret != pdPASS) {
            LOGE("Core0Lifecycle", "Failed to create Lifecycle0 task on Core 0!");
            _lifecycleTaskHandle = nullptr;
        } else {
            LOGI("Core0Lifecycle", "Core0LifecycleDispatcher initialized on Core 0.");
        }
    }
}

void Core0LifecycleDispatcher::processRetirements() {
    // 1. Retry quarantined engines if any were held from a previous timeout
    if (_quarantineCount > 0) {
        for (size_t i = 0; i < _quarantineCount; ) {
            if (_quarantine[i] && _quarantine[i]->shutdownForDestruction()) {
                LOGI("Core0Lifecycle", "Quarantined engine at %p cleanly stopped on retry. Reclaiming...", _quarantine[i].get());
                _quarantine[i]->setResourceState(EngineResourceState::RETIRED);
                _quarantine[i].reset();
                // Compact quarantine array
                for (size_t j = i; j < _quarantineCount - 1; ++j) {
                    _quarantine[j] = std::move(_quarantine[j + 1]);
                }
                _quarantine[_quarantineCount - 1].reset();
                _quarantineCount--;
            } else {
                i++;
            }
        }
    }

    // 2. Drain newly retired engines pushed from Core 1
    std::unique_ptr<IEngine> engine;
    while (_retireQueue.pop(engine)) {
        if (!engine) continue;

        LOGI("Core0Lifecycle", "Core 0 processing cooperative shutdown for retired engine at %p...", engine.get());
        if (engine->shutdownForDestruction()) {
            LOGI("Core0Lifecycle", "Cooperative shutdown succeeded. Destroying engine and releasing Core 0 resources.");
            engine->setResourceState(EngineResourceState::RETIRED);
            engine.reset(); // Safe destruction on Core 0
        } else {
            LOGE("Core0Lifecycle", "CRITICAL: Engine at %p failed cooperative shutdown within timeout! Placing in quarantine (anti-UAF).", engine.get());
            engine->setResourceState(EngineResourceState::QUARANTINED);
            if (_quarantineCount < QUARANTINE_CAPACITY) {
                _quarantine[_quarantineCount++] = std::move(engine);
            } else {
                LOGE("Core0Lifecycle", "CRITICAL: Quarantine capacity reached (%u). Preserving reference to prevent UAF crash.", (unsigned)QUARANTINE_CAPACITY);
                // Intentionally release without deleting to prevent Use-After-Free: safe leak > memory corruption
                engine.release();
            }
        }
    }
}
