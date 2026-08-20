#pragma once
#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <memory>
#include <mutex>

enum class DisplayPriority {
    ROTATION = 10,
    GIF = 20,
    MARQUEE = 30,
    VISUALIZER = 40,
    MQTT = 100
};

enum class RequestLifecycle {
    ONE_SHOT,         // Triggered once, removed immediately
    TIMED,            // Stays active until timeout expires
    UNTIL_CANCELLED,  // Stays active until explicitly cancelled
    PERSISTENT        // Always active, though can be preempted
};

struct DisplayRequest {
    String source;
    DisplayPriority priority;
    RequestLifecycle lifecycle;
    bool preemptive;
    String instance_id;
    unsigned long timeout_ms; // Used if lifecycle is TIMED
    unsigned long created_at;
};

class DisplayArbiter {
public:
    DisplayArbiter();
    void submitRequest(const DisplayRequest& request);
    void cancelRequest(const String& source);
    void clearExpired();
    
    // Call in the main loop to evaluate the current winner
    DisplayRequest evaluate();
    
private:
    std::vector<DisplayRequest> requests;
    std::mutex arbiterMutex;
};
