#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>

/**
 * @class DLNAService
 * @brief Autonomous UPnP / DLNA MediaRenderer and SSDP Discovery Service for ArcadeMatrix.
 * Enables zero-config wireless streaming from AudioCast, BubbleUPnP, RiMusic, ViMusic,
 * VLC, Windows Media Player, and other DLNA/UPnP clients.
 */
class DLNAService {
public:
    DLNAService();
    ~DLNAService();

    /**
     * @brief Initializes the DLNA MediaRenderer service and starts SSDP multicast listener.
     * @param friendlyName Custom name advertised to DLNA controllers
     */
    bool begin(const String& friendlyName = "");

    /**
     * @brief Registers DLNA description, SCPD and SOAP control HTTP endpoints on the web server.
     */
    void registerRoutes(AsyncWebServer* server);

    /**
     * @brief Background loop to process incoming SSDP discovery packets and periodic broadcasts.
     */
    void loop();

    /**
     * @brief Stops DLNA service and releases network sockets.
     */
    void stop();

    /**
     * @brief Returns current advertised friendly name.
     */
    String getFriendlyName() const { return _friendlyName; }

private:
    WiFiUDP _udp;
    bool _running;
    String _friendlyName;
    String _uuid;
    String _currentUri;
    String _currentTitle;
    String _currentArtist;
    uint32_t _lastNotifyTime;

    void handleSSDPPacket(const char* packet, IPAddress remoteIp, uint16_t remotePort);
    void sendSSDPResponse(IPAddress remoteIp, uint16_t remotePort, const String& st, const String& usn);
    void sendSSDPNotify(const char* nts);

    static String getDeviceDescriptionXML(const String& friendlyName, const String& uuid, IPAddress ip);
    static String getAVTransportSCPD();
    static String getRenderingControlSCPD();
    static String getConnectionManagerSCPD();

    void handleAVTransportSOAP(AsyncWebServerRequest *request, const String& body);
    void handleRenderingControlSOAP(AsyncWebServerRequest *request, const String& body);
    void handleConnectionManagerSOAP(AsyncWebServerRequest *request, const String& body);
};

extern DLNAService dlnaService;
