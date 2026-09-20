#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t sdMutex;
extern TaskHandle_t s_sdOwnerTask;
extern uint32_t s_sdRecursionCount;

/**
 * @class SdLockGuard
 * @brief Reentrant RAII scoped lock guard for sdMutex ensuring zero lock leaks, deterministic release,
 *        and safe reentrancy for nested SD calls within the same FreeRTOS task.
 */
class SdLockGuard {
public:
    explicit SdLockGuard(TickType_t timeout = pdMS_TO_TICKS(2000))
        : _locked(false), _isReentrant(false) {
        if (!sdMutex) return;
        TaskHandle_t current = xTaskGetCurrentTaskHandle();
        if (s_sdOwnerTask != nullptr && s_sdOwnerTask == current) {
            s_sdRecursionCount++;
            _locked = true;
            _isReentrant = true;
            return;
        }
        if (xSemaphoreTake(sdMutex, timeout) == pdTRUE) {
            s_sdOwnerTask = current;
            s_sdRecursionCount = 1;
            _locked = true;
            _isReentrant = false;
        }
    }

    ~SdLockGuard() {
        unlock();
    }

    bool isLocked() const { return _locked; }
    explicit operator bool() const { return _locked; }

    void unlock() {
        if (_locked) {
            if (_isReentrant) {
                if (s_sdRecursionCount > 0) s_sdRecursionCount--;
                _locked = false;
                _isReentrant = false;
            } else if (sdMutex) {
                s_sdOwnerTask = nullptr;
                s_sdRecursionCount = 0;
                xSemaphoreGive(sdMutex);
                _locked = false;
            }
        }
    }

    SdLockGuard(const SdLockGuard&) = delete;
    SdLockGuard& operator=(const SdLockGuard&) = delete;

private:
    bool _locked;
    bool _isReentrant;
};
