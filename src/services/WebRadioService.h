#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include "../core/AudioHub.h"
#include <minimp3.h>

/**
 * @class WebRadioService
 * @brief Thread-safe, autonomous background audio streaming service.
 * Completely decoupled from loopTask and executed exclusively on Core 0 worker task.
 */
class WebRadioService {
public:
    WebRadioService();
    ~WebRadioService();

    /**
     * @brief Initializes the dedicated FreeRTOS audio worker task on Core 0.
     */
    bool begin();

    /**
     * @brief Requests streaming of the specified radio URL (thread-safe).
     */
    bool play(const String& url, const String& stationName = "Web Radio");

    /**
     * @brief Requests stopping the stream (thread-safe).
     */
    void stop();

    /**
     * @brief Returns whether radio is actively streaming.
     */
    bool isPlaying() const { return _isPlaying; }

    /**
     * @brief Returns current station name.
     */
    String getStationName();

    /**
     * @brief Returns current stream URL.
     */
    String getStreamUrl();

private:
    std::mutex _mutex;
    WiFiClient _client;
    WiFiClientSecure _secureClient;
    WiFiClient* _activeClient;
    String _streamUrl;
    String _stationName;
    String _currentTitle;
    String _nextUrl;
    String _nextStation;
    volatile bool _requestPlay;
    volatile bool _requestStop;
    volatile bool _isPlaying;
    volatile bool _taskRunning;
    bool _isHttps;
    bool _isWavStream;
    bool _wavHeaderParsed;
    int _metaint;
    int _bytesUntilMeta;
    TaskHandle_t _audioTaskHandle;

    mp3dec_t _mp3d;
    mp3dec_frame_info_t _frameInfo;
    int16_t _pcmDecBuf[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint8_t* _streamBuf;
    size_t _streamBufCapacity;
    size_t _streamBufLen;
    bool _isBuffering;

    bool connectStreamInternal(const String& url);
    void handleStream();
    void extractIcyMetadata();
    void decodeAndPlayFrames();
    void closeActiveClient();

    static void audioTaskStatic(void* pvParameters);
};

extern WebRadioService webRadioService;
