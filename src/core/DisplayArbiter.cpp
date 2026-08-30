#include "DisplayArbiter.h"

DisplaySourceId DisplayArbiter::parseSourceId(const String& name) {
    if (name.equalsIgnoreCase("VISUALIZER") || name.equalsIgnoreCase("AUDIOVISUALIZER")) {
        return DisplaySourceId::VISUALIZER;
    }
    if (name.equalsIgnoreCase("MESSAGE") || name.equalsIgnoreCase("MQTT")) {
        return DisplaySourceId::MQTT;
    }
    if (name.equalsIgnoreCase("MARQUEE")) {
        return DisplaySourceId::MARQUEE;
    }
    if (name.equalsIgnoreCase("GIF") || name.equalsIgnoreCase("GIFS")) {
        return DisplaySourceId::GIF;
    }
    if (name.equalsIgnoreCase("ALERT")) {
        return DisplaySourceId::ALERT;
    }
    return DisplaySourceId::ROTATION;
}

DisplayArbiter::DisplayArbiter() {
    for (size_t i = 0; i < MAX_REQUESTS; ++i) {
        requests[i].active = false;
    }
    
    // Add default persistent rotation request as fallback in slot 0
    DisplayRequest rotReq;
    rotReq.sourceId = DisplaySourceId::ROTATION;
    rotReq.priority = DisplayPriority::ROTATION;
    rotReq.lifecycle = RequestLifecycle::PERSISTENT;
    rotReq.preemptive = false;
    rotReq.timeout_ms = 0;
    rotReq.created_at = millis();
    rotReq.active = true;
    requests[0] = rotReq;
}

void DisplayArbiter::submitRequest(const DisplayRequest& request, bool restartTimer) {
    std::lock_guard<std::mutex> lock(arbiterMutex);
    
    // Check if source already has an active slot, if so update it
    int foundIndex = -1;
    int firstFreeIndex = -1;
    
    for (size_t i = 0; i < MAX_REQUESTS; ++i) {
        if (requests[i].active && requests[i].sourceId == request.sourceId) {
            foundIndex = (int)i;
            break;
        }
        if (!requests[i].active && firstFreeIndex == -1 && i > 0) {
            firstFreeIndex = (int)i;
        }
    }
    
    if (foundIndex != -1) {
        auto prevCreated = requests[foundIndex].created_at;
        requests[foundIndex] = request;
        requests[foundIndex].active = true;
        if (!restartTimer) {
            requests[foundIndex].created_at = prevCreated;
        } else {
            requests[foundIndex].created_at = millis();
        }
    } else if (firstFreeIndex != -1) {
        requests[firstFreeIndex] = request;
        requests[firstFreeIndex].active = true;
        requests[firstFreeIndex].created_at = millis();
    }
}

void DisplayArbiter::cancelRequest(DisplaySourceId sourceId) {
    if (sourceId == DisplaySourceId::ROTATION) {
        return; // Fallback rotation request cannot be cancelled
    }
    std::lock_guard<std::mutex> lock(arbiterMutex);
    for (size_t i = 1; i < MAX_REQUESTS; ++i) {
        if (requests[i].active && requests[i].sourceId == sourceId) {
            requests[i].active = false;
        }
    }
}

void DisplayArbiter::cancelRequest(const String& sourceName) {
    cancelRequest(parseSourceId(sourceName));
}

void DisplayArbiter::clearExpired() {
    unsigned long now = millis();
    for (size_t i = 1; i < MAX_REQUESTS; ++i) {
        if (requests[i].active && requests[i].lifecycle == RequestLifecycle::TIMED) {
            if (requests[i].timeout_ms > 0 && (now - requests[i].created_at >= requests[i].timeout_ms)) {
                requests[i].active = false;
            }
        }
    }
}

DisplayDecision DisplayArbiter::evaluate() {
    std::lock_guard<std::mutex> lock(arbiterMutex);
    clearExpired();
    
    DisplayDecision decision;
    decision.priority = static_cast<DisplayPriority>(0);
    decision.valid = false;
    
    int bestSlot = -1;
    for (size_t i = 0; i < MAX_REQUESTS; ++i) {
        if (requests[i].active) {
            if (static_cast<uint8_t>(requests[i].priority) >= static_cast<uint8_t>(decision.priority)) {
                decision.priority = requests[i].priority;
                decision.sourceId = requests[i].sourceId;
                decision.engineHandle = requests[i].engineHandle;
                decision.engine = requests[i].engine;
                decision.requestId = requests[i].requestId;
                decision.lifecycle = requests[i].lifecycle;
                decision.allowsOverlay = requests[i].allowsOverlay;
                decision.needsClear = requests[i].needsClear;
                decision.isRealtime = requests[i].isRealtime;
                decision.valid = true;
                bestSlot = (int)i;
            }
        }
    }
    
    // Auto-consume ONE_SHOT requests
    if (bestSlot >= 1 && decision.lifecycle == RequestLifecycle::ONE_SHOT) {
        requests[bestSlot].active = false;
    }
    
    return decision;
}
