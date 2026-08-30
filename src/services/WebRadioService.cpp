#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "WebRadioService.h"
#include "../core/Logger.h"
#include "AudioAnalysisService.h"

WebRadioService webRadioService;

#define RADIO_CHUNK_SIZE 2048
#define PREBUFFER_THRESHOLD 16384

WebRadioService::WebRadioService()
    : _activeClient(nullptr), _requestPlay(false), _requestStop(false),
      _isPlaying(false), _taskRunning(false),
      _isHttps(false), _isWavStream(false), _wavHeaderParsed(false),
      _metaint(0), _bytesUntilMeta(0),
      _audioTaskHandle(nullptr), _streamBuf(nullptr),
      _streamBufCapacity(0), _streamBufLen(0), _isBuffering(true) {
    mp3dec_init(&_mp3d);

    // Allocate large audio streaming buffer (prefer PSRAM if available)
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
            LOGW("WebRadio", "Fallback: Allocated 4KB stream buffer.");
        }
    }
}

WebRadioService::~WebRadioService() {
    stop();
    if (_audioTaskHandle) {
        _taskRunning = false;
        vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelete(_audioTaskHandle);
        _audioTaskHandle = nullptr;
    }
    if (_streamBuf) {
        free(_streamBuf);
        _streamBuf = nullptr;
    }
}

String WebRadioService::getStationName() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _stationName;
}

String WebRadioService::getStreamUrl() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _streamUrl;
}

bool WebRadioService::begin() {
    if (_taskRunning && _audioTaskHandle) return true;

    _taskRunning = true;
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
        return false;
    }

    LOGI("WebRadio", "WebRadioService initialized with dedicated worker task.");
    return true;
}

bool WebRadioService::play(const String& url, const String& stationName) {
    if (url.isEmpty()) return false;

    if (!_taskRunning || !_audioTaskHandle) {
        begin();
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _nextUrl = url;
        _nextStation = stationName.length() > 0 ? stationName : "Web Radio";
        _requestPlay = true;
        _requestStop = false;
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

        if (_isHttps) {
            _secureClient.setInsecure();
            _activeClient = (WiFiClient*)&_secureClient;
        } else {
            _activeClient = (WiFiClient*)&_client;
        }

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

        LOGI("WebRadio", "Connecting to %s:%d%s (%s)...", host.c_str(), port, path.c_str(), _isHttps ? "HTTPS" : "HTTP");

        if (!_activeClient->connect(host.c_str(), port)) {
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
    while (_streamBufLen >= 128) {
        int samples = mp3dec_decode_frame(&_mp3d, _streamBuf, _streamBufLen, _pcmDecBuf, &_frameInfo);
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
            mp3dec_init(&self->_mp3d);

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
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    vTaskDelete(NULL);
}
