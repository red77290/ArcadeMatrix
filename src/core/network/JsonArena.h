/**
 * @file JsonArena.h
 * @brief Fixed-capacity StaticJsonDocument buffer arena with static Core 0 synchronization.
 *
 * Provides a statically pre-allocated StaticJsonDocument arena to eliminate DynamicJsonDocument
 * heap allocations during REST payload deserialization under concurrent mbedTLS operations.
 * Guaranteed zero-heap synchronization via StaticSemaphore_t.
 */
#pragma once
#include <ArduinoJson.h>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

class JsonArena {
public:
    static constexpr size_t ARENA_CAPACITY = 4096;

    static void begin() {
        (void)instance();
    }

    static JsonArena& instance() {
        static JsonArena s_instance;
        return s_instance;
    }

    /**
     * @brief RAII Lock acquiring exclusive access to the Core 0 shared JSON arena.
     * Statically initialized at boot time on Core 0, zero dynamic heap allocations.
     */
    class Lock {
    public:
        explicit Lock(JsonArena& arena) : _arena(arena) {
#if defined(ESP32)
            if (_arena._mutex) {
                xSemaphoreTake(_arena._mutex, portMAX_DELAY);
            }
#endif
            _arena._doc.clear();
        }

        ~Lock() {
            _arena._doc.clear();
#if defined(ESP32)
            if (_arena._mutex) {
                xSemaphoreGive(_arena._mutex);
            }
#endif
        }

        StaticJsonDocument<ARENA_CAPACITY>& doc() { return _arena._doc; }

    private:
        JsonArena& _arena;
    };

private:
    JsonArena() {
#if defined(ESP32)
        _mutex = xSemaphoreCreateMutexStatic(&_mutexBuffer);
#endif
    }

    StaticJsonDocument<ARENA_CAPACITY> _doc;
#if defined(ESP32)
    StaticSemaphore_t _mutexBuffer;
    SemaphoreHandle_t _mutex = nullptr;
#endif
};
