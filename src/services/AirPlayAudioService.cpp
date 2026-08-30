#include "AirPlayAudioService.h"
#include "../core/Logger.h"
#include "AudioAnalysisService.h"
#include <ESPmDNS.h>
#include <esp_mac.h>

AirPlayAudioService airPlayAudioService;

#define AIRPLAY_RTSP_PORT 5000
#define AIRPLAY_AUDIO_PORT 6000
#define AIRPLAY_CONTROL_PORT 6001
#define AIRPLAY_TIMING_PORT 6002

AirPlayAudioService::AirPlayAudioService()
    : _rtspServer(AIRPLAY_RTSP_PORT),
      _running(false),
      _isStreaming(false),
      _serverAudioPort(AIRPLAY_AUDIO_PORT),
      _serverControlPort(AIRPLAY_CONTROL_PORT),
      _serverTimingPort(AIRPLAY_TIMING_PORT),
      _clientControlPort(0),
      _clientTimingPort(0),
      _lastTimingResponse(0) {}

AirPlayAudioService::~AirPlayAudioService() {
    stop();
}

bool AirPlayAudioService::begin(const String& serviceName) {
    if (WiFi.status() != WL_CONNECTED) {
        LOGW("AirPlay", "WiFi not connected, AirPlay init deferred.");
        return false;
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char macHex[13];
    snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    _macAddressStr = String(macHex);

    if (serviceName.length() > 0) {
        _serviceName = serviceName;
    } else {
        char suffix[10];
        snprintf(suffix, sizeof(suffix), "-%02X%02X", mac[4], mac[5]);
        _serviceName = String("ArcadeMatrix AirPlay") + suffix;
    }

    _raopMdnsName = _macAddressStr + "@" + _serviceName;

    // 1. Start RTSP Server on Port 5000
    _rtspServer.begin();

    // 2. Register mDNS RAOP Service (_raop._tcp)
    MDNS.addService("raop", "tcp", AIRPLAY_RTSP_PORT);
    MDNS.addServiceTxt("raop", "tcp", "txtvers", "1");
    MDNS.addServiceTxt("raop", "tcp", "ch", "2");
    MDNS.addServiceTxt("raop", "tcp", "cn", "0,1");
    MDNS.addServiceTxt("raop", "tcp", "et", "0,1");
    MDNS.addServiceTxt("raop", "tcp", "sv", "false");
    MDNS.addServiceTxt("raop", "tcp", "da", "true");
    MDNS.addServiceTxt("raop", "tcp", "sr", "44100");
    MDNS.addServiceTxt("raop", "tcp", "ss", "16");
    MDNS.addServiceTxt("raop", "tcp", "pw", "false");
    MDNS.addServiceTxt("raop", "tcp", "vn", "3");
    MDNS.addServiceTxt("raop", "tcp", "tp", "UDP");
    MDNS.addServiceTxt("raop", "tcp", "md", "0,1,2");
    MDNS.addServiceTxt("raop", "tcp", "am", "AirPort4,107");
    MDNS.addServiceTxt("raop", "tcp", "vs", "220.68");
    MDNS.addServiceTxt("raop", "tcp", "sf", "0x4");
    MDNS.addServiceTxt("raop", "tcp", "sm", "false");

    _running = true;
    LOGI("AirPlay", "Apple AirPlay (RAOP) active as \"%s\" on port %d", _serviceName.c_str(), AIRPLAY_RTSP_PORT);
    return true;
}

void AirPlayAudioService::stop() {
    if (_isStreaming) {
        _isStreaming = false;
        audioHub.updateStatus(AudioSource::AIRPLAY, PlaybackStatus::STATUS_STOPPED);
        audioHub.releasePlayback(AudioSource::AIRPLAY);
    }

    if (_running) {
        _rtspClient.stop();
        _rtspServer.stop();
        _audioUdp.stop();
        _controlUdp.stop();
        _timingUdp.stop();
        _running = false;
        LOGI("AirPlay", "AirPlay service stopped.");
    }
}

void AirPlayAudioService::loop() {
    if (!_running) return;

    // 1. Accept incoming RTSP client or process data
    handleRtspClient();

    // 2. Receive RTP audio packets if stream active
    if (_isStreaming) {
        processAudioPacket();
        processTimingPacket();
    }
}

void AirPlayAudioService::handleRtspClient() {
    if (_rtspClient && !_rtspClient.connected()) {
        _rtspClient.stop();
    }

    WiFiClient incoming = _rtspServer.available();
    if (incoming) {
        if (_rtspClient && _rtspClient.connected()) {
            _rtspClient.stop();
        }
        _rtspClient = incoming;
        LOGI("AirPlay", "New RTSP connection from %s", _rtspClient.remoteIP().toString().c_str());
    }

    if (_rtspClient && _rtspClient.connected() && _rtspClient.available() > 0) {
        String request = "";
        uint32_t startRead = millis();
        while (_rtspClient.available() > 0 && millis() - startRead < 150) {
            request += _rtspClient.readStringUntil('\n') + "\n";
            if (request.endsWith("\r\n\r\n") || request.endsWith("\n\n")) {
                break;
            }
        }

        if (request.length() > 0) {
            processRtspCommand(request);
        }
    }
}

void AirPlayAudioService::sendRtspResponse(const String& cseq, const String& extraHeaders, const String& body) {
    if (!_rtspClient || !_rtspClient.connected()) return;

    String resp = "RTSP/1.0 200 OK\r\n";
    resp += "CSeq: " + cseq + "\r\n";
    resp += "Audio-Jack-Status: connected; type=analog\r\n";
    resp += "Server: AirTunes/220.68\r\n";
    if (extraHeaders.length() > 0) {
        resp += extraHeaders;
        if (!extraHeaders.endsWith("\r\n")) resp += "\r\n";
    }
    if (body.length() > 0) {
        resp += "Content-Type: text/parameters\r\n";
        resp += "Content-Length: " + String(body.length()) + "\r\n\r\n";
        resp += body;
    } else {
        resp += "\r\n";
    }

    _rtspClient.print(resp);
    _rtspClient.flush();
}

void AirPlayAudioService::processRtspCommand(const String& request) {
    int firstLineEnd = request.indexOf('\n');
    String firstLine = (firstLineEnd != -1) ? request.substring(0, firstLineEnd) : request;
    firstLine.trim();

    // Extract CSeq header
    String cseq = "1";
    int cseqIdx = request.indexOf("CSeq:");
    if (cseqIdx == -1) cseqIdx = request.indexOf("cseq:");
    if (cseqIdx != -1) {
        int cseqEnd = request.indexOf('\n', cseqIdx);
        cseq = request.substring(cseqIdx + 5, cseqEnd);
        cseq.trim();
    }

    LOGI("AirPlay", "RTSP RX: %s (CSeq: %s)", firstLine.c_str(), cseq.c_str());

    if (firstLine.startsWith("OPTIONS")) {
        String extra = "Public: OPTIONS, ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, SET_PARAMETER, GET_PARAMETER\r\n";
        sendRtspResponse(cseq, extra);
    } else if (firstLine.startsWith("ANNOUNCE")) {
        // Parse Title / Album from SDP body if present
        int sdpTitle = request.indexOf("i=");
        if (sdpTitle != -1) {
            int titleEnd = request.indexOf('\n', sdpTitle);
            String title = request.substring(sdpTitle + 2, titleEnd);
            title.trim();
            audioHub.updateMetadata(AudioSource::AIRPLAY, title, "AirPlay Audio");
        } else {
            audioHub.updateMetadata(AudioSource::AIRPLAY, "AirPlay Audio", "MacBook / iOS Stream");
        }
        sendRtspResponse(cseq);
    } else if (firstLine.startsWith("SETUP")) {
        // Parse Transport ports
        int ctrlIdx = request.indexOf("control_port=");
        if (ctrlIdx != -1) {
            _clientControlPort = request.substring(ctrlIdx + 13).toInt();
        }
        int timingIdx = request.indexOf("timing_port=");
        if (timingIdx != -1) {
            _clientTimingPort = request.substring(timingIdx + 12).toInt();
        }

        // Start UDP listeners for audio stream
        _audioUdp.begin(_serverAudioPort);
        _controlUdp.begin(_serverControlPort);
        _timingUdp.begin(_serverTimingPort);

        String extra = "Transport: RTP/AVP/UDP;unicast;mode=record;server_port=" + String(_serverAudioPort) +
                       ";control_port=" + String(_serverControlPort) +
                       ";timing_port=" + String(_serverTimingPort) + "\r\nSession: 1";
        sendRtspResponse(cseq, extra);
        LOGI("AirPlay", "SETUP complete. Audio UDP listening on port %d", _serverAudioPort);
    } else if (firstLine.startsWith("RECORD")) {
        _isStreaming = true;
        audioHub.requestPlayback(AudioSource::AIRPLAY);
        audioHub.updateStatus(AudioSource::AIRPLAY, PlaybackStatus::STATUS_PLAYING);
        String extra = "Audio-Latency: 2205";
        sendRtspResponse(cseq, extra);
        LOGI("AirPlay", "RECORD: Streaming started.");
    } else if (firstLine.startsWith("SET_PARAMETER")) {
        if (request.indexOf("volume:") != -1) {
            handleSetVolume(request);
        } else {
            handleMetadata(request);
        }
        sendRtspResponse(cseq);
    } else if (firstLine.startsWith("FLUSH") || firstLine.startsWith("PAUSE")) {
        String extra = "RTP-Info: seq=0;rtptime=0";
        sendRtspResponse(cseq, extra);
    } else if (firstLine.startsWith("TEARDOWN")) {
        _isStreaming = false;
        audioHub.updateStatus(AudioSource::AIRPLAY, PlaybackStatus::STATUS_STOPPED);
        audioHub.releasePlayback(AudioSource::AIRPLAY);
        _audioUdp.stop();
        _controlUdp.stop();
        _timingUdp.stop();
        String extra = "Connection: close";
        sendRtspResponse(cseq, extra);
        LOGI("AirPlay", "TEARDOWN: Streaming finished.");
    } else {
        sendRtspResponse(cseq);
    }
}

void AirPlayAudioService::handleSetVolume(const String& req) {
    int volIdx = req.indexOf("volume:");
    if (volIdx == -1) return;

    float volDb = req.substring(volIdx + 7).toFloat();
    uint8_t volumePercent = 0;

    if (volDb <= -144.0f || volDb < -30.0f) {
        volumePercent = 0;
    } else if (volDb >= 0.0f) {
        volumePercent = 100;
    } else {
        // Map -30.0 dB .. 0.0 dB to 0 .. 100%
        volumePercent = (uint8_t)constrain((int)((volDb + 30.0f) * (100.0f / 30.0f)), 0, 100);
    }

    audioHub.setVolume(volumePercent);
    LOGI("AirPlay", "Volume adjusted: %.1f dB -> %d%%", volDb, volumePercent);
}

void AirPlayAudioService::handleMetadata(const String& body) {
    // Look for text metadata parameters
    int titleIdx = body.indexOf("title=");
    if (titleIdx != -1) {
        int end = body.indexOf('\n', titleIdx);
        String title = body.substring(titleIdx + 6, end);
        title.trim();
        audioHub.updateMetadata(AudioSource::AIRPLAY, title, "AirPlay");
        LOGI("AirPlay", "Metadata Track: %s", title.c_str());
    }
}

void AirPlayAudioService::processAudioPacket() {
    int packetSize = _audioUdp.parsePacket();
    if (packetSize <= 12) return;

    int bytesRead = _audioUdp.read(_audioBuffer, sizeof(_audioBuffer));
    if (bytesRead > 12) {
        // Skip 12-byte RTP header
        uint8_t* payload = _audioBuffer + 12;
        size_t payloadBytes = bytesRead - 12;

        size_t sampleCount = payloadBytes / sizeof(int16_t);
        if (sampleCount > 0) {
            int16_t* samples = (int16_t*)payload;
            audioHub.writePCM(AudioSource::AIRPLAY, samples, sampleCount);
            audioAnalysisService.processSamples(samples, sampleCount);
        }
    }
}

void AirPlayAudioService::processTimingPacket() {
    int packetSize = _timingUdp.parsePacket();
    if (packetSize > 0) {
        uint8_t buf[32];
        _timingUdp.read(buf, sizeof(buf));
        // Respond to timing synchronization packet if needed
    }
}
