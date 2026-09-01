#pragma once
#include <Arduino.h>
#include <array>
#include <atomic>
#include <mutex>
#include <cstring>
#include "../../include/core/EngineContract.h"

enum class DisplaySourceId : uint16_t {
    ROTATION = 10,
    GIF = 20,
    MARQUEE = 30,
    MQTT = 40,
    VISUALIZER = 50,
    ALERT = 100
};

enum class DisplayPriority : uint8_t {
    ROTATION = 10,
    GIF = 20,
    MARQUEE = 30,
    MQTT = 40,
    VISUALIZER = 50,
    ALERT = 100
};

enum class RequestLifecycle : uint8_t {
    ONE_SHOT,         // Triggered once, marked inactive immediately after evaluation
    TIMED,            // Stays active until timeout expires
    UNTIL_CANCELLED,  // Stays active until explicitly cancelled
    PERSISTENT        // Always active fallback (ROTATION)
};

/**
 * @brief Canonical identity struct for an engine / instance (zero heap allocations, POD).
 */
struct EngineHandle {
    char descriptorId[32]{0};
    char instanceId[32]{0};

    EngineHandle() = default;
    EngineHandle(const char* descId, const char* instId = "") {
        if (descId) strncpy(descriptorId, descId, sizeof(descriptorId) - 1);
        if (instId) strncpy(instanceId, instId, sizeof(instanceId) - 1);
    }
    EngineHandle(const String& descId, const String& instId = "") {
        strncpy(descriptorId, descId.c_str(), sizeof(descriptorId) - 1);
        strncpy(instanceId, instId.c_str(), sizeof(instanceId) - 1);
    }

    bool operator==(const EngineHandle& o) const {
        return strncmp(descriptorId, o.descriptorId, sizeof(descriptorId)) == 0 &&
               strncmp(instanceId, o.instanceId, sizeof(instanceId)) == 0;
    }
    bool operator!=(const EngineHandle& o) const { return !(*this == o); }
    bool isEmpty() const { return descriptorId[0] == '\0' && instanceId[0] == '\0'; }
};

struct DisplayRequest {
    DisplaySourceId sourceId = DisplaySourceId::ROTATION;
    DisplayPriority priority = DisplayPriority::ROTATION;
    RequestLifecycle lifecycle = RequestLifecycle::PERSISTENT;
    bool preemptive = false;
    uint32_t requestId = 0;
    EngineHandle engineHandle{};
    unsigned long timeout_ms = 0;
    unsigned long created_at = 0;
    bool allowsOverlay = true;
    bool needsClear = true;
    bool isRealtime = false;

    DisplayRequest() = default;
    DisplayRequest(DisplaySourceId src, DisplayPriority prio, RequestLifecycle life, bool pre = true,
                   uint32_t reqId = 0, EngineHandle handle = {},
                   unsigned long timeout = 0, unsigned long created = 0,
                   bool allowOv = true, bool clear = true, bool realtime = false)
        : sourceId(src), priority(prio), lifecycle(life), preemptive(pre),
          requestId(reqId), engineHandle(handle),
          timeout_ms(timeout), created_at(created),
          allowsOverlay(allowOv), needsClear(clear), isRealtime(realtime) {}
};

struct DisplayRequestSlot {
    bool active = false;
    DisplayRequest request{};
};

enum class TransitionMode : uint8_t {
    NONE = 0,
    REPLACE,
    PREEMPT,
    RESUME
};

struct DisplayDecision {
    EngineHandle engineHandle{};
    DisplaySourceId sourceId = DisplaySourceId::ROTATION;
    DisplayPriority priority = DisplayPriority::ROTATION;
    uint32_t requestId = 0;
    RequestLifecycle lifecycle = RequestLifecycle::PERSISTENT;
    bool preemptive = false;
    bool allowsOverlay = true;
    bool needsClear = true;
    bool isRealtime = false;
    bool valid = false;
};

enum class ArbiterCommandType : uint8_t {
    SUBMIT,
    CANCEL
};

struct ArbiterCommand {
    ArbiterCommandType type = ArbiterCommandType::SUBMIT;
    DisplayRequest request{};
    bool restartTimer = false;
};

template <typename T, size_t Capacity>
class LockFreeSPSCQueue {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    bool push(const T& item) {
        size_t head = _head.load(std::memory_order_relaxed);
        size_t tail = _tail.load(std::memory_order_acquire);
        if ((head - tail) >= Capacity) {
            return false; // Full
        }
        _buffer[head & (Capacity - 1)] = item;
        _head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t tail = _tail.load(std::memory_order_relaxed);
        size_t head = _head.load(std::memory_order_acquire);
        if (tail == head) {
            return false; // Empty
        }
        item = _buffer[tail & (Capacity - 1)];
        _tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return _head.load(std::memory_order_relaxed) == _tail.load(std::memory_order_relaxed);
    }

private:
    std::array<T, Capacity> _buffer{};
    std::atomic<size_t> _head{0};
    std::atomic<size_t> _tail{0};
};

class DisplayArbiter {
public:
    static constexpr size_t MAX_REQUESTS = 8;
    static constexpr size_t QUEUE_CAPACITY = 16;

    DisplayArbiter();

    // Core 0 producers: serialized through _producerMutex, pushes non-blocking into SPSC queue
    void submitRequest(const DisplayRequest& request, bool restartTimer = false);
    void cancelRequest(DisplaySourceId sourceId);
    void clearExpired();
    
    // Core 1 consumer: drains command queue and evaluates highest priority decision with ZERO mutex and ZERO allocations
    DisplayDecision evaluate();

    static DisplaySourceId parseSourceId(const String& name);
    
private:
    mutable std::mutex _producerMutex;
    LockFreeSPSCQueue<ArbiterCommand, QUEUE_CAPACITY> _commandQueue;
    std::array<DisplayRequestSlot, MAX_REQUESTS> slots{};
    uint32_t _nextRequestId = 1;

    void applySubmit(const DisplayRequest& request, bool restartTimer);
    void applyCancel(DisplaySourceId sourceId);
};

