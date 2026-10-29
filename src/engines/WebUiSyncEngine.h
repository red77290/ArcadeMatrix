/**
 * @file WebUiSyncEngine.h
 * @brief Zero-heap priority display notice engaged exclusively on non-PSRAM ESP32 boards (`esp32dev`)
 *        while loading the single-page WebUI bundle, temporarily evicting heavy rotation engines.
 */
#pragma once

#include "../../include/core/EngineContract.h"
#include "../core/I18n.h"
#include "../core/drawing/IDrawingSurface.h"
#include <atomic>

class WebUiSyncEngine : public IEngine {
public:
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

    static void markRequested() { s_requested.store(true, std::memory_order_release); }
    static bool isRequested() { return s_requested.load(std::memory_order_acquire); }
    static bool isReadyForServing() { return s_readyForServing.load(std::memory_order_acquire); }
    static void resetState() {
        s_readyForServing.store(false, std::memory_order_release);
        s_requested.store(false, std::memory_order_release);
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
};

