#pragma once

#include <Arduino.h>
#include <memory>
#include "../../include/core/EngineContract.h"
#include "ConfigLoader.h"
#include "../engines/FighterEngine.h"
#include "Logger.h"

/**
 * @class OverlayManager
 * @brief Transverse composition layer applying decorative overlays additively onto base framebuffers.
 *
 * CONTRACT:
 * - Overlay rendering MUST be additive onto existing framebuffers.
 * - Overlay rendering MUST NOT call matrix->clear() or replace the base display.
 * - FighterEngine is lazily instantiated on first demand and preserved in heap to prevent fragmentation.
 */
class OverlayManager {
public:
    OverlayManager() = default;
    ~OverlayManager() { deactivate(); }

    void initialize(EngineContext* context, ConfigLoader* config) {
        _context = context;
        _config = config;
    }

    /**
     * @brief Configures which overlays should be active for the current display pass.
     * @param overlays Requested overlay switches from the current rotation slot (or empty when preempted).
     */
    void configure(const OverlayConfig& overlays) {
        if (!_config) {
            _fighterActive = false;
            return;
        }

        // Global master switch + per-rotation entry switch
        bool globalEnabled = _config->system.idle_fighter_enabled;
        bool requested = overlays.fighter;
        bool shouldBeActive = globalEnabled && requested;

        if (shouldBeActive) {
            if (!_fighterOverlay) {
                LOGI("OverlayManager", "Lazy-instantiating FighterEngine overlay...");
                _fighterOverlay = std::unique_ptr<FighterEngine>(new FighterEngine());
                if (_context) {
                    _fighterOverlay->initialize(_context, nullptr);
                }
            }
            if (!_fighterActive && _fighterOverlay) {
                _fighterOverlay->activate();
                _fighterActive = true;
            }
        } else {
            if (_fighterActive && _fighterOverlay) {
                _fighterOverlay->deactivate();
                _fighterActive = false;
            }
        }
    }

    void update() {
        if (_fighterActive && _fighterOverlay && _context) {
            _fighterOverlay->update(_context);
        }
    }

    void render() {
        if (_fighterActive && _fighterOverlay && _context) {
            _fighterOverlay->render(_context);
        }
    }

    void deactivate() {
        if (_fighterActive && _fighterOverlay) {
            _fighterOverlay->deactivate();
            _fighterActive = false;
        }
    }

    void onConfigChanged() {
        if (_fighterOverlay) {
            _fighterOverlay->onConfigChanged(nullptr);
        }
    }

    bool isActive() const { return _fighterActive; }
    bool hasInstantiatedFighter() const { return _fighterOverlay != nullptr; }

private:
    EngineContext* _context = nullptr;
    ConfigLoader* _config = nullptr;
    std::unique_ptr<FighterEngine> _fighterOverlay;
    bool _fighterActive = false;
};
