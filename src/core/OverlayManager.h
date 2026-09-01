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
class OverlayManager : public IDisplayGeometryAware {
public:
    OverlayManager() = default;
    ~OverlayManager() { deactivate(); }

    void initialize(EngineContext* context, ConfigLoader* config) {
        _context = context;
        _config = config;
    }

    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override {
        if (_fighterOverlay) {
            _fighterOverlay->onDisplayGeometryChanged(geometry);
        }
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

        // Global master switch + per-rotation entry tri-state switch
        ConfigSnapshotGuard guard = _config->acquireSnapshot();
        bool globalEnabled = guard->system.idle_fighter_enabled;
        bool shouldBeActive = globalEnabled && (overlays.fighter != FighterOverride::Disabled);

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

    bool isActive() const { return _fighterActive && _fighterOverlay && _fighterOverlay->isActive(); }
    bool hasInstantiatedFighter() const { return _fighterOverlay != nullptr; }

private:
    EngineContext* _context = nullptr;
    ConfigLoader* _config = nullptr;
    std::unique_ptr<FighterEngine> _fighterOverlay;
    bool _fighterActive = false;
};
