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
    MQTT = 40,
    VISUALIZER = 50,
    ALERT = 100
};

enum class RequestLifecycle {
    ONE_SHOT,         // Triggered once, removed immediately
    TIMED,            // Stays active until timeout expires
    UNTIL_CANCELLED,  // Stays active until explicitly cancelled
    PERSISTENT        // Always active, though can be preempted
};

class IEngine;

struct DisplayRequest {
    String source;
    DisplayPriority priority;
    RequestLifecycle lifecycle;
    bool preemptive;
    String instance_id;
    unsigned long timeout_ms; // Used if lifecycle is TIMED
    unsigned long created_at;
    IEngine* engine; // Polymorphic engine pointer for direct execution

    DisplayRequest()
        : source(""), priority(DisplayPriority::ROTATION), lifecycle(RequestLifecycle::PERSISTENT),
          preemptive(false), instance_id(""), timeout_ms(0), created_at(0), engine(nullptr) {}

    DisplayRequest(String src, DisplayPriority prio, RequestLifecycle life, bool pre = true,
                   String inst = "", unsigned long timeout = 0, unsigned long created = 0, IEngine* eng = nullptr)
        : source(src), priority(prio), lifecycle(life), preemptive(pre),
          instance_id(inst), timeout_ms(timeout), created_at(created), engine(eng) {}
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
