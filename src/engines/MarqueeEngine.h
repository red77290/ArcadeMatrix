#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../include/core/EngineContract.h"
#include "GifEngine.h"

/**
 * @class MarqueeEngine
 * @brief Gameroom Marquee engine displaying custom animations (GIF) or static images (PNG/RAW).
 *
 * Serves as the visual identity of the gameroom in idle rotation (allowRotation = true),
 * and can be pushed on-demand via /api/marquee or frontend sync events.
 */
class MarqueeEngine : public IEngine {
public:
    MarqueeEngine();
    ~MarqueeEngine() override;

    EngineError initialize(EngineContext* context, const EngineConfig* engineConfig) override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void activate() override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* engineConfig) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

    // Copies exactly width*height uint16_t pixels from src and displays them immediately for
    // durationSeconds (default 8s). Retained for live streaming compatibility.
    void show(const uint8_t* rgb565Data, size_t len, unsigned long durationSeconds = 8);
    bool isActive() const { return m_active; }

    bool allowsOverlay() const override { return false; }
    bool allowRotation() const override { return true; }
    bool isRealtime() const override { return true; }
    bool selfPaced() const override { return true; }
    bool needsClear() const override { return false; }

    size_t expectedBufferBytes() const { return (size_t)panelWidth * panelHeight * 2; }

    void setMarqueeFile(const char* path);
    String getMarqueeFile() const { return m_filePath; }
    String resolveMarqueeFile() const;

private:
    int panelWidth;
    int panelHeight;
    uint16_t* m_rawBuffer;
    bool m_active;
    bool m_hasRawBuffer;
    unsigned long m_rawStartTime;
    unsigned long m_rawDurationMs;
    bool m_hasPsram = false;

    String m_filePath;
    float m_speedMultiplier;
    String m_fitMode;

    GifEngine* m_gifEngine;
};

class MarqueeEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
