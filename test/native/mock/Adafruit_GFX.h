#ifndef MOCK_ADAFRUIT_GFX_H
#define MOCK_ADAFRUIT_GFX_H

#include <cstdint>

class Adafruit_GFX {
public:
    Adafruit_GFX(int16_t w = 0, int16_t h = 0) : _width(w), _height(h) {}
    virtual ~Adafruit_GFX() = default;
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) { (void)x; (void)y; (void)color; }
    virtual void fillScreen(uint16_t color) { (void)color; }
    int16_t width() const { return _width; }
    int16_t height() const { return _height; }
protected:
    int16_t _width;
    int16_t _height;
};

#endif // MOCK_ADAFRUIT_GFX_H
