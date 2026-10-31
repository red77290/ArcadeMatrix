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
    uint8_t getRotation() const { return rotation; }
    void setRotation(uint8_t r) { rotation = static_cast<uint8_t>(r & 3U); }
protected:
    int16_t _width;
    int16_t _height;
    uint8_t rotation = 0;
};

#endif // MOCK_ADAFRUIT_GFX_H
