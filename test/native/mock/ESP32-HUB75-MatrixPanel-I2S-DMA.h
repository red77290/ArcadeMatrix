#ifndef MOCK_ESP32_HUB75_H
#define MOCK_ESP32_HUB75_H

#include <cstdint>

struct HUB75_I2S_CFG {
    struct i2s_pins {
        int8_t r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
        int8_t a = 0, b = 0, c = 0, d = 0, e = 0;
        int8_t lat = 0, oe = 0, clk = 0;
    };
};

#endif // MOCK_ESP32_HUB75_H
