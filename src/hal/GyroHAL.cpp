#include "GyroHAL.h"
#include "../core/Logger.h"

GyroHAL gyroHAL;

GyroHAL::GyroHAL() 
    : _wire(&Wire), _type(GyroType::NONE), _i2cAddr(0),
      _stableRotation(0), _candidateRotation(0), _candidateStartTime(0) {
    _lastOrientation.available = false;
    _lastOrientation.suggestedRotation = 0;
}

bool GyroHAL::begin(TwoWire* wire) {
    if (wire) _wire = wire;
    _type = GyroType::NONE;
    _i2cAddr = 0;

    // 1. Probe MPU6050 / MPU6500 (0x68 / 0x69)
    if (probeMPU6050()) {
        _type = GyroType::MPU6050;
        _lastOrientation.sensorName = "MPU6050/6500";
        LOGI("GyroHAL", "Detected MPU6050/MPU6500 Accelerometer at 0x%02X", _i2cAddr);
        return true;
    }

    // 2. Probe QMI8658 (0x6B / 0x6A)
    if (probeQMI8658()) {
        _type = GyroType::QMI8658;
        _lastOrientation.sensorName = "QMI8658";
        LOGI("GyroHAL", "Detected QMI8658 Accelerometer at 0x%02X", _i2cAddr);
        return true;
    }

    // 3. Probe LSM6DS3 (0x6A / 0x6B)
    if (probeLSM6DS3()) {
        _type = GyroType::LSM6DS3;
        _lastOrientation.sensorName = "LSM6DS3";
        LOGI("GyroHAL", "Detected LSM6DS3 Accelerometer at 0x%02X", _i2cAddr);
        return true;
    }

    // 4. Probe LIS3DHTR (0x18 / 0x19)
    if (probeLIS3DHTR()) {
        _type = GyroType::LIS3DHTR;
        _lastOrientation.sensorName = "LIS3DHTR";
        LOGI("GyroHAL", "Detected LIS3DHTR Accelerometer at 0x%02X", _i2cAddr);
        return true;
    }

    LOGD("GyroHAL", "No I2C Gyroscope / Accelerometer detected (Manual rotation mode active).");
    return false;
}

bool GyroHAL::writeReg(uint8_t reg, uint8_t val) {
    if (!_wire || _i2cAddr == 0) return false;
    _wire->beginTransmission(_i2cAddr);
    _wire->write(reg);
    _wire->write(val);
    return (_wire->endTransmission() == 0);
}

uint8_t GyroHAL::readReg(uint8_t reg) {
    uint8_t val = 0;
    readRegs(reg, &val, 1);
    return val;
}

bool GyroHAL::readRegs(uint8_t reg, uint8_t* buffer, size_t len) {
    if (!_wire || _i2cAddr == 0 || !buffer || len == 0) return false;
    _wire->beginTransmission(_i2cAddr);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) return false;

    size_t count = _wire->requestFrom(_i2cAddr, (uint8_t)len);
    if (count < len) return false;

    for (size_t i = 0; i < len; i++) {
        buffer[i] = _wire->read();
    }
    return true;
}

bool GyroHAL::probeMPU6050() {
    uint8_t addrs[2] = {0x68, 0x69};
    for (uint8_t addr : addrs) {
        _i2cAddr = addr;
        uint8_t who = readReg(0x75); // WHO_AM_I
        if (who == 0x68 || who == 0x70 || who == 0x72 || who == 0x71 || who == 0x98) {
            // Wake up MPU6050 (PWR_MGMT_1 = 0x00)
            writeReg(0x6B, 0x00);
            delay(10);
            // Set accel config to 2G (ACCEL_CONFIG = 0x00)
            writeReg(0x1C, 0x00);
            return true;
        }
    }
    _i2cAddr = 0;
    return false;
}

bool GyroHAL::probeQMI8658() {
    uint8_t addrs[2] = {0x6B, 0x6A};
    for (uint8_t addr : addrs) {
        _i2cAddr = addr;
        uint8_t who = readReg(0x00); // WHO_AM_I
        if (who == 0x05) {
            // Enable Accelerometer (CTRL7 = 0x03)
            writeReg(0x08, 0x03);
            // CTRL1: 2G range, 100Hz ODR
            writeReg(0x02, 0x00);
            return true;
        }
    }
    _i2cAddr = 0;
    return false;
}

bool GyroHAL::probeLSM6DS3() {
    uint8_t addrs[2] = {0x6A, 0x6B};
    for (uint8_t addr : addrs) {
        _i2cAddr = addr;
        uint8_t who = readReg(0x0F); // WHO_AM_I
        if (who == 0x69 || who == 0x6A || who == 0x6C) {
            // CTRL1_XL: 104Hz, 2G scale (0x40)
            writeReg(0x10, 0x40);
            return true;
        }
    }
    _i2cAddr = 0;
    return false;
}

