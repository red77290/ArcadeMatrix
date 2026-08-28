#include "WebRadioService.h"
#include "../core/Logger.h"
#include "AudioAnalysisService.h"

WebRadioService webRadioService;

#define RADIO_CHUNK_SIZE 512

WebRadioService::WebRadioService()
    : _isPlaying(false), _metaint(0), _bytesUntilMeta(0), _lastYieldTime(0) {}

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
        audioHub.updateStatus(AudioSource::WEBRADIO, PlaybackStatus::STATUS_STOPPED);
        audioHub.releasePlayback(AudioSource::WEBRADIO);
        LOGI("WebRadio", "WebRadio stream stopped.");
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

    size_t toRead = min(avail, (size_t)RADIO_CHUNK_SIZE);
    if (_metaint > 0 && (int)toRead > _bytesUntilMeta) {
        toRead = _bytesUntilMeta;
    }

    uint8_t buffer[RADIO_CHUNK_SIZE];
    int bytesRead = _client.read(buffer, toRead);
    if (bytesRead > 0) {
        // Stream PCM samples to AudioHub and AudioAnalysisService
        int16_t* pcm = (int16_t*)buffer;
        size_t samplesCount = bytesRead / sizeof(int16_t);
        audioHub.writePCM(AudioSource::WEBRADIO, pcm, samplesCount);
        audioAnalysisService.processSamples(pcm, samplesCount);

        if (_metaint > 0) {
            _bytesUntilMeta -= bytesRead;
            if (_bytesUntilMeta <= 0) {
                extractIcyMetadata();
                _bytesUntilMeta = _metaint;
            }
        }
    }

    // Yield CPU to FreeRTOS watchdog
    uint32_t now = millis();
    if (now - _lastYieldTime >= 20) {
        vTaskDelay(pdMS_TO_TICKS(2));
        _lastYieldTime = now;
    }
}
