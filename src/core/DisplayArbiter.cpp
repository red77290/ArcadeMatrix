#include "DisplayArbiter.h"

DisplayArbiter::DisplayArbiter() {
    // Add default persistent rotation request as fallback
    DisplayRequest rotReq;
    rotReq.source = "ROTATION";
    rotReq.priority = DisplayPriority::ROTATION;
    rotReq.lifecycle = RequestLifecycle::PERSISTENT;
    rotReq.preemptive = false;
    rotReq.instance_id = "rotation_manager";
    rotReq.timeout_ms = 0;
    rotReq.created_at = millis();
    requests.push_back(rotReq);
}

void DisplayArbiter::submitRequest(const DisplayRequest& request) {
    std::lock_guard<std::mutex> lock(arbiterMutex);
    
    // Check if source already has a request, if so update it
    bool found = false;
    for (auto& req : requests) {
        if (req.source == request.source) {
            req = request;
            req.created_at = millis();
            found = true;
            break;
        }
    }
    
    if (!found) {
        DisplayRequest newReq = request;
        newReq.created_at = millis();
        requests.push_back(newReq);
    }
}

void DisplayArbiter::cancelRequest(const String& source) {
    std::lock_guard<std::mutex> lock(arbiterMutex);
    for (auto it = requests.begin(); it != requests.end(); ) {
        if (it->source == source) {
            it = requests.erase(it);
        } else {
            ++it;
        }
    }
}

void DisplayArbiter::clearExpired() {
    unsigned long now = millis();
    for (auto it = requests.begin(); it != requests.end(); ) {
        if (it->lifecycle == RequestLifecycle::TIMED) {
            if (now - it->created_at >= it->timeout_ms) {
                it = requests.erase(it);
                continue;
            }
        } else if (it->lifecycle == RequestLifecycle::ONE_SHOT) {
            it = requests.erase(it);
            continue;
        }
        ++it;
    }
}

DisplayRequest DisplayArbiter::evaluate() {
    std::lock_guard<std::mutex> lock(arbiterMutex);
    clearExpired();
    
    DisplayRequest winner;
    winner.priority = static_cast<DisplayPriority>(0); // Lowest
    
    for (const auto& req : requests) {
        if (static_cast<int>(req.priority) > static_cast<int>(winner.priority)) {
            winner = req;
        }
    }
    
    return winner;
}
