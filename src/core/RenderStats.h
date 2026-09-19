#pragma once
#include <stdint.h>
#include <atomic>

/**
 * @brief Render-loop counters, exposed by /api/status as per-second rates.
 *
 * Written from the Core 1 render loop, read from the Core 0 web server, hence atomics. They only
 * ever count up; the status handler keeps the previous sample and turns the deltas into rates,
 * so nothing here needs resetting.
 */
struct RenderStats {
    std::atomic<uint32_t> loops{0};             ///< main render-loop iterations
    std::atomic<uint32_t> presents{0};          ///< framebuffer flips
    std::atomic<uint32_t> gifFrames{0};         ///< GIF/PNG frames pushed to the panel
    std::atomic<uint32_t> gifPixelsWritten{0};  ///< panel pixels actually written by those frames
    std::atomic<uint32_t> gifPixelsTotal{0};    ///< panel pixels those frames covered
    std::atomic<uint32_t> gifBlitMicros{0};     ///< time spent pushing pixels
    std::atomic<uint32_t> gifDecodeMicros{0};   ///< time spent decoding
};

extern RenderStats g_renderStats;
