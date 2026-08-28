#include "DisplayOrientationManager.h"
#include "Logger.h"

DisplayOrientationManager displayOrientationManager;

DisplayOrientationManager::DisplayOrientationManager()
    : _display(nullptr), _currentRotation(0), _rotationOffset(0),
      _transitionEffect(RotationEffect::PARTICLE_VORTEX),
      _transitionDurationMs(400), _lastCheckTime(0) {}

void DisplayOrientationManager::begin(Adafruit_GFX* display) {
    _display = display;
    _currentRotation = 0;
    _rotationOffset = 0;
    _lastCheckTime = 0;

    if (_display) {
        _display->setRotation(0);
    }
}

void DisplayOrientationManager::onApexReached(uint8_t targetRot) {
    if (displayOrientationManager._display) {
        displayOrientationManager._display->setRotation(targetRot);
        LOGI("DisplayOrientation", "Apex reached: Applied new matrix rotation: %d (%dx%d)", 
             targetRot, displayOrientationManager._display->width(), displayOrientationManager._display->height());
    }
}

void DisplayOrientationManager::calibrateZeroReference() {
    if (gyroHAL.isAvailable()) {
        GyroOrientation orient = gyroHAL.update();
        if (orient.available) {
            // Compute offset needed so current physical orientation maps to Rotation 0 (Normal)
            _rotationOffset = (4 - (orient.suggestedRotation % 4)) % 4;
            setRotation(0, false);
            LOGI("DisplayOrientation", "Calibrated zero reference! Rotation offset set to %d", _rotationOffset);
        }
    }
}

void DisplayOrientationManager::triggerTestTransition(RotationEffect effect) {
    uint8_t testTo = (_currentRotation + 1) % 4;
    RotationEffect eff = (effect != RotationEffect::NONE) ? effect : _transitionEffect;
    if (eff == RotationEffect::NONE) eff = RotationEffect::PARTICLE_VORTEX;
    _fx.start(_currentRotation, testTo, eff, _transitionDurationMs);
    _currentRotation = testTo;
}

void DisplayOrientationManager::setRotation(uint8_t rotation, bool animated) {
    uint8_t target = rotation % 4;
    if (_currentRotation != target) {
        uint8_t oldRot = _currentRotation;
        _currentRotation = target;
        
        if (animated && _transitionEffect != RotationEffect::NONE && _display) {
            _fx.start(oldRot, _currentRotation, _transitionEffect, _transitionDurationMs);
            LOGI("DisplayOrientation", "Started rotation transition '%s' (%d -> %d)", 
                 RotationTransitionFX::effectToString(_transitionEffect).c_str(), oldRot, _currentRotation);
        } else {
            if (_display) {
                _display->setRotation(_currentRotation);
                LOGI("DisplayOrientation", "Applied instantaneous matrix rotation: %d (%dx%d)", 
                     _currentRotation, _display->width(), _display->height());
            }
        }
    }
}

bool DisplayOrientationManager::renderTransition() {
    if (_fx.isRunning() && _display) {
        return _fx.render(_display, onApexReached);
    }
    return false;
}

bool DisplayOrientationManager::update(bool autoRotate, uint8_t manualRotation) {
    uint32_t now = millis();
    // Poll gyro at maximum 10Hz (every 100ms) to avoid bus overhead
    if (now - _lastCheckTime < 100) {
        return false;
    }
    _lastCheckTime = now;

    uint8_t targetRot = _currentRotation;

    if (autoRotate && gyroHAL.isAvailable()) {
        GyroOrientation orient = gyroHAL.update();
        if (orient.available) {
            // Apply mounting rotation offset
            targetRot = (orient.suggestedRotation + _rotationOffset) % 4;
        }
    } else {
        targetRot = (manualRotation + _rotationOffset) % 4;
    }

    if (targetRot != _currentRotation && !_fx.isRunning()) {
        setRotation(targetRot, true);
        return true;
    }

    return false;
}
