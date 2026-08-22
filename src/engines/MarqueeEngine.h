#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// Displays a full-screen static raw RGB565 image (little-endian, row-major, matching the panel's
// exact resolution) received via HTTP POST, for a bounded duration. Intended for "live marquee"
// / box-art style integrations with arcade frontends (Batocera/Recalbox/RetroPie), mirroring
// ArcadeMatrix_RPi's /api/marquee endpoint. Unlike the RPi (which can decode arbitrary image
// formats via PIL), the ESP32 has no general-purpose image decoder on board, so the companion
// tooling/bridge script is expected to pre-convert artwork to raw RGB565 (see tools/mugen_extractor
// for the existing convention used by fighter sprites and date backgrounds).
#include "../../include/core/EngineContract.h"

class MarqueeEngine : public IEngine {
public:
    MarqueeEngine();
    ~MarqueeEngine();

    EngineError initialize(EngineContext* context, const EngineConfig* engineConfig) override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void activate() override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* engineConfig) override;

    // Copies exactly width*height uint16_t pixels from src and displays them immediately for
    // durationSeconds (default 8s, matching the RPi's typical marquee dwell time).
    void show(const uint8_t* rgb565Data, size_t len, unsigned long durationSeconds = 8);
    bool isActive() const { return active; }

    size_t expectedBufferBytes() const { return (size_t)panelWidth * panelHeight * 2; }

private:
    int panelWidth;
    int panelHeight;
    uint16_t* buffer;
    bool active;
    unsigned long startTime;
    unsigned long durationMs;
    bool m_hasPsram = false;
};
