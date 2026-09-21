#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "WebRadioService.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#include "AudioAnalysisService.h"
#include <esp_heap_caps.h>

WebRadioService webRadioService;

#define RADIO_CHUNK_SIZE 2048
#define PREBUFFER_THRESHOLD 16384

/// Idle grace period before the worker task terminates and frees its memory.
static constexpr uint32_t IDLE_SHUTDOWN_MS = 5000;

WebRadioService::WebRadioService()
    : _activeClient(nullptr), _requestPlay(false), _requestStop(false),
      _isPlaying(false), _taskRunning(false),
      _isHttps(false), _isWavStream(false), _wavHeaderParsed(false),
      _metaint(0), _bytesUntilMeta(0),
      _audioTaskHandle(nullptr), _idleSinceMs(0), _mp3d(nullptr), _pcmDecBuf(nullptr),
      _streamBuf(nullptr),
      _streamBufCapacity(0), _streamBufLen(0), _isBuffering(true) {
    // Buffers are deliberately NOT allocated here. This object is a global, so its
    // constructor runs during static initialisation on every boot. Allocating the
    // decoder eagerly would permanently consume internal DRAM (and, without PSRAM,
    // a 16 KB stream buffer) even when the radio is never used. begin() performs
    // the allocation on first actual use instead.
}

bool WebRadioService::ensureDecoderStorage() {
    if (_mp3d && _pcmDecBuf && _streamBuf) return true;

    if (!_mp3d) {
        // The decoder state is touched on every frame, so internal DRAM is preferred
        // for speed; PSRAM is an acceptable fallback on a memory-constrained boot.
        _mp3d = (mp3dec_t*)heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if defined(BOARD_HAS_PSRAM)
        if (!_mp3d && psramFound()) {
            _mp3d = (mp3dec_t*)ps_malloc(sizeof(mp3dec_t));
        }
#endif
        if (!_mp3d) {
            LOGE("WebRadio", "Failed to allocate MP3 decoder state (%u bytes).",
                 (unsigned)sizeof(mp3dec_t));
            releaseDecoderStorage();
            return false;
        }
        mp3dec_init(_mp3d);
    }

    if (!_pcmDecBuf) {
        const size_t pcmBytes = sizeof(int16_t) * MINIMP3_MAX_SAMPLES_PER_FRAME;
        _pcmDecBuf = (int16_t*)heap_caps_malloc(pcmBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if defined(BOARD_HAS_PSRAM)
        if (!_pcmDecBuf && psramFound()) {
            _pcmDecBuf = (int16_t*)ps_malloc(pcmBytes);
        }
#endif
        if (!_pcmDecBuf) {
            LOGE("WebRadio", "Failed to allocate PCM decode buffer (%u bytes).", (unsigned)pcmBytes);
            releaseDecoderStorage();
            return false;
        }
    }

    if (!_streamBuf) {
        // Large streaming buffer: PSRAM is strongly preferred, it is only read
        // sequentially and never from the Core 1 hot path.
        _streamBufCapacity = 65536; // 64KB (approx 4 seconds of 128kbps audio)
#if defined(BOARD_HAS_PSRAM)
        if (psramFound()) {
            _streamBuf = (uint8_t*)ps_malloc(_streamBufCapacity);
            if (_streamBuf) {
                LOGI("WebRadio", "Allocated 64KB stream buffer in PSRAM.");
            }
        }
#endif
        if (!_streamBuf) {
            _streamBufCapacity = 16384; // 16KB in internal SRAM
            _streamBuf = (uint8_t*)malloc(_streamBufCapacity);
            if (_streamBuf) {
                LOGI("WebRadio", "Allocated 16KB stream buffer in SRAM.");
            } else {
                _streamBufCapacity = 4096;
                _streamBuf = (uint8_t*)malloc(_streamBufCapacity);
                if (_streamBuf) {
                    LOGW("WebRadio", "Fallback: Allocated 4KB stream buffer.");
                }
            }
        }
        if (!_streamBuf) {
            _streamBufCapacity = 0;
            LOGE("WebRadio", "Failed to allocate any stream buffer.");
            releaseDecoderStorage();
            return false;
        }
    }

    _streamBufLen = 0;
    _isBuffering = true;
    return true;
}

void WebRadioService::releaseDecoderStorage() {
    if (_mp3d) {
        heap_caps_free(_mp3d);
        _mp3d = nullptr;
    }
    if (_pcmDecBuf) {
        heap_caps_free(_pcmDecBuf);
        _pcmDecBuf = nullptr;
    }
    if (_streamBuf) {
        free(_streamBuf);
        _streamBuf = nullptr;
    }
    _streamBufCapacity = 0;
    _streamBufLen = 0;
    _isBuffering = true;
}

WebRadioService::~WebRadioService() {
    stop();
    if (_audioTaskHandle) {
        _taskRunning = false;
        vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelete(_audioTaskHandle);
        _audioTaskHandle = nullptr;
    }
    releaseDecoderStorage();
}

String WebRadioService::getStationName() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _stationName;
}

