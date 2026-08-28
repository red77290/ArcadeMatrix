#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "WebRadioService.h"
#include "../core/Logger.h"
#include "AudioAnalysisService.h"

WebRadioService webRadioService;

#define RADIO_CHUNK_SIZE 512

WebRadioService::WebRadioService()
    : _isPlaying(false), _metaint(0), _bytesUntilMeta(0), _lastYieldTime(0), _streamBufLen(0) {
    mp3dec_init(&_mp3d);
}

WebRadioService::~WebRadioService() {
    stop();
}

bool WebRadioService::connectStream() {
    if (_streamUrl.isEmpty()) return false;

    // Parse host, port, path from URL
    String host;
    int port = 80;
    String path = "/";

    int protoEnd = _streamUrl.indexOf("://");
    String urlNoProto = (protoEnd != -1) ? _streamUrl.substring(protoEnd + 3) : _streamUrl;

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

    LOGI("WebRadio", "Connecting to %s:%d%s ...", host.c_str(), port, path.c_str());

    if (!_client.connect(host.c_str(), port)) {
        LOGE("WebRadio", "Failed to connect to HTTP host %s:%d", host.c_str(), port);
        return false;
    }

    // Send HTTP GET request with ICY metadata header
    _client.printf("GET %s HTTP/1.0\r\n", path.c_str());
    _client.printf("Host: %s\r\n", host.c_str());
    _client.printf("User-Agent: ArcadeMatrix/1.0\r\n");
    _client.printf("Icy-MetaData: 1\r\n");
    _client.printf("Connection: close\r\n\r\n");

    parseIcyHeaders();
    return true;
}

void WebRadioService::parseIcyHeaders() {
    _metaint = 0;
    uint32_t timeout = millis() + 3000;

    while (_client.connected() && millis() < timeout) {
        String line = _client.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) {
            break; // End of HTTP headers
        }

        if (line.startsWith("icy-metaint:")) {
            _metaint = line.substring(12).toInt();
            LOGI("WebRadio", "ICY Metaint interval: %d bytes", _metaint);
        } else if (line.startsWith("icy-name:")) {
            _stationName = line.substring(9);
            _stationName.trim();
            LOGI("WebRadio", "Station Name: %s", _stationName.c_str());
        }
    }

    _bytesUntilMeta = _metaint;
}

void WebRadioService::extractIcyMetadata() {
    int metaLenByte = _client.read();
    if (metaLenByte <= 0) return;

    int metaLen = metaLenByte * 16;
    char metaBuf[512];
    int toRead = min(metaLen, (int)sizeof(metaBuf) - 1);
    int readBytes = _client.readBytes((uint8_t*)metaBuf, toRead);
    metaBuf[readBytes] = '\0';

    // Flush any overflow bytes
    for (int i = readBytes; i < metaLen; i++) {
        _client.read();
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

bool WebRadioService::play(const String& url, const String& stationName) {
    stop();

    _streamUrl = url;
    _stationName = stationName.length() > 0 ? stationName : "Web Radio";
    _currentTitle = _stationName;
    _streamBufLen = 0;
    mp3dec_init(&_mp3d);

    audioHub.requestPlayback(AudioSource::WEBRADIO);
    audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_BUFFERING);
    audioHub.updateMetadata(AudioSource::WEBRADIO, _currentTitle, _stationName);

    if (connectStream()) {
        _isPlaying = true;
        audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_PLAYING);
        LOGI("WebRadio", "WebRadio stream started: %s", _stationName.c_str());
        return true;
    } else {
        _isPlaying = false;
        audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_ERROR);
        return false;
    }
}

void WebRadioService::stop() {
    if (_isPlaying) {
        _isPlaying = false;
        if (_client.connected()) {
            _client.stop();
        }
        _streamBufLen = 0;
        audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_STOPPED);
        audioHub.releasePlayback(AudioSource::WEBRADIO);
        LOGI("WebRadio", "WebRadio stream stopped.");
    }
}

void WebRadioService::decodeAndPlayFrames() {
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info;

    while (_streamBufLen >= 128) {
        int samples = mp3dec_decode_frame(&_mp3d, _streamBuf, _streamBufLen, pcm, &info);
        if (info.frame_bytes <= 0) {
            // Check if we need more bytes or if there is junk header
            if (_streamBufLen > 512) {
                // Skip 1 junk byte to re-sync
                memmove(_streamBuf, _streamBuf + 1, _streamBufLen - 1);
                _streamBufLen--;
                continue;
            }
            break;
        }

        if (samples > 0) {
            size_t totalSamples = samples * info.channels;
            audioHub.writePCM(AudioSource::WEBRADIO, pcm, totalSamples);
            audioAnalysisService.processSamples(pcm, totalSamples);
        }

        size_t consumed = (size_t)info.frame_bytes;
        if (consumed <= _streamBufLen) {
            memmove(_streamBuf, _streamBuf + consumed, _streamBufLen - consumed);
            _streamBufLen -= consumed;
        } else {
            _streamBufLen = 0;
            break;
        }
    }
}

void WebRadioService::loop() {
    if (!_isPlaying || !_client.connected()) {
        if (_isPlaying && !_client.connected()) {
            LOGW("WebRadio", "Stream disconnected unexpectedly, stopping...");
            stop();
        }
        return;
    }

    size_t avail = _client.available();
    if (avail == 0) return;

    size_t spaceLeft = sizeof(_streamBuf) - _streamBufLen;
    if (spaceLeft == 0) {
        decodeAndPlayFrames();
        spaceLeft = sizeof(_streamBuf) - _streamBufLen;
        if (spaceLeft == 0) {
            // Buffer full: drop oldest 512 bytes to recover stream sync
            memmove(_streamBuf, _streamBuf + 512, _streamBufLen - 512);
            _streamBufLen -= 512;
            spaceLeft = 512;
        }
    }

    size_t toRead = min(avail, min(spaceLeft, (size_t)RADIO_CHUNK_SIZE));
    if (_metaint > 0 && (int)toRead > _bytesUntilMeta) {
        toRead = _bytesUntilMeta;
    }

    int bytesRead = _client.read(_streamBuf + _streamBufLen, toRead);
    if (bytesRead > 0) {
        _streamBufLen += bytesRead;

        if (_metaint > 0) {
            _bytesUntilMeta -= bytesRead;
            if (_bytesUntilMeta <= 0) {
                extractIcyMetadata();
                _bytesUntilMeta = _metaint;
            }
        }

        decodeAndPlayFrames();
    }

    // Yield CPU to FreeRTOS watchdog
    uint32_t now = millis();
    if (now - _lastYieldTime >= 20) {
        vTaskDelay(pdMS_TO_TICKS(2));
        _lastYieldTime = now;
    }
}
