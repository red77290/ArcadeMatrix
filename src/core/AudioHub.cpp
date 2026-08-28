#include "AudioHub.h"
#include "Logger.h"

AudioHub audioHub;

AudioHub::AudioHub()
    : _activeSource(AudioSource::NONE) {
    _state.source = AudioSource::NONE;
    _state.status = PlaybackStatus::STATUS_STOPPED;
    _state.volume = 80;
    _state.generation = 1;
}

bool AudioHub::begin() {
    std::lock_guard<std::mutex> lock(_mutex);
    LOGI("AudioHub", "Initializing AudioHub central service...");
    bool ok = audioOutputHAL.begin();
    if (ok) {
        audioOutputHAL.setVolume(_state.volume);
    }
    return ok;
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

void AudioHub::notifyStateChanged() {
    _state.generation++;
}

AudioPlaybackState AudioHub::getPlaybackStateSnapshot() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _state; // Returns copy
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
