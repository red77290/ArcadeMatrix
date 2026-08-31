#pragma once
#include <Arduino.h>

struct FrameRenderResult {
    bool rendered = false;
    bool framebufferChanged = false;
    bool mustPresent = false;
};

class FrameScheduler {
public:
    static constexpr unsigned long CURRENT_REALTIME_INTERVAL = 16; // ~60 FPS
    static constexpr unsigned long CURRENT_STATIC_INTERVAL = 50;   // ~20 FPS

    FrameScheduler() : lastFrameTime(0) {}

    /**
     * @brief Evaluates whether the DMA buffer must be presented/flipped based on render result.
     */
    inline bool evaluatePresentation(const FrameRenderResult& result) const {
        return result.mustPresent || (result.rendered && result.framebufferChanged);
    }

    /**
     * @brief Paces the loop strictly replicating current firmware cadence.
     */
    void delayUntilNextFrame(bool isRealtime) {
        unsigned long currentLoopTime = millis();
        unsigned long targetInterval = isRealtime ? CURRENT_REALTIME_INTERVAL : CURRENT_STATIC_INTERVAL;
        unsigned long elapsed = currentLoopTime - lastFrameTime;
        if (elapsed < targetInterval) {
            delay(targetInterval - elapsed);
        }
        lastFrameTime = millis();
    }

private:
    unsigned long lastFrameTime;
};
