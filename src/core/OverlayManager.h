#pragma once

#include <Arduino.h>
#include <memory>
#include "../../include/core/EngineContract.h"
#include "../../include/core/EngineRegistry.h"
#include "ConfigLoader.h"

/**
 * @class OverlayManager
 * @brief Encapsulates decorative overlay passes composited on top of active base engines.
 */
class OverlayManager {
public:
    OverlayManager() = default;
    ~OverlayManager() { deactivate(); }

    void initialize(EngineContext* context, ConfigLoader* config) {
        _context = context;
        _config = config;
    }

    void process(bool allowsOverlay) {
        if (!_context || !_config) return;

        auto fighterInst = _config->getInstance("fighter_main");
        bool enabled = fighterInst && fighterInst->config.getBool("enabled", false);

        if (enabled && allowsOverlay) {
            if (!_activeOverlay) {
                auto desc = EngineRegistry::getDescriptor("fighter");
                if (desc && desc->factory) {
                    _activeOverlay = desc->factory();
                    _activeOverlay->initialize(_context, &fighterInst->config);
                    _activeOverlay->activate();
                }
            }
            if (_activeOverlay) {
                _activeOverlay->update(_context);
                _activeOverlay->render(_context);
            }
        } else if (_activeOverlay) {
            deactivate();
        }
    }

    void deactivate() {
        if (_activeOverlay) {
            _activeOverlay->deactivate();
            _activeOverlay.reset();
        }
    }

    void onConfigChanged() {
        if (_activeOverlay && _config) {
            auto fighterInst = _config->getInstance("fighter_main");
            if (fighterInst) {
                _activeOverlay->onConfigChanged(&fighterInst->config);
            }
        }
    }

    bool hasActiveOverlay() const { return _activeOverlay != nullptr; }

private:
    EngineContext* _context = nullptr;
    ConfigLoader* _config = nullptr;
    std::unique_ptr<IEngine> _activeOverlay;
};
