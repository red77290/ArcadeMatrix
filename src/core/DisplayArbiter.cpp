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
        slots[i].active = false;
        slots[i].request = DisplayRequest{};
    }
    
    // Add default persistent rotation request as fallback in slot 0
    DisplayRequest rotReq;
    rotReq.sourceId = DisplaySourceId::ROTATION;
    rotReq.priority = DisplayPriority::ROTATION;
    rotReq.lifecycle = RequestLifecycle::PERSISTENT;
    rotReq.preemptive = false;
    rotReq.requestId = _nextRequestId++;
    rotReq.timeout_ms = 0;
    rotReq.created_at = millis();
    slots[0].request = rotReq;
    slots[0].active = true;
}

void DisplayArbiter::submitRequest(const DisplayRequest& request, bool restartTimer) {
    std::lock_guard<std::mutex> lock(_producerMutex);
    ArbiterCommand cmd;
    cmd.type = ArbiterCommandType::SUBMIT;
    cmd.request = request;
    cmd.restartTimer = restartTimer;
    _commandQueue.push(cmd);
}

void DisplayArbiter::cancelRequest(DisplaySourceId sourceId) {
    if (sourceId == DisplaySourceId::ROTATION) {
        return; // Fallback rotation request cannot be cancelled
    }
    std::lock_guard<std::mutex> lock(_producerMutex);
    ArbiterCommand cmd;
    cmd.type = ArbiterCommandType::CANCEL;
    cmd.request.sourceId = sourceId;
    _commandQueue.push(cmd);
}

void DisplayArbiter::applySubmit(const DisplayRequest& request, bool restartTimer) {
    int foundIndex = -1;
    int firstFreeIndex = -1;
    
    for (size_t i = 0; i < MAX_REQUESTS; ++i) {
        if (slots[i].active && slots[i].request.sourceId == request.sourceId) {
            foundIndex = (int)i;
            break;
        }
        if (!slots[i].active && firstFreeIndex == -1 && i > 0) {
            firstFreeIndex = (int)i;
        }
    }
    
    if (foundIndex != -1) {
        unsigned long prevCreated = slots[foundIndex].request.created_at;
        uint32_t prevReqId = slots[foundIndex].request.requestId;
        slots[foundIndex].request = request;
        slots[foundIndex].active = true;
        
        // Retain existing requestId unless caller explicitly passed a new one or restartTimer is requested
        if (request.requestId != 0) {
            slots[foundIndex].request.requestId = request.requestId;
        } else if (restartTimer) {
            slots[foundIndex].request.requestId = _nextRequestId++;
        } else {
            slots[foundIndex].request.requestId = prevReqId;
        }
        
        if (!restartTimer) {
            slots[foundIndex].request.created_at = prevCreated;
        } else {
            slots[foundIndex].request.created_at = millis();
        }
    } else if (firstFreeIndex != -1) {
        slots[firstFreeIndex].request = request;
        slots[firstFreeIndex].active = true;
        if (slots[firstFreeIndex].request.requestId == 0) {
            slots[firstFreeIndex].request.requestId = _nextRequestId++;
        }
        slots[firstFreeIndex].request.created_at = millis();
    }
}

void DisplayArbiter::applyCancel(DisplaySourceId sourceId) {
    for (size_t i = 1; i < MAX_REQUESTS; ++i) {
        if (slots[i].active && slots[i].request.sourceId == sourceId) {
            slots[i].active = false;
        }
    }
}

void DisplayArbiter::clearExpired() {
    unsigned long now = millis();
    for (size_t i = 1; i < MAX_REQUESTS; ++i) {
        if (slots[i].active && slots[i].request.lifecycle == RequestLifecycle::TIMED) {
            if (slots[i].request.timeout_ms > 0 &&
                (now - slots[i].request.created_at >= slots[i].request.timeout_ms)) {
                slots[i].active = false;
            }
        }
    }
}

DisplayDecision DisplayArbiter::evaluate() {
    // 1. Drain pending cross-core commands from SPSC queue into Core 1 local slots (100% lock-free)
    ArbiterCommand cmd;
    while (_commandQueue.pop(cmd)) {
        if (cmd.type == ArbiterCommandType::SUBMIT) {
            applySubmit(cmd.request, cmd.restartTimer);
        } else if (cmd.type == ArbiterCommandType::CANCEL) {
            applyCancel(cmd.request.sourceId);
        }
    }

    // 2. Clear expired timed requests
    clearExpired();

    // 3. Find active request with highest priority
    int bestSlot = 0; // Default to ROTATION fallback
    for (size_t i = 1; i < MAX_REQUESTS; ++i) {
        if (slots[i].active) {
            if (static_cast<uint8_t>(slots[i].request.priority) >
                static_cast<uint8_t>(slots[bestSlot].request.priority)) {
                bestSlot = (int)i;
            }
        }
    }

    DisplayDecision decision;
    decision.sourceId = slots[bestSlot].request.sourceId;
    decision.priority = slots[bestSlot].request.priority;
    decision.requestId = slots[bestSlot].request.requestId;
    decision.engineHandle = slots[bestSlot].request.engineHandle;
    decision.lifecycle = slots[bestSlot].request.lifecycle;
    decision.allowsOverlay = slots[bestSlot].request.allowsOverlay;
    decision.needsClear = slots[bestSlot].request.needsClear;
    decision.isRealtime = slots[bestSlot].request.isRealtime;
    decision.valid = true;

    // 4. Compute deterministic TransitionMode
    if (decision.priority > _lastPriority && decision.sourceId != DisplaySourceId::ROTATION) {
        decision.transitionMode = TransitionMode::PREEMPT;
    } else if (decision.priority < _lastPriority) {
        decision.transitionMode = TransitionMode::RESUME;
    } else {
        decision.transitionMode = TransitionMode::REPLACE;
    }

    _lastPriority = decision.priority;
    _lastSourceId = decision.sourceId;

    // 5. Auto-consume ONE_SHOT requests
    if (bestSlot > 0 && decision.lifecycle == RequestLifecycle::ONE_SHOT) {
        slots[bestSlot].active = false;
    }

    return decision;
}
