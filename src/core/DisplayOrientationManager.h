#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../hal/GyroHAL.h"

/**
 * @enum DisplayOrientation
 * @brief Abstract representation of screen orientation.
 */
enum class DisplayOrientation : uint8_t {
    ROT_0 = 0,   ///< Normal (0°)
    ROT_90 = 1,  ///< Clockwise (90°)
    ROT_180 = 2, ///< Inverted (180°)
    ROT_270 = 3  ///< Counter-Clockwise (270°)
};

/**
 * @class DisplayOrientationManager
 * @brief Manages global screen rotation, applying Gyro auto-rotation or manual override.
 * Ensures engines remain orientation-agnostic and render into current matrix dimensions.
 */
class DisplayOrientationManager {
public:
    DisplayOrientationManager();
    ~DisplayOrientationManager() = default;

    /**
     * @brief Initializes display orientation manager with matrix display instance.
     */
    void begin(Adafruit_GFX* display);

    /**
     * @brief Polls gyro/config and updates display rotation if orientation changed.
     * @param autoRotate Whether gyroscope auto-rotation is enabled
     * @param manualRotation Forced manual rotation index (0..3)
     * @return true if orientation changed during this update
     */
    bool update(bool autoRotate, uint8_t manualRotation);

    /**
     * @brief Directly forces a specific display rotation.
     */
    void setRotation(uint8_t rotation);

    /**
     * @brief Sets baseline mechanical mounting offset (0..3).
     */
    void setRotationOffset(uint8_t offset) { _rotationOffset = offset % 4; }

    /**
     * @brief Returns active mounting offset (0..3).
     */
    uint8_t getRotationOffset() const { return _rotationOffset; }

    /**
     * @brief Calibrates the current physical position as 0° (Normal orientation).
     */
    void calibrateZeroReference();

    /**
     * @brief Returns current active rotation index (0..3).
     */
    uint8_t getRotation() const { return _currentRotation; }

    /**
     * @brief Returns current abstract display orientation enum.
     */
    DisplayOrientation getOrientation() const { return static_cast<DisplayOrientation>(_currentRotation); }

private:
    Adafruit_GFX* _display;
    uint8_t _currentRotation;
    uint8_t _rotationOffset;
    uint32_t _lastCheckTime;
};

extern DisplayOrientationManager displayOrientationManager;
