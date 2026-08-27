#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../include/core/EngineContract.h"

struct GoogleCastMediaState {
    bool isActive = false;
    bool isPlaying = false;
    String appName = "";
    String title = "";
    String artist = "";
    String album = "";
    String imageUrl = "";
    float currentTimeSec = 0.0f;
    float durationSec = 0.0f;
    float volumeLevel = 0.5f;
    uint32_t lastPollTime = 0;
};

class GoogleCastEngine : public IEngine {
public:
    GoogleCastEngine();
    virtual ~GoogleCastEngine() = default;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    bool isRealtime() const override { return true; }

private:
    String m_deviceIp = "";
    String m_deviceName = "";
    bool m_showAlbumArt = true;
    bool m_showProgress = true;
    bool m_showVolume = true;
    bool m_showVisualizer = true;

    GoogleCastMediaState m_state;
    bool m_hasPsram = false;

    // Animation & Marquee variables
    int m_marqueeOffset = 0;
    uint32_t m_lastMarqueeTick = 0;
    uint32_t m_lastAnimTick = 0;
    uint8_t m_animFrame = 0;
    String m_lastLoggedTrack = "";

    void applyConfig(const EngineConfig* config);
    void pollCastStatus();
};

class GoogleCastDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
