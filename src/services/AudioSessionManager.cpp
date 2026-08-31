#include "AudioSessionManager.h"
#include "../core/Logger.h"
#include "BluetoothAudioService.h"
#include "WebRadioService.h"
#include "DLNAService.h"
#include "AirPlayAudioService.h"

AudioSessionManager audioSessionManager;

AudioSessionManager::AudioSessionManager()
    : m_hasActiveEngine(false), m_lastConfigVersion(0) {
}

void AudioSessionManager::begin() {
    m_activeSession.source = AudioSourceType::NONE;
    m_activeSession.state = AudioSessionState::IDLE;
    m_activeSession.isOutputActive = false;
}

void AudioSessionManager::registerSource(IAudioSource* source) {
    if (source) {
        m_sources.push_back(source);
    }
}

void AudioSessionManager::evaluateRequiredServices(const ConfigSnapshot& snapshot) {
    bool needed = false;
    for (const auto& rot : snapshot.rotation) {
        const auto* inst = snapshot.getInstance(rot.instance_id);
        if (inst) {
            if (inst->engine_id == "universal_audio" || inst->engine_id == "music" ||
                inst->engine_id == "spotify" || inst->engine_id == "webradio" ||
                inst->engine_id == "airplay" || inst->engine_id == "dlna") {
                needed = true;
                break;
            }
        }
    }

    if (needed != m_hasActiveEngine) {
        m_hasActiveEngine = needed;
        LOGI("AudioSessionManager", "Audio services state updated dynamically: active=%s", needed ? "true" : "false");
        
        if (m_hasActiveEngine) {
            bluetoothAudioService.begin("ArcadeMatrix Audio");
            webRadioService.begin();
            dlnaService.begin();
            airPlayAudioService.begin();
        }
    }
}

void AudioSessionManager::update(const ConfigSnapshot& snapshot) {
    if (snapshot.version != m_lastConfigVersion) {
        m_lastConfigVersion = snapshot.version;
        evaluateRequiredServices(snapshot);
    }

    if (m_hasActiveEngine) {
        dlnaService.loop();
        airPlayAudioService.loop();
    }
}

void AudioSessionManager::requestPlayback(AudioSourceType source) {
    m_activeSession.source = source;
    m_activeSession.state = AudioSessionState::STREAMING;
    m_activeSession.isOutputActive = true;
}

void AudioSessionManager::releasePlayback(AudioSourceType source) {
    if (m_activeSession.source == source) {
        m_activeSession.source = AudioSourceType::NONE;
        m_activeSession.state = AudioSessionState::IDLE;
        m_activeSession.isOutputActive = false;
    }
}
