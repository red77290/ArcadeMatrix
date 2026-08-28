#pragma once
#include <Arduino.h>
#include <mutex>
#include "../hal/AudioOutputHAL.h"

/**
 * @enum AudioSource
 * @brief Logical audio stream sources.
 */
enum class AudioSource : uint8_t {
    NONE = 0,
    BLUETOOTH = 1,
    SPOTIFY = 2,
    AIRPLAY = 3,
    WEBRADIO = 4
};

/**
 * @enum AudioServiceState
 * @brief Lifecycle state of an individual background audio service.
 */
enum class AudioServiceState : uint8_t {
    SERVICE_DISABLED = 0,
    SERVICE_STOPPED = 1,
    SERVICE_STARTING = 2,
    SERVICE_RUNNING = 3,
    SERVICE_ERROR = 4
};

/**
 * @enum PlaybackStatus
 * @brief Current playback status of the active stream.
 */
enum class PlaybackStatus : uint8_t {
    STATUS_STOPPED = 0,
    STATUS_PLAYING = 1,
    STATUS_PAUSED = 2,
    STATUS_BUFFERING = 3,
    STATUS_ERROR = 4
};

/**
 * @struct AudioPlaybackState
 * @brief Normalized snapshot of current audio playback state.
 */
struct AudioPlaybackState {
    AudioSource source = AudioSource::NONE;
    PlaybackStatus status = PlaybackStatus::STATUS_STOPPED;
    String title;
    String artist;
    String album;
    uint32_t durationMs = 0;
    uint32_t positionMs = 0;
    uint8_t volume = 100;
    String artworkId;
    uint32_t generation = 0; // Increments on each meaningful state/track change
};

/**
 * @class AudioHub
 * @brief Central Logical Orchestrator for all Audio Streaming Services.
 * Arbitrates active source, manages normalized state snapshots, and routes PCM to AudioOutputHAL.
 */
class AudioHub {
public:
    AudioHub();
    ~AudioHub() = default;

    /**
     * @brief Initializes AudioHub and sets up AudioOutputHAL.
     */
    bool begin();

    /**
     * @brief Thread-safe getter returning an immutable snapshot of current playback state.
     */
    AudioPlaybackState getPlaybackStateSnapshot();

    /**
     * @brief Requests playback ownership for a specific audio source.
     * Preempts previous active source if different.
     * @return true if source was granted active ownership.
     */
    bool requestPlayback(AudioSource source);

    /**
     * @brief Releases playback ownership for a specific audio source.
     */
    void releasePlayback(AudioSource source);

    /**
     * @brief Updates track metadata from the active audio source.
     */
    void updateMetadata(AudioSource source, 
                        const String& title, 
                        const String& artist = "", 
                        const String& album = "", 
                        uint32_t durationMs = 0, 
                        uint32_t positionMs = 0, 
                        const String& artworkId = "");

    /**
     * @brief Updates playback status (PLAYING, PAUSED, STOPPED, BUFFERING).
     */
    void updateStatus(AudioSource source, PlaybackStatus status);

    /**
     * @brief Updates current track playback position.
     */
    void updatePosition(AudioSource source, uint32_t positionMs);

    /**
     * @brief Sets master audio volume (0-100%).
     */
    void setVolume(uint8_t volume);

    /**
     * @brief Routes PCM samples from the active source to AudioOutputHAL.
     */
    size_t writePCM(AudioSource source, const int16_t* samples, size_t numSamples);

    /**
     * @brief Returns current active audio source.
     */
    AudioSource getActiveSource() const { return _activeSource; }

    /**
     * @brief Helper to convert AudioSource to display name string.
     */
    static const char* getSourceName(AudioSource src);

private:
    std::mutex _mutex;
    AudioSource _activeSource;
    AudioPlaybackState _state;

    void notifyStateChanged();
};

extern AudioHub audioHub;
