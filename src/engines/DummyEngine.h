#pragma once
#include "../../include/core/EngineContract.h"
#include <Arduino.h>

class DummyEngine : public IEngine {
public:
    DummyEngine() {}
    virtual ~DummyEngine() {}

    EngineError initialize(EngineContext* context, const EngineConfig* config) override {
        return EngineError::OK;
    }
    void activate() override {
        Serial.println("DummyEngine activated!");
    }
    void deactivate() override {
        Serial.println("DummyEngine deactivated!");
    }
    void update(EngineContext* context) override {}
    void render(EngineContext* context) override {}
    void onConfigChanged(const EngineConfig* config) override {}
};
