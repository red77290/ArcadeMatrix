#ifndef MOCK_WIRE_H
#define MOCK_WIRE_H

#include <cstdint>
#include <cstddef>

class TwoWire {
public:
    void begin() {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 0; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t len) { return len; }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    int read() { return -1; }
    int available() { return 0; }
};

extern TwoWire Wire;

#endif // MOCK_WIRE_H
