#pragma once
#include "core/EngineContract.h"

// Includes for legacy engines
#include "ClockEngine.h"
#include "DateEngine.h"
#include "WeatherEngine.h"
#include "FighterEngine.h"
#include "GifEngine.h"
#include "CryptoEngine.h"
#include "StockEngine.h"
#include "TempEngine.h"
#include "VisualizerEngine.h"
#include "DecibelEngine.h"
#include "MessageEngine.h"
#include "MarqueeEngine.h"

/**
 * @brief Generic wrapper for legacy engines that don't implement IEngine yet.
 * In a real scenario, you'd specialize this or use lambdas to map the specific loop()/render() calls.
 */
template <typename T>
class LegacyEngineWrapper : public IEngine {
public:
    LegacyEngineWrapper() : instance(nullptr) {}
    ~LegacyEngineWrapper() override {
        if (instance) delete instance;
    }

    EngineError initialize(EngineContext* context, const EngineConfig* config) override {
        // Most legacy engines take a MatrixPanel_I2S_DMA*
        instance = new T(context->getMatrix());
        return EngineError::OK;
    }

    void activate() override {
        // Implement default or nothing if legacy doesn't have it
    }

    void update(EngineContext* context) override {
        // If the legacy engine has a loop() method, we would call it here.
        // We will specialize or add logic as needed.
    }

    void render(EngineContext* context) override {
        // Default render mapping
    }

    void deactivate() override {
    }

    T* getLegacyInstance() { return instance; }

private:
    T* instance;
};

