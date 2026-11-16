#ifndef MOCK_ADAFRUIT_GFX_H
#define MOCK_ADAFRUIT_GFX_H

#include <cstdint>

class Adafruit_GFX {
public:
    Adafruit_GFX(int16_t w = 0, int16_t h = 0) : _width(w), _height(h) {}
    virtual ~Adafruit_GFX() = default;
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) { (void)x; (void)y; (void)color; }
    virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
        for (int16_t i = 0; i < h; ++i) drawPixel(x, y + i, color);
    }
    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
        for (int16_t i = 0; i < w; ++i) drawPixel(x + i, y, color);
    }
    virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, y + h - 1, w, color);
        drawFastVLine(x, y, h, color);
        drawFastVLine(x + w - 1, y, h, color);
    }
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        for (int16_t j = 0; j < h; ++j) drawFastHLine(x, y + j, w, color);
    }
    virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        (void)x1; (void)y1;
        drawPixel(x0, y0, color);
    }
    virtual void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
        (void)r;
        drawPixel(x0, y0, color);
    }
    virtual void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size) {
        (void)c; (void)bg; (void)size;
        drawPixel(x, y, color);
    }
    virtual void fillScreen(uint16_t color) { (void)color; }
    void setCursor(int16_t x, int16_t y) { (void)x; (void)y; }
    void setTextColor(uint16_t c) { (void)c; }
    void setTextSize(uint8_t s) { (void)s; }
    void setTextWrap(bool w) { (void)w; }
    void setFont(const void* f = nullptr) { (void)f; }
    void print(const char* s) { (void)s; }
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