String WebRadioService::getStreamUrl() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _streamUrl;
}

bool WebRadioService::startWorker() {
    std::lock_guard<std::mutex> lock(_mutex);

    // The previous worker clears _audioTaskHandle only after it has fully released
    // its buffers, still holding this mutex. Observing a non-null handle therefore
    // guarantees a live, fully-owning worker and prevents spawning a second one.
    if (_audioTaskHandle) return true;

    if (!ensureDecoderStorage()) {
        LOGE("WebRadio", "Cannot start WebRadio: decoder storage unavailable.");
        return false;
    }

    _taskRunning = true;
    _idleSinceMs = 0;
    BaseType_t ret = xTaskCreatePinnedToCore(
        audioTaskStatic,
        "WebRadioTask",
        24576,          // 24KB dedicated stack for minimp3 + networking
        this,
        5,              // Priority 5
        &_audioTaskHandle,
        0               // Pinned to Core 0 (Network & Background core)
    );

    if (ret != pdPASS) {
        LOGE("WebRadio", "Failed to create WebRadio FreeRTOS worker task!");
        _taskRunning = false;
        _audioTaskHandle = nullptr;
        releaseDecoderStorage();
        return false;
    }

    LOGI("WebRadio", "WebRadio worker task started on demand.");
    return true;
}

bool WebRadioService::play(const String& url, const String& stationName) {
    if (url.isEmpty()) return false;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _nextUrl = url;
        _nextStation = stationName.length() > 0 ? stationName : "Web Radio";
        _requestPlay = true;
        _requestStop = false;
    }

    if (!startWorker()) {
        std::lock_guard<std::mutex> lock(_mutex);
        _requestPlay = false;
        LOGE("WebRadio", "Playback request rejected: worker task unavailable.");
        return false;
    }

    LOGI("WebRadio", "Queued playback request for: %s", url.c_str());
    return true;
}

void WebRadioService::stop() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _requestStop = true;
        _requestPlay = false;
    }
    LOGI("WebRadio", "Queued stop request.");
}

void WebRadioService::closeActiveClient() {
    if (_activeClient) {
        if (_activeClient->connected()) {
            _activeClient->stop();
        }
        _activeClient = nullptr;
    }
    _client.stop();
    _secureClient.stop();
    _streamBufLen = 0;
    _isBuffering = true;
}

