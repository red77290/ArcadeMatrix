#pragma once
#include <Arduino.h>
#include <array>
#include <mutex>
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

struct EngineHandle {
    uint16_t descriptorId = 0;
    uint16_t instanceId = 0;

    constexpr EngineHandle() = default;
    constexpr EngineHandle(uint16_t descId, uint16_t instId) : descriptorId(descId), instanceId(instId) {}

    constexpr bool operator==(const EngineHandle& o) const {
        return descriptorId == o.descriptorId && instanceId == o.instanceId;
    }
    constexpr bool operator!=(const EngineHandle& o) const { return !(*this == o); }
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

struct DisplayDecision {
    EngineHandle engineHandle{};
    DisplaySourceId sourceId = DisplaySourceId::ROTATION;
    DisplayPriority priority = DisplayPriority::ROTATION;
    uint32_t requestId = 0;
    RequestLifecycle lifecycle = RequestLifecycle::PERSISTENT;
    bool allowsOverlay = true;
    bool needsClear = true;
    bool isRealtime = false;
    bool valid = false;
};

class DisplayArbiter {
public:
    static constexpr size_t MAX_REQUESTS = 8;

    DisplayArbiter();
    void submitRequest(const DisplayRequest& request, bool restartTimer = false);
    void cancelRequest(DisplaySourceId sourceId);
    void clearExpired();
    
    // Evaluates current winner decision (Pure value-type decision, zero allocations, zero engine pointers)
    DisplayDecision evaluate();

    static DisplaySourceId parseSourceId(const String& name);
    
private:
    mutable std::mutex arbiterMutex;
    std::array<DisplayRequestSlot, MAX_REQUESTS> slots{};
    uint32_t _nextRequestId = 1;
};
