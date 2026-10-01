/**
 * @file JsonArena.h
 * @brief Zero-heap REST streaming JSON buffer arena for Core 0 network operations.
 *
 * Provides a statically pre-allocated StaticJsonDocument arena to eliminate DynamicJsonDocument
 * heap allocations during REST payload deserialization under concurrent mbedTLS operations.
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

    static JsonArena& instance() {
        static JsonArena s_instance;
        return s_instance;
    }

    /**
     * @brief RAII Lock acquiring exclusive access to the Core 0 shared JSON arena.
     * Guaranteed to be initialized at boot time on Core 0, zero allocations on render path.
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
        _mutex = xSemaphoreCreateMutex();
#endif
    }

    StaticJsonDocument<ARENA_CAPACITY> _doc;
#if defined(ESP32)
    SemaphoreHandle_t _mutex = nullptr;
#endif
};
