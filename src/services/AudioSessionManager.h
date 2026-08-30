#pragma once
#include <Arduino.h>
#include <vector>
#include <memory>
#include "../core/ConfigLoader.h"

enum class AudioSourceType : uint8_t {
    NONE,
    WEBRADIO,
    BLUETOOTH,
    AIRPLAY,
    DLNA
};

enum class AudioSessionState : uint8_t {
    IDLE,
    STREAMING,
    PAUSED
};

struct AudioSession {
    AudioSourceType source = AudioSourceType::NONE;
    AudioSessionState state = AudioSessionState::IDLE;
    uint8_t volume = 100;
    bool isOutputActive = false;
};

class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual AudioSourceType getSourceType() const = 0;
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isPlaying() const = 0;
};

class AudioSessionManager {
public:
    AudioSessionManager();
    
    void begin();
    void update(const ConfigSnapshot& snapshot);
    
    void registerSource(IAudioSource* source);
    void requestPlayback(AudioSourceType source);
    void releasePlayback(AudioSourceType source);

    inline const AudioSession& getActiveSession() const { return m_activeSession; }
    inline bool hasActiveAudioEngine() const { return m_hasActiveEngine; }

private:
    std::vector<IAudioSource*> m_sources;
    AudioSession m_activeSession;
    bool m_hasActiveEngine = false;
    uint32_t m_lastConfigVersion = 0;

    void evaluateRequiredServices(const ConfigSnapshot& snapshot);
};

extern AudioSessionManager audioSessionManager;
