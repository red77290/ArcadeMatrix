#pragma once
#include <Arduino.h>
#include <Wire.h>

/**
 * @enum GyroType
 * @brief Detected Gyroscope / Accelerometer sensor type.
 */
enum class GyroType {
    NONE = 0,
    MPU6050,
    QMI8658,
    LSM6DS3,
    LIS3DHTR
};

/**
 * @struct GyroOrientation
 * @brief Gyroscope sensor orientation reading and calculated matrix rotation.
 */
struct GyroOrientation {
    bool available = false;
    float ax = 0.0f; // in G (-1.0 to 1.0)
    float ay = 0.0f;
    float az = 0.0f;
    float gx = 0.0f; // in dps (deg/s)
    float gy = 0.0f;
    float gz = 0.0f;
    uint8_t suggestedRotation = 0; // 0, 1, 2, 3 (Adafruit_GFX rotation)
    const char* sensorName = "None";
};

/**
 * @class GyroHAL
 * @brief Hardware Abstraction Layer for I2C Gyroscope & Accelerometer Auto-Rotation.
 */
class GyroHAL {
public:
    GyroHAL();
    ~GyroHAL() = default;

    /**
     * @brief Initializes I2C probe and auto-detects connected gyro/accelerometer.
     * @param wire Pointer to TwoWire bus (default &Wire)
     * @return true if a compatible sensor was found and initialized.
     */
    bool begin(TwoWire* wire = &Wire);

    /**
     * @brief Polls sensor acceleration and returns updated orientation.
     * Includes 500ms hysteresis debounce to avoid rotation flickering.
     */
    GyroOrientation update();

    /**
     * @brief Returns current active orientation snapshot.
     */
    const GyroOrientation& getOrientation() const { return _lastOrientation; }

    /**
     * @brief Indicates if auto-rotation sensor is present and responding.
     */
    bool isAvailable() const { return _type != GyroType::NONE; }

    /**
     * @brief Returns detected sensor type.
     */
    GyroType getType() const { return _type; }

private:
    TwoWire* _wire;
    GyroType _type;
    uint8_t _i2cAddr;
    GyroOrientation _lastOrientation;

    uint8_t _stableRotation;
    uint8_t _candidateRotation;
    uint32_t _candidateStartTime;

    // Probe helper methods
    bool probeMPU6050();
    bool probeQMI8658();
    bool probeLSM6DS3();
    bool probeLIS3DHTR();

    // Raw read helpers
    bool readRawMPU6050(float& ax, float& ay, float& az);
    bool readRawQMI8658(float& ax, float& ay, float& az);
    bool readRawLSM6DS3(float& ax, float& ay, float& az);
    bool readRawLIS3DHTR(float& ax, float& ay, float& az);

    // I2C register helpers
    bool writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg);
    bool readRegs(uint8_t reg, uint8_t* buffer, size_t len);
};

extern GyroHAL gyroHAL;
