#include "MarqueeEngine.h"
#include "../hal/HardwareHAL.h"
#include <string.h>

MarqueeEngine::MarqueeEngine()
    : panelWidth(0), panelHeight(0), buffer(nullptr),
      active(false), startTime(0), durationMs(0) {
}

EngineError MarqueeEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    auto matrix = context->getMatrix();
    if (!matrix) return EngineError::HardwareUnavailable;

    panelWidth = matrix->width();
    panelHeight = matrix->height();

    // Allocate lazily-sized to the configured panel resolution. PSRAM is used automatically by
    // the ESP32 Arduino allocator when available (256x64 / S3 case); on classic ESP32 at 128x32
    // this is only 8KB, well within the free SRAM budget documented in docs/HARDWARE.md.
    size_t bufferSize = (size_t)panelWidth * panelHeight * sizeof(uint16_t);
    if (hardwareHAL.capabilities().hasPsram) {
        buffer = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
    } else {
        buffer = (uint16_t*)malloc(bufferSize);
    }
    
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

MarqueeEngine::~MarqueeEngine() {
    if (buffer) {
        if (hardwareHAL.capabilities().hasPsram) heap_caps_free(buffer);
        else free(buffer);
    }
}

void MarqueeEngine::show(const uint8_t* rgb565Data, size_t len, unsigned long durationSeconds) {
    if (!buffer || len != expectedBufferBytes()) return;
    memcpy(buffer, rgb565Data, len);
    active = true;
    startTime = millis();
    durationMs = durationSeconds * 1000UL;
}

void MarqueeEngine::activate() {}

void MarqueeEngine::deactivate() {
    active = false;
}

void MarqueeEngine::onConfigChanged(const EngineConfig* engineConfig) {}

void MarqueeEngine::update(EngineContext* context) {
    if (!active) return;
    if (millis() - startTime >= durationMs) {
        active = false;
    }
}

void MarqueeEngine::render(EngineContext* context) {
    if (!active || !buffer) return;

    auto matrix = context->getMatrix();
    if (!matrix) return;

    // main.cpp's loop() clears the screen every frame before calling into whichever engine is
    // active, so this must redraw every frame while active (it's a static image, but there is no
    // separate "keep previous frame" buffering mode like GifEngine has).
    for (int y = 0; y < panelHeight; y++) {
        for (int x = 0; x < panelWidth; x++) {
            matrix->drawPixel(x, y, buffer[y * panelWidth + x]);
        }
    }
}
