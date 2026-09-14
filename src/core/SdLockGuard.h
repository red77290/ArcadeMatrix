#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t sdMutex;

/**
 * @class SdLockGuard
 * @brief RAII scoped lock guard for sdMutex ensuring zero lock leaks and deterministic release.
 */
class SdLockGuard {
public:
    explicit SdLockGuard(TickType_t timeout = pdMS_TO_TICKS(2000))
        : _locked(false) {
        if (sdMutex && xSemaphoreTake(sdMutex, timeout) == pdTRUE) {
            _locked = true;
        }
    }

    ~SdLockGuard() {
        unlock();
    }

    bool isLocked() const { return _locked; }
    explicit operator bool() const { return _locked; }

    void unlock() {
        if (_locked && sdMutex) {
            xSemaphoreGive(sdMutex);
            _locked = false;
        }
    }

    SdLockGuard(const SdLockGuard&) = delete;
    SdLockGuard& operator=(const SdLockGuard&) = delete;

private:
    bool _locked;
};
