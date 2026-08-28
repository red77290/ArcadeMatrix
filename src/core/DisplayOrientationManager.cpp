#include "DisplayOrientationManager.h"
#include "Logger.h"

DisplayOrientationManager displayOrientationManager;

DisplayOrientationManager::DisplayOrientationManager()
    : _display(nullptr), _currentRotation(0), _lastCheckTime(0) {}

void DisplayOrientationManager::begin(Adafruit_GFX* display) {
    _display = display;
    _currentRotation = 0;
    _lastCheckTime = 0;

    if (_display) {
        _display->setRotation(0);
    }
}

void DisplayOrientationManager::setRotation(uint8_t rotation) {
    uint8_t target = rotation % 4;
    if (_currentRotation != target) {
        _currentRotation = target;
        if (_display) {
            _display->setRotation(_currentRotation);
            LOGI("DisplayOrientation", "Applied new matrix rotation: %d (%dx%d)", 
                 _currentRotation, _display->width(), _display->height());
        }
    }
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
            targetRot = orient.suggestedRotation;
        }
    } else {
        targetRot = manualRotation % 4;
    }

    if (targetRot != _currentRotation) {
        setRotation(targetRot);
        return true;
    }

    return false;
}
