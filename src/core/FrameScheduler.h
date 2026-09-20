#pragma once
#include <Arduino.h>

struct FrameRenderResult {
    bool rendered = false;
    bool framebufferChanged = false;
    bool mustPresent = false;
    unsigned long nextDueInMs = ULONG_MAX; ///< when the active engine wants its next frame (ULONG_MAX = no opinion)
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
    void delayUntilNextFrame(bool isRealtime, unsigned long nextDueInMs = ULONG_MAX) {
        unsigned long currentLoopTime = millis();
        unsigned long targetInterval = isRealtime ? CURRENT_REALTIME_INTERVAL : CURRENT_STATIC_INTERVAL;
        unsigned long elapsed = currentLoopTime - lastFrameTime;
        unsigned long wait = (elapsed < targetInterval) ? (targetInterval - elapsed) : 0;
        // A self-pacing engine (GIF) says when its next frame is due. Waking for it instead of
        // sleeping out the whole tick lets 20-60 ms frame delays land on time rather than being
        // rounded up to the next 16 ms boundary.
        if (nextDueInMs < wait) wait = (nextDueInMs < 1) ? 1 : nextDueInMs;
        if (wait > 0) {
            delay(wait);
        }
        lastFrameTime = millis();
    }

private:
    unsigned long lastFrameTime;
};