bool WebRadioService::connectStreamInternal(const String& url) {
    closeActiveClient();

    String currentUrl = url;
    int redirectCount = 0;

    while (redirectCount < 3) {
        _isHttps = currentUrl.startsWith("https://");

        // Parse host, port, path from URL
        String host;
        int port = _isHttps ? 443 : 80;
        String path = "/";

        int protoEnd = currentUrl.indexOf("://");
        String urlNoProto = (protoEnd != -1) ? currentUrl.substring(protoEnd + 3) : currentUrl;

        int slashIdx = urlNoProto.indexOf('/');
        if (slashIdx != -1) {
            host = urlNoProto.substring(0, slashIdx);
            path = urlNoProto.substring(slashIdx);
        } else {
            host = urlNoProto;
        }

        int colonIdx = host.indexOf(':');
        if (colonIdx != -1) {
            port = host.substring(colonIdx + 1).toInt();
            host = host.substring(0, colonIdx);
        }

        if (_isHttps) {
            if (!NetworkBudget::canStartTlsSession()) {
                LOGW("WebRadio", "TLS admission denied (insufficient internal DRAM/DMA); cannot stream HTTPS %s", host.c_str());
                return false;
            }
            _secureClient.setInsecure();
            _activeClient = (WiFiClient*)&_secureClient;
        } else {
            _activeClient = (WiFiClient*)&_client;
        }

        LOGI("WebRadio", "Connecting to %s:%d%s (%s)...", host.c_str(), port, path.c_str(), _isHttps ? "HTTPS" : "HTTP");

        bool connected = false;
        if (_isHttps) {
            NetworkBudget::ScopedTlsHandshakeLock tlsLock;
            if (!tlsLock) {
                LOGW("WebRadio", "Failed to acquire TLS handshake lock (timeout/denied); cannot stream %s", host.c_str());
                return false;
            }
            connected = _activeClient->connect(host.c_str(), port);
            // tlsLock unlocks immediately here as it leaves scope, before HTTP headers / payload transfer.
        } else {
            connected = _activeClient->connect(host.c_str(), port);
        }

        if (!connected) {
            LOGE("WebRadio", "Failed to connect to host %s:%d", host.c_str(), port);
            return false;
        }

        // Send HTTP GET request with ICY metadata & universal audio accept headers
        _activeClient->printf("GET %s HTTP/1.0\r\n", path.c_str());
        _activeClient->printf("Host: %s\r\n", host.c_str());
        _activeClient->printf("User-Agent: ArcadeMatrix/3.0\r\n");
        _activeClient->printf("Accept: */*, audio/mpeg, audio/mp3, audio/wav, audio/x-wav, audio/L16\r\n");
        _activeClient->printf("Icy-MetaData: 1\r\n");
        _activeClient->printf("Connection: close\r\n\r\n");

        _metaint = 0;
        _isWavStream = false;
        _wavHeaderParsed = false;
        String redirectUrl = "";
        uint32_t timeout = millis() + 4000;

        while (_activeClient->connected() && millis() < timeout) {
            String line = _activeClient->readStringUntil('\n');
            line.trim();
            if (line.isEmpty()) {
                break; // End of HTTP headers
            }

            String lineLower = line;
            lineLower.toLowerCase();

            if (lineLower.startsWith("location:")) {
                redirectUrl = line.substring(9);
                redirectUrl.trim();
                LOGI("WebRadio", "Redirected to: %s", redirectUrl.c_str());
            } else if (lineLower.startsWith("icy-metaint:")) {
                _metaint = line.substring(12).toInt();
                LOGI("WebRadio", "ICY Metaint interval: %d bytes", _metaint);
            } else if (lineLower.startsWith("icy-name:")) {
                _stationName = line.substring(9);
                _stationName.trim();
                LOGI("WebRadio", "Station Name: %s", _stationName.c_str());
            } else if (lineLower.startsWith("content-type:")) {
                if (lineLower.indexOf("audio/wav") != -1 || 
                    lineLower.indexOf("audio/x-wav") != -1 || 
                    lineLower.indexOf("audio/l16") != -1) {
                    _isWavStream = true;
                    LOGI("WebRadio", "Detected WAV/PCM audio stream format.");
                }
            }
        }

        if (!redirectUrl.isEmpty()) {
            closeActiveClient();
            currentUrl = redirectUrl;
            redirectCount++;
            continue;
        }

        _bytesUntilMeta = _metaint;
        _isBuffering = true;
        return true;
    }

    return false;
}

void WebRadioService::extractIcyMetadata() {
    if (!_activeClient) return;
    int metaLenByte = _activeClient->read();
    if (metaLenByte <= 0) return;

    int metaLen = metaLenByte * 16;
    char metaBuf[512];
    int toRead = min(metaLen, (int)sizeof(metaBuf) - 1);
    int readBytes = _activeClient->readBytes((uint8_t*)metaBuf, toRead);
    metaBuf[readBytes] = '\0';

    for (int i = readBytes; i < metaLen; i++) {
        _activeClient->read();
    }

    String metaStr = String(metaBuf);
    int titleIdx = metaStr.indexOf("StreamTitle='");
    if (titleIdx != -1) {
        int titleEnd = metaStr.indexOf("';", titleIdx + 13);
        if (titleEnd != -1) {
            String newTitle = metaStr.substring(titleIdx + 13, titleEnd);
            newTitle.trim();
            if (newTitle != _currentTitle) {
                _currentTitle = newTitle;
                LOGI("WebRadio", "Now Playing -> %s", _currentTitle.c_str());
                audioHub.updateMetadata(AudioSource::WEBRADIO, _currentTitle, _stationName);
            }
        }
    }
}

