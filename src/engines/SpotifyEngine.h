#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../include/core/EngineContract.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

struct SpotifyMediaStatePOD {
    bool isActive = false;
    bool isPlaying = false;
    char title[64] = {0};
    char artist[48] = {0};
    char album[48] = {0};
    char imageUrl[96] = {0};
    char artworkId[32] = {0};
    uint32_t progressMs = 0;
    uint32_t durationMs = 0;
    uint8_t volumePercent = 50;
    uint32_t localTimestampMs = 0;
    uint32_t generation = 0;
};

class SpotifyEngine : public IEngine {
public:
    SpotifyEngine();
    ~SpotifyEngine() override;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    bool isRealtime() const override { return true; }

private:
    String m_clientId = "";
    String m_clientSecret = "";
    String m_refreshToken = "";
    String m_accessToken = "";
    uint32_t m_tokenExpiry = 0;

    bool m_showAlbumArt = true;
    bool m_showProgress = true;
    bool m_showVolume = true;
    bool m_showVisualizer = true;

    // Lock-free double-buffered state for Core 1 (Invariant 1: Zero-Mutex & Zero-Allocation)
    SpotifyMediaStatePOD m_podBuffers[2];
    std::atomic<uint8_t> m_publishedPodIdx{0};
    SpotifyMediaStatePOD m_cachedRenderState;
    bool m_hasPsram = false;

    // Background polling worker task (Core 0)
    TaskHandle_t m_pollTaskHandle = nullptr;
    volatile bool m_taskRunning = false;
    volatile bool m_isActive = false;
    static void pollTaskStatic(void* pvParameters);
    void pollTaskLoop();

    // Artwork caching
    String m_artworkId = "";
    String m_loadedImageUrl = "";

    // Animation & Marquee variables
    int m_marqueeOffset = 0;
    uint32_t m_lastMarqueeTick = 0;
    uint32_t m_lastAnimTick = 0;
    uint8_t m_animFrame = 0;

    void applyConfig(const EngineConfig* config);
    bool refreshAccessToken();
    void pollSpotifyStatus();
};

class SpotifyDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
