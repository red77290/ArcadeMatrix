#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include "../core/AudioHub.h"

/**
 * @class AirPlayAudioService
 * @brief Autonomous Apple AirPlay 1 / RAOP (AirTunes) Audio Receiver for ArcadeMatrix.
 * Enables direct lossless Wi-Fi audio streaming from MacBooks, iPhones, iPads, and AirPlay senders.
 */
class AirPlayAudioService {
public:
    AirPlayAudioService();
    ~AirPlayAudioService();

    /**
     * @brief Starts the AirPlay RTSP control server on port 5000 and registers mDNS RAOP services.
     * @param serviceName Custom AirPlay advertised device name
     */
    bool begin(const String& serviceName = "");

    /**
     * @brief Background loop to process RTSP control messages and receive RTP audio packets.
     */
    void loop();

    /**
     * @brief Stops AirPlay service and releases network sockets.
     */
    void stop();

    /**
     * @brief Returns whether an AirPlay session is currently active/streaming.
     */
    bool isStreaming() const { return _isStreaming; }

    /**
     * @brief Returns advertised friendly name.
     */
    String getServiceName() const { return _serviceName; }

private:
    WiFiServer _rtspServer;
    WiFiClient _rtspClient;
    WiFiUDP _audioUdp;
    WiFiUDP _controlUdp;
    WiFiUDP _timingUdp;

    bool _running;
    bool _isStreaming;
    String _serviceName;
    String _macAddressStr;
    String _raopMdnsName;

    uint16_t _serverAudioPort;
    uint16_t _serverControlPort;
    uint16_t _serverTimingPort;
    uint16_t _clientControlPort;
    uint16_t _clientTimingPort;

    uint8_t _audioBuffer[2048];
    uint32_t _lastTimingResponse;

    void handleRtspClient();
    void processRtspCommand(const String& request);
    void sendRtspResponse(const String& cseq, const String& extraHeaders = "", const String& body = "");
    void processAudioPacket();
    void processTimingPacket();
    void handleSetVolume(const String& line);
    void handleMetadata(const String& body);
};

extern AirPlayAudioService airPlayAudioService;