bool GyroHAL::probeLIS3DHTR() {
    uint8_t addrs[2] = {0x18, 0x19};
    for (uint8_t addr : addrs) {
        _i2cAddr = addr;
        uint8_t who = readReg(0x0F); // WHO_AM_I
        if (who == 0x33) {
            // CTRL_REG1: 50Hz, Normal mode, XYZ enabled (0x47)
            writeReg(0x20, 0x47);
            return true;
        }
    }
    _i2cAddr = 0;
    return false;
}

bool GyroHAL::readRawMPU6050(float& ax, float& ay, float& az) {
    uint8_t raw[6];
    if (!readRegs(0x3B, raw, 6)) return false;
    int16_t x = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t y = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t z = (int16_t)((raw[4] << 8) | raw[5]);
    ax = x / 16384.0f;
    ay = y / 16384.0f;
    az = z / 16384.0f;
    return true;
}

bool GyroHAL::readRawQMI8658(float& ax, float& ay, float& az) {
    uint8_t raw[6];
    if (!readRegs(0x35, raw, 6)) return false;
    int16_t x = (int16_t)(raw[0] | (raw[1] << 8));
    int16_t y = (int16_t)(raw[2] | (raw[3] << 8));
    int16_t z = (int16_t)(raw[4] | (raw[5] << 8));
    ax = x / 16384.0f;
    ay = y / 16384.0f;
    az = z / 16384.0f;
    return true;
}

bool GyroHAL::readRawLSM6DS3(float& ax, float& ay, float& az) {
    uint8_t raw[6];
    if (!readRegs(0x28, raw, 6)) return false;
    int16_t x = (int16_t)(raw[0] | (raw[1] << 8));
    int16_t y = (int16_t)(raw[2] | (raw[3] << 8));
    int16_t z = (int16_t)(raw[4] | (raw[5] << 8));
    ax = (x * 0.061f) / 1000.0f;
    ay = (y * 0.061f) / 1000.0f;
    az = (z * 0.061f) / 1000.0f;
    return true;
}

bool GyroHAL::readRawLIS3DHTR(float& ax, float& ay, float& az) {
    uint8_t raw[6];
    if (!readRegs(0x28 | 0x80, raw, 6)) return false; // auto-increment bit
    int16_t x = (int16_t)(raw[0] | (raw[1] << 8)) >> 4;
    int16_t y = (int16_t)(raw[2] | (raw[3] << 8)) >> 4;
    int16_t z = (int16_t)(raw[4] | (raw[5] << 8)) >> 4;
    ax = x / 1000.0f;
    ay = y / 1000.0f;
    az = z / 1000.0f;
    return true;
}

GyroOrientation GyroHAL::update() {
    if (_type == GyroType::NONE) {
        _lastOrientation.available = false;
        return _lastOrientation;
    }

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    bool ok = false;
    switch (_type) {
        case GyroType::MPU6050: ok = readRawMPU6050(ax, ay, az); break;
        case GyroType::QMI8658: ok = readRawQMI8658(ax, ay, az); break;
        case GyroType::LSM6DS3: ok = readRawLSM6DS3(ax, ay, az); break;
        case GyroType::LIS3DHTR: ok = readRawLIS3DHTR(ax, ay, az); break;
        default: break;
    }

    if (!ok) {
        _lastOrientation.available = false;
        return _lastOrientation;
    }

    _lastOrientation.available = true;
    _lastOrientation.ax = ax;
    _lastOrientation.ay = ay;
    _lastOrientation.az = az;

    // Determine target rotation candidate from gravity vector
    uint8_t targetRot = _stableRotation;
    if (ay < -0.55f) {
        targetRot = 0; // 0° Normal orientation
    } else if (ax > 0.55f) {
        targetRot = 1; // 90° Clockwise (Cable on Left)
    } else if (ay > 0.55f) {
        targetRot = 2; // 180° Inverted (Cable on Top)
    } else if (ax < -0.55f) {
        targetRot = 3; // 270° Counter-Clockwise (Cable on Right)
    }

    // 500ms Hysteresis Debounce Filter
    uint32_t now = millis();
    if (targetRot != _stableRotation) {
        if (targetRot != _candidateRotation) {
            _candidateRotation = targetRot;
            _candidateStartTime = now;
        } else if (now - _candidateStartTime >= 500) {
            _stableRotation = targetRot;
            LOGI("GyroHAL", "Orientation changed -> Rotation: %d (ax: %.2f, ay: %.2f, az: %.2f)", 
                 _stableRotation, ax, ay, az);
        }
    } else {
        _candidateRotation = _stableRotation;
    }

    _lastOrientation.suggestedRotation = _stableRotation;
    return _lastOrientation;
}
