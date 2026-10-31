/**
 * @file WebUiSyncEngine.h
 * @brief Zero-heap priority display notice engaged exclusively on non-PSRAM ESP32 boards (`esp32dev`)
 *        while loading the single-page WebUI bundle, temporarily evicting heavy rotation engines.
 */
#pragma once

#include "../../include/core/EngineContract.h"
#include "../core/I18n.h"
#include "../core/NetworkBudget.h"
#include "../core/drawing/IDrawingSurface.h"
#include <atomic>

class WebUiSyncEngine : public IEngine {
public:
    /// Initial window while the lightweight staging HTML triggers its 250 ms reload on cold SPA visits.
    static constexpr uint32_t kStagingWindowMs = 2000U;
    /// Window covering cold 200 OK compressed SPA transfer + initial DOMContentLoaded bootstrap.
    static constexpr uint32_t kColdLoadWindowMs = 2400U;
    /// Short non-shortening keep-alive tail applied by sequential /api/* bootstrap requests.
    static constexpr uint32_t kBootstrapTailWindowMs = 1200U;
    /// On-demand window when an API handler genuinely lacks internal 8-bit DRAM and defers with 503.
    static constexpr uint32_t kLowHeapDeferralWindowMs = 2500U;
    /// Minimum free internal 8-bit DRAM above which even a cold 200 OK SPA load skips WEBUI_SYNC.
    static constexpr size_t kColdSpaBypassMinFree8 = 32768U;
    /// Minimum contiguous internal 8-bit block above which even a cold 200 OK SPA load skips WEBUI_SYNC.
    static constexpr size_t kColdSpaBypassMinLargest8 = 18432U;

    WebUiSyncEngine() = default;
    ~WebUiSyncEngine() override = default;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void deactivate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;

    bool needsClear() const override { return true; }
    bool allowsOverlay() const override { return false; }
    bool isRealtime() const override { return false; }
    bool hasNewFrame() const override { return m_renderedNewFrame; }
    uint32_t nextFrameDueInMs() const override { return 120; }

    /**
     * @brief Determines whether serving the WebUI document requires engaging or keeping WEBUI_SYNC.
     *
     * Cached conditional refreshes (`304 Not Modified`) send 0 payload bytes and never interrupt
     * DMD playback. Cold `200 OK` transfers only stage `WEBUI_SYNC` on non-PSRAM boards when
     * internal 8-bit DRAM is below the safe bulk-streaming watermark (or while a staging reload
     * is already in progress).
     */
    static constexpr bool needsColdSpaStaging(
        bool hasPsram,
        bool etagMatched,
        bool syncAlreadyActive,
        size_t free8,
        size_t largest8
    ) {
        return (!hasPsram && !etagMatched) &&
               (syncAlreadyActive ||
                (free8 < kColdSpaBypassMinFree8) ||
                (largest8 < kColdSpaBypassMinLargest8));
    }

    /**
     * @brief Monotonically acquires or extends the active `WEBUI_SYNC` window without shortening
     *        a longer window already in flight.
     * @return Duration in ms to submit to `DisplayArbiter`, or `0` if the active deadline already
     *         covers `requestedDurationMs` with margin.
     */
    static uint32_t acquireOrExtendWindowMs(uint32_t nowMs, uint32_t requestedDurationMs) {
        if (requestedDurationMs == 0U) {
            return 0U;
        }
        const bool active = isRequested() || isReadyForServing();
        if (active && s_hasDeadline.load(std::memory_order_acquire)) {
            const int32_t remaining = static_cast<int32_t>(
                s_syncDeadlineMs.load(std::memory_order_acquire) - nowMs
            );
            if (remaining >= static_cast<int32_t>(requestedDurationMs) + 100) {
                markRequested();
                return 0U;
            }
        }
        s_syncDeadlineMs.store(nowMs + requestedDurationMs, std::memory_order_release);
        s_hasDeadline.store(true, std::memory_order_release);
        markRequested();
        return requestedDurationMs;
    }

    static void markRequested() {
        NetworkBudget::setDmdNetActivity(NetworkBudget::DmdNetActivity::WebUiSync);
        s_requested.store(true, std::memory_order_release);
    }
    static bool isRequested() { return s_requested.load(std::memory_order_acquire); }
    static bool isReadyForServing() { return s_readyForServing.load(std::memory_order_acquire); }
    static void resetState() {
        s_hasDeadline.store(false, std::memory_order_release);
        s_syncDeadlineMs.store(0U, std::memory_order_release);
        s_readyForServing.store(false, std::memory_order_release);
        s_requested.store(false, std::memory_order_release);
        NetworkBudget::clearDmdNetActivityIf(NetworkBudget::DmdNetActivity::WebUiSync);
    }

private:
    IDrawingSurface* m_surface = nullptr;
    uint32_t m_lastAnimStepMs = 0;
    uint8_t m_animStep = 0;
    Lang m_lastLang = Lang::FR;
    bool m_dirty = true;
    bool m_renderedNewFrame = false;

    static std::atomic<bool> s_requested;
    static std::atomic<bool> s_readyForServing;
    static std::atomic<bool> s_hasDeadline;
    static std::atomic<uint32_t> s_syncDeadlineMs;
};

