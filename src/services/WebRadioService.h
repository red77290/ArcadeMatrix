#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "../core/AudioHub.h"
#include <minimp3.h>

/**
 * @class WebRadioService
 * @brief Autonomous background service for streaming Internet Web Radios (Icecast/Shoutcast).
 * Extracts ICY metadata and pushes PCM audio to AudioHub.
 */
class WebRadioService {
public:
    WebRadioService();
    ~WebRadioService();

    /**
     * @brief Starts streaming the specified radio URL.
     * @param url HTTP/HTTPS stream URL
     * @param stationName Optional station name label
     */
    bool play(const String& url, const String& stationName = "Web Radio");

    /**
     * @brief Stops streaming and releases network connection.
     */
    void stop();

    /**
     * @brief Background loop to read HTTP stream chunks, extract ICY metadata and send audio.
     */
    void loop();

    /**
     * @brief Returns whether radio is actively streaming.
     */
    bool isPlaying() const { return _isPlaying; }

    /**
     * @brief Returns current station name.
     */
    String getStationName() const { return _stationName; }

    /**
     * @brief Returns current stream URL.
     */
    String getStreamUrl() const { return _streamUrl; }

private:
    WiFiClient _client;
    String _streamUrl;
    String _stationName;
    String _currentTitle;
    bool _isPlaying;
    int _metaint;
    int _bytesUntilMeta;
    uint32_t _lastYieldTime;

    mp3dec_t _mp3d;
    uint8_t _streamBuf[4096];
    size_t _streamBufLen;

    bool connectStream();
    void parseIcyHeaders();
    void extractIcyMetadata();
    void decodeAndPlayFrames();
};

extern WebRadioService webRadioService;
