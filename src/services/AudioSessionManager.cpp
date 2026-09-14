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
    // Only the "Universal Music Player" engine displays what the audio receivers
    // (Bluetooth A2DP, DLNA, AirPlay) are streaming, so it is the only engine that
    // justifies paying for them. Starting those receivers allocates a listening
    // socket, mDNS records and, on classic ESP32, the whole Bluedroid A2DP stack.
    //
    // The previous list matched "universal_audio", "music", "webradio", "airplay"
    // and "dlna", none of which is a registered engine id. The real identifier is
    // "music_player" (see MusicEngine.cpp), so the receivers were in practice only
    // started when the unrelated "spotify" engine was configured, and never for the
    // engine that actually needs them.
    bool needed = false;
    for (const auto& rot : snapshot.rotation) {
        const auto* inst = snapshot.getInstance(rot.instance_id);
        if (inst && inst->engine_id == "music_player") {
            needed = true;
            break;
        }
    }

    if (needed != m_hasActiveEngine) {
        m_hasActiveEngine = needed;
        LOGI("AudioSessionManager", "Audio services state updated dynamically: active=%s", needed ? "true" : "false");

        if (m_hasActiveEngine) {
            bluetoothAudioService.begin("ArcadeMatrix Audio");
            dlnaService.begin();
            airPlayAudioService.begin();
            // webRadioService intentionally not started here: it allocates its
            // worker task and decoder buffers only when playback is requested.
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
