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
     * @brief Requests streaming of the specified radio URL (thread-safe).
     *
     * This is the single entry point that makes the service allocate anything:
     * the worker task and its buffers are created here and released again as soon
     * as playback ends. An idle WebRadioService owns no task and no buffer.
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
    uint32_t _idleSinceMs;

    // Decoder state is allocated on demand rather than embedded in the object.
    // Embedding mp3dec_t (~6 KB) and the PCM scratch buffer (~4.6 KB) reserved
    // roughly 11 KB of internal DRAM in .bss for the whole lifetime of the
    // firmware, even on devices that never stream a radio station. Internal DRAM
    // is the scarce resource on this platform (allocations <= 4 KB are forced
    // internal by CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL), and mbedTLS needs a large
    // internal budget for every TLS handshake. Keeping this storage lazy leaves
    // that budget available to the rest of the system.
    mp3dec_t* _mp3d;
    mp3dec_frame_info_t _frameInfo;
    int16_t* _pcmDecBuf;
    uint8_t* _streamBuf;
    size_t _streamBufCapacity;
    size_t _streamBufLen;
    bool _isBuffering;

    bool connectStreamInternal(const String& url);
    void handleStream();
    void extractIcyMetadata();
    void decodeAndPlayFrames();
    void closeActiveClient();

    /**
     * @brief Allocates the MP3 decoder state, PCM scratch buffer and stream buffer.
     * Called from startWorker() only, i.e. when playback actually begins.
     * @return true when every buffer is available, false when allocation failed.
     */
    bool ensureDecoderStorage();

    /**
     * @brief Releases every buffer acquired by ensureDecoderStorage().
     */
    void releaseDecoderStorage();

    /**
     * @brief Spawns the Core 0 worker task and its buffers if not already running.
     *
     * Nothing is allocated while the service is idle: an inactive WebRadioService
     * owns neither the 24 KB task stack nor any decoder buffer.
     *
     * @return true when a worker task is running and ready to consume requests.
     */
    bool startWorker();

    static void audioTaskStatic(void* pvParameters);
};

extern WebRadioService webRadioService;
