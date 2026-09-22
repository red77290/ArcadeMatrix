#include "AudioHub.h"
#include "Logger.h"

AudioHub audioHub;

AudioHub::AudioHub()
    : _activeSource(AudioSource::NONE), _publishedPodIdx(0) {
    _state.source = AudioSource::NONE;
    _state.status = PlaybackStatus::STATUS_STOPPED;
    _state.volume = 80;
    _state.generation = 1;
    syncPodState();
}

bool AudioHub::begin() {
    std::lock_guard<std::mutex> lock(_mutex);
    LOGI("AudioHub", "Initializing AudioHub central service (playback on-demand)...");
    return true;
}

const char* AudioHub::getSourceName(AudioSource src) {
    switch (src) {
        case AudioSource::BLUETOOTH: return "Bluetooth";
        case AudioSource::SPOTIFY:   return "Spotify";
        case AudioSource::AIRPLAY:   return "AirPlay";
        case AudioSource::WEBRADIO:  return "WebRadio";
        default:                     return "None";
    }
}

void AudioHub::syncPodState() {
    uint8_t nextIdx = 1 - _publishedPodIdx.load(std::memory_order_relaxed);
    AudioPlaybackStatePOD& target = _podBuffers[nextIdx];
    target.source = _state.source;
    target.status = _state.status;
    strncpy(target.title, _state.title.c_str(), sizeof(target.title) - 1);
    target.title[sizeof(target.title) - 1] = '\0';
    strncpy(target.artist, _state.artist.c_str(), sizeof(target.artist) - 1);
    target.artist[sizeof(target.artist) - 1] = '\0';
    strncpy(target.album, _state.album.c_str(), sizeof(target.album) - 1);
    target.album[sizeof(target.album) - 1] = '\0';
    strncpy(target.artworkId, _state.artworkId.c_str(), sizeof(target.artworkId) - 1);
    target.artworkId[sizeof(target.artworkId) - 1] = '\0';
    target.durationMs = _state.durationMs;
    target.positionMs = _state.positionMs;
    target.volume = _state.volume;
    target.generation = _state.generation;

    _publishedPodIdx.store(nextIdx, std::memory_order_release);
}

void AudioHub::notifyStateChanged() {
    _state.generation++;
    syncPodState();
}

AudioPlaybackState AudioHub::getPlaybackStateSnapshot() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _state; // Returns copy
}

AudioPlaybackStatePOD AudioHub::getPlaybackStatePOD() const {
    uint8_t idx = _publishedPodIdx.load(std::memory_order_acquire);
    return _podBuffers[idx];
}

bool AudioHub::requestPlayback(AudioSource source) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_activeSource != source) {
        LOGI("AudioHub", "Source arbitration: Preempting %s -> New active source: %s",
             getSourceName(_activeSource), getSourceName(source));
        _activeSource = source;
        _state.source = source;
        _state.status = PlaybackStatus::STATUS_BUFFERING;
        notifyStateChanged();
    }
    if (hardwareHAL.isAudioSamplingActive()) {
        // Keep the capture intent: the visualizer/decibel engine may still be the active
        // screen. Capture resumes by itself once playback releases the bus.
        hardwareHAL.stopAudioSampling(false);
    }
    audioOutputHAL.preparePlayback();
    return true;
}

void AudioHub::releasePlayback(AudioSource source) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_activeSource == source) {
        LOGI("AudioHub", "Source %s released playback ownership.", getSourceName(source));
        _activeSource = AudioSource::NONE;
        _state.source = AudioSource::NONE;
        _state.status = PlaybackStatus::STATUS_STOPPED;
        notifyStateChanged();
        audioOutputHAL.stop();
    }
}

void AudioHub::updateMetadata(AudioSource source, 
                              const String& title, 
                              const String& artist, 
                              const String& album, 
                              uint32_t durationMs, 
                              uint32_t positionMs, 
                              const String& artworkId) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_activeSource != source && _activeSource != AudioSource::NONE) {
        return; // Reject metadata updates from inactive sources
    }

    _state.source = source;
    _state.title = title;
    _state.artist = artist;
    _state.album = album;
    _state.durationMs = durationMs;
    _state.positionMs = positionMs;
    _state.artworkId = artworkId;
    notifyStateChanged();

    LOGI("AudioHub", "[%s] Track: \"%s\" by \"%s\" (%u ms)", 
         getSourceName(source), title.c_str(), artist.c_str(), durationMs);
}

void AudioHub::updateStatus(AudioSource source, PlaybackStatus status) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_activeSource != source && _activeSource != AudioSource::NONE) {
        return;
    }

    if (_state.status != status) {
        _state.status = status;
        notifyStateChanged();
        LOGI("AudioHub", "[%s] Status changed -> %d", getSourceName(source), (int)status);
    }
}

void AudioHub::updatePosition(AudioSource source, uint32_t positionMs) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_activeSource == source) {
        _state.positionMs = positionMs;
        syncPodState();
    }
}

void AudioHub::setVolume(uint8_t volume) {
    std::lock_guard<std::mutex> lock(_mutex);
    _state.volume = (volume > 100) ? 100 : volume;
    audioOutputHAL.setVolume(_state.volume);
    notifyStateChanged();
}

size_t AudioHub::writePCM(AudioSource source, const int16_t* samples, size_t numSamples) {
    if (_activeSource != source || !samples || numSamples == 0) {
        return 0;
    }
    return audioOutputHAL.writeSamples(samples, numSamples);
}
