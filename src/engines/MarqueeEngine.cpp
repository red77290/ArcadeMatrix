#include "MarqueeEngine.h"
#include <string.h>

MarqueeEngine::MarqueeEngine(MatrixPanel_I2S_DMA* display, int width, int height)
    : matrix(display), panelWidth(width), panelHeight(height), buffer(nullptr),
      active(false), startTime(0), durationMs(0) {
    // Allocate lazily-sized to the configured panel resolution. PSRAM is used automatically by
    // the ESP32 Arduino allocator when available (256x64 / S3 case); on classic ESP32 at 128x32
    // this is only 8KB, well within the free SRAM budget documented in docs/HARDWARE.md.
    buffer = new uint16_t[(size_t)panelWidth * panelHeight];
}

MarqueeEngine::~MarqueeEngine() {
    delete[] buffer;
}

void MarqueeEngine::show(const uint8_t* rgb565Data, size_t len, unsigned long durationSeconds) {
    if (!buffer || len != expectedBufferBytes()) return;
    memcpy(buffer, rgb565Data, len);
    active = true;
    startTime = millis();
    durationMs = durationSeconds * 1000UL;
}

bool MarqueeEngine::isActive() {
    return active;
}

void MarqueeEngine::stop() {
    active = false;
}

void MarqueeEngine::loop() {
    if (!active) return;

    if (millis() - startTime >= durationMs) {
        active = false;
        return;
    }

    // main.cpp's loop() clears the screen every frame before calling into whichever engine is
    // active, so this must redraw every frame while active (it's a static image, but there is no
    // separate "keep previous frame" buffering mode like GifEngine has).
    for (int y = 0; y < panelHeight; y++) {
        for (int x = 0; x < panelWidth; x++) {
            matrix->drawPixel(x, y, buffer[y * panelWidth + x]);
        }
    }
}