void WebRadioService::decodeAndPlayFrames() {
    if (!_streamBuf || _streamBufLen == 0) return;
    // 1. WAV / L16 Uncompressed PCM Stream Handling
    if (_isWavStream || (_streamBufLen >= 12 && memcmp(_streamBuf, "RIFF", 4) == 0 && memcmp(_streamBuf + 8, "WAVE", 4) == 0)) {
        _isWavStream = true;

        if (!_wavHeaderParsed) {
            size_t dataOffset = 44;
            for (size_t i = 12; i + 8 <= _streamBufLen && i < 256; i++) {
                if (memcmp(_streamBuf + i, "data", 4) == 0) {
                    dataOffset = i + 8;
                    break;
                }
            }

            if (_streamBufLen > dataOffset) {
                memmove(_streamBuf, _streamBuf + dataOffset, _streamBufLen - dataOffset);
                _streamBufLen -= dataOffset;
                _wavHeaderParsed = true;
            } else {
                return;
            }
        }

        size_t samplesCount = _streamBufLen / sizeof(int16_t);
        if (samplesCount > 0) {
            int16_t* pcm = (int16_t*)_streamBuf;
            audioHub.writePCM(AudioSource::WEBRADIO, pcm, samplesCount);
            audioAnalysisService.processSamples(pcm, samplesCount);

            size_t bytesConsumed = samplesCount * sizeof(int16_t);
            if (bytesConsumed < _streamBufLen) {
                memmove(_streamBuf, _streamBuf + bytesConsumed, _streamBufLen - bytesConsumed);
                _streamBufLen -= bytesConsumed;
            } else {
                _streamBufLen = 0;
            }
        }
        return;
    }

    // 2. MP3 Compressed Stream Decoding via minimp3
    if (!_mp3d || !_pcmDecBuf) return;
    while (_streamBufLen >= 128) {
        int samples = mp3dec_decode_frame(_mp3d, _streamBuf, _streamBufLen, _pcmDecBuf, &_frameInfo);
        if (_frameInfo.frame_bytes <= 0) {
            if (_streamBufLen > 512) {
                memmove(_streamBuf, _streamBuf + 1, _streamBufLen - 1);
                _streamBufLen--;
                continue;
            }
            break;
        }

        if (samples > 0) {
            size_t totalSamples = samples * _frameInfo.channels;
            audioHub.writePCM(AudioSource::WEBRADIO, _pcmDecBuf, totalSamples);
            audioAnalysisService.processSamples(_pcmDecBuf, totalSamples);
        }

        size_t consumed = (size_t)_frameInfo.frame_bytes;
        if (consumed <= _streamBufLen) {
            memmove(_streamBuf, _streamBuf + consumed, _streamBufLen - consumed);
            _streamBufLen -= consumed;
        } else {
            _streamBufLen = 0;
            break;
        }

        // Allow network task to pull more data if available
        if (_activeClient && _activeClient->available() >= 2048) {
            break;
        }
    }
}

void WebRadioService::handleStream() {
    if (!_isPlaying || !_activeClient || !_activeClient->connected()) {
        if (_isPlaying) {
            LOGW("WebRadio", "Stream disconnected unexpectedly.");
            closeActiveClient();
            _isPlaying = false;
            audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_STOPPED);
            audioHub.releasePlayback(AudioSource::WEBRADIO);
        }
        return;
    }

    size_t avail = _activeClient->available();
    if (avail > 0 && _streamBuf && _streamBufCapacity > 0) {
        size_t spaceLeft = _streamBufCapacity - _streamBufLen;
        if (spaceLeft > 0) {
            size_t toRead = min(avail, min(spaceLeft, (size_t)RADIO_CHUNK_SIZE));
            if (_metaint > 0 && (int)toRead > _bytesUntilMeta) {
                toRead = _bytesUntilMeta;
            }

            int bytesRead = _activeClient->read(_streamBuf + _streamBufLen, toRead);
            if (bytesRead > 0) {
                _streamBufLen += bytesRead;

                if (_metaint > 0) {
                    _bytesUntilMeta -= bytesRead;
                    if (_bytesUntilMeta <= 0) {
                        extractIcyMetadata();
                        _bytesUntilMeta = _metaint;
                    }
                }
            }
        }
    }

    // Pre-buffering control (fill buffer before playing to prevent stuttering)
    if (_isBuffering) {
        size_t threshold = min((size_t)PREBUFFER_THRESHOLD, _streamBufCapacity / 2);
        if (_streamBufLen >= threshold) {
            _isBuffering = false;
            audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_PLAYING);
            LOGI("WebRadio", "Pre-buffering complete (%u bytes). Starting playback.", _streamBufLen);
        } else {
            if (avail == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            return;
        }
    }

    // Underrun protection: if buffer drops dangerously low, re-buffer briefly
    if (_streamBufLen < 1024) {
        _isBuffering = true;
        audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_BUFFERING);
        if (avail == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return;
    }

    decodeAndPlayFrames();

    if (avail == 0) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void WebRadioService::audioTaskStatic(void* pvParameters) {
    WebRadioService* self = static_cast<WebRadioService*>(pvParameters);
    LOGI("WebRadio", "Dedicated FreeRTOS WebRadioTask started on Core %d (Stack: 24KB)", xPortGetCoreID());

    while (self->_taskRunning) {
        // 1. Process pending stop request
        if (self->_requestStop) {
            self->closeActiveClient();
            self->_isPlaying = false;
            self->_requestStop = false;
            audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_STOPPED);
            audioHub.releasePlayback(AudioSource::WEBRADIO);
            LOGI("WebRadio", "WebRadio stream stopped.");
        }

        // 2. Process pending play request
        if (self->_requestPlay) {
            String urlToPlay;
            String stationToPlay;
            {
                std::lock_guard<std::mutex> lock(self->_mutex);
                urlToPlay = self->_nextUrl;
                stationToPlay = self->_nextStation;
                self->_requestPlay = false;
            }

            self->_streamUrl = urlToPlay;
            self->_stationName = stationToPlay;
            self->_currentTitle = stationToPlay;
            self->_streamBufLen = 0;
            self->_isWavStream = false;
            self->_wavHeaderParsed = false;
            if (self->_mp3d) {
                mp3dec_init(self->_mp3d);
            }

            audioHub.requestPlayback(AudioSource::WEBRADIO);
            audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_BUFFERING);
            audioHub.updateMetadata(AudioSource::WEBRADIO, self->_currentTitle, self->_stationName);

            if (self->connectStreamInternal(urlToPlay)) {
                self->_isPlaying = true;
                audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_PLAYING);
                LOGI("WebRadio", "WebRadio stream started: %s (%s)", self->_stationName.c_str(), self->_isWavStream ? "WAV/PCM" : "MP3");
            } else {
                self->_isPlaying = false;
                audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_ERROR);
                audioHub.releasePlayback(AudioSource::WEBRADIO);
                LOGE("WebRadio", "Failed to start stream: %s", urlToPlay.c_str());
            }
        }

        // 3. Process active audio stream
        if (self->_isPlaying) {
            self->handleStream();
            self->_idleSinceMs = 0;
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));

            // Nothing is playing: shut the worker down so the 24 KB stack and every
            // decoder buffer go back to the system. An idle WebRadioService must own
            // no memory at all; a later play() call spawns a fresh worker.
            const uint32_t now = millis();
            if (self->_idleSinceMs == 0) {
                self->_idleSinceMs = now;
            } else if ((now - self->_idleSinceMs) > IDLE_SHUTDOWN_MS) {
                std::lock_guard<std::mutex> lock(self->_mutex);
                if (!self->_requestPlay && !self->_isPlaying) {
                    self->_taskRunning = false;
                }
            }
        }
    }

    {
        // Tear down while holding the mutex so startWorker() can never observe a
        // partially released worker: _audioTaskHandle is cleared last, and only
        // once every buffer has been freed.
        std::lock_guard<std::mutex> lock(self->_mutex);
        self->closeActiveClient();
        self->_isPlaying = false;
        self->_requestStop = false;
        self->releaseDecoderStorage();
        self->_taskRunning = false;
        self->_audioTaskHandle = nullptr;
    }

    LOGI("WebRadio", "WebRadio worker task stopped; decoder memory released.");
    vTaskDelete(NULL);
}
