#include "DLNAService.h"
#include "../core/Logger.h"
#include "../core/AudioHub.h"
#include "WebRadioService.h"
#include <esp_mac.h>

DLNAService dlnaService;

#define SSDP_MULTICAST_IP IPAddress(239, 255, 255, 250)
#define SSDP_PORT 1900

DLNAService::DLNAService() 
    : _running(false), _lastNotifyTime(0) {}

DLNAService::~DLNAService() {
    stop();
}

bool DLNAService::begin(const String& friendlyName) {
    if (WiFi.status() != WL_CONNECTED) {
        LOGW("DLNAService", "WiFi not connected, DLNA init deferred.");
        return false;
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macSuffix[10];
    snprintf(macSuffix, sizeof(macSuffix), "-%02X%02X", mac[4], mac[5]);

    if (friendlyName.length() > 0) {
        _friendlyName = friendlyName;
    } else {
        _friendlyName = String("ArcadeMatrix (DLNA)") + macSuffix;
    }

    char uuidBuf[48];
    snprintf(uuidBuf, sizeof(uuidBuf), "28169123-1122-3344-5566-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    _uuid = String(uuidBuf);

    if (!_udp.beginMulticast(SSDP_MULTICAST_IP, SSDP_PORT)) {
        LOGE("DLNAService", "Failed to start SSDP multicast listener on port %d", SSDP_PORT);
        return false;
    }

    _running = true;
    _lastNotifyTime = millis();
    sendSSDPNotify("ssdp:alive");

    LOGI("DLNAService", "DLNA MediaRenderer active as \"%s\" (UUID: %s)", _friendlyName.c_str(), _uuid.c_str());
    return true;
}

void DLNAService::stop() {
    if (_running) {
        sendSSDPNotify("ssdp:byebye");
        _udp.stop();
        _running = false;
        LOGI("DLNAService", "DLNA MediaRenderer stopped.");
    }
}

void DLNAService::loop() {
    if (!_running) return;

    // Periodic SSDP alive announcement every 60 seconds
    uint32_t now = millis();
    if (now - _lastNotifyTime >= 60000) {
        _lastNotifyTime = now;
        sendSSDPNotify("ssdp:alive");
    }

    // Process incoming discovery packets
    int packetSize = _udp.parsePacket();
    if (packetSize > 0) {
        char buf[1024];
        int len = _udp.read(buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            handleSSDPPacket(buf, _udp.remoteIP(), _udp.remotePort());
        }
    }
}

void DLNAService::handleSSDPPacket(const char* packet, IPAddress remoteIp, uint16_t remotePort) {
    if (!strstr(packet, "M-SEARCH")) return;
    if (!strstr(packet, "ssdp:discover")) return;

    const char* stPos = strstr(packet, "ST:");
    if (!stPos) stPos = strstr(packet, "st:");
    if (!stPos) return;

    stPos += 3;
    while (*stPos == ' ' || *stPos == '\t') stPos++;
    const char* stEnd = strpbrk(stPos, "\r\n");
    if (!stEnd) return;

    String st = String(stPos).substring(0, stEnd - stPos);
    st.trim();

    String uuidPrefix = "uuid:" + _uuid;

    if (st == "ssdp:all" || st.equalsIgnoreCase("ssdp:all")) {
        sendSSDPResponse(remoteIp, remotePort, "upnp:rootdevice", uuidPrefix + "::upnp:rootdevice");
        sendSSDPResponse(remoteIp, remotePort, uuidPrefix, uuidPrefix);
        sendSSDPResponse(remoteIp, remotePort, "urn:schemas-upnp-org:device:MediaRenderer:1", uuidPrefix + "::urn:schemas-upnp-org:device:MediaRenderer:1");
        sendSSDPResponse(remoteIp, remotePort, "urn:schemas-upnp-org:service:AVTransport:1", uuidPrefix + "::urn:schemas-upnp-org:service:AVTransport:1");
        sendSSDPResponse(remoteIp, remotePort, "urn:schemas-upnp-org:service:RenderingControl:1", uuidPrefix + "::urn:schemas-upnp-org:service:RenderingControl:1");
        sendSSDPResponse(remoteIp, remotePort, "urn:schemas-upnp-org:service:ConnectionManager:1", uuidPrefix + "::urn:schemas-upnp-org:service:ConnectionManager:1");
    } else if (st.indexOf("rootdevice") != -1) {
        sendSSDPResponse(remoteIp, remotePort, "upnp:rootdevice", uuidPrefix + "::upnp:rootdevice");
    } else if (st.indexOf("MediaRenderer") != -1) {
        sendSSDPResponse(remoteIp, remotePort, st, uuidPrefix + "::" + st);
    } else if (st.indexOf("AVTransport") != -1) {
        sendSSDPResponse(remoteIp, remotePort, st, uuidPrefix + "::" + st);
    } else if (st.indexOf("RenderingControl") != -1) {
        sendSSDPResponse(remoteIp, remotePort, st, uuidPrefix + "::" + st);
    } else if (st.indexOf("ConnectionManager") != -1) {
        sendSSDPResponse(remoteIp, remotePort, st, uuidPrefix + "::" + st);
    } else if (st.indexOf(_uuid) != -1) {
        sendSSDPResponse(remoteIp, remotePort, st, uuidPrefix);
    }
}

void DLNAService::sendSSDPResponse(IPAddress remoteIp, uint16_t remotePort, const String& st, const String& usn) {
    String loc = "http://" + WiFi.localIP().toString() + ":80/dlna/description.xml";

    char resp[512];
    snprintf(resp, sizeof(resp),
             "HTTP/1.1 200 OK\r\n"
             "CACHE-CONTROL: max-age=1800\r\n"
             "DATE: Sun, 01 Jan 2026 00:00:00 GMT\r\n"
             "EXT:\r\n"
             "LOCATION: %s\r\n"
             "SERVER: ArcadeMatrix/3.0 UPnP/1.0 DLNADOC/1.50\r\n"
             "ST: %s\r\n"
             "USN: %s\r\n"
             "BOOTID.UPNP.ORG: 1\r\n"
             "CONFIGID.UPNP.ORG: 1\r\n\r\n",
             loc.c_str(), st.c_str(), usn.c_str());

    _udp.beginPacket(remoteIp, remotePort);
    _udp.write((const uint8_t*)resp, strlen(resp));
    _udp.endPacket();
}

void DLNAService::sendSSDPNotify(const char* nts) {
    String loc = "http://" + WiFi.localIP().toString() + ":80/dlna/description.xml";
    String uuidPrefix = "uuid:" + _uuid;

    const char* targets[] = {
        "upnp:rootdevice",
        "urn:schemas-upnp-org:device:MediaRenderer:1",
        "urn:schemas-upnp-org:service:AVTransport:1",
        "urn:schemas-upnp-org:service:RenderingControl:1",
        "urn:schemas-upnp-org:service:ConnectionManager:1"
    };

    for (const char* nt : targets) {
        String usn = (strcmp(nt, "upnp:rootdevice") == 0) ? (uuidPrefix + "::upnp:rootdevice") : (uuidPrefix + "::" + nt);
        char notify[512];
        snprintf(notify, sizeof(notify),
                 "NOTIFY * HTTP/1.1\r\n"
                 "HOST: 239.255.255.250:1900\r\n"
                 "CACHE-CONTROL: max-age=1800\r\n"
                 "LOCATION: %s\r\n"
                 "NT: %s\r\n"
                 "NTS: %s\r\n"
                 "SERVER: ArcadeMatrix/3.0 UPnP/1.0 DLNADOC/1.50\r\n"
                 "USN: %s\r\n"
                 "BOOTID.UPNP.ORG: 1\r\n"
                 "CONFIGID.UPNP.ORG: 1\r\n\r\n",
                 loc.c_str(), nt, nts, usn.c_str());

        _udp.beginPacket(SSDP_MULTICAST_IP, SSDP_PORT);
        _udp.write((const uint8_t*)notify, strlen(notify));
        _udp.endPacket();
    }
}

String DLNAService::getDeviceDescriptionXML(const String& friendlyName, const String& uuid, IPAddress ip) {
    String xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    xml += "<root xmlns=\"urn:schemas-upnp-org:device-1-0\" xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">\r\n";
    xml += "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n";
    xml += "  <device>\r\n";
    xml += "    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\r\n";
    xml += "    <friendlyName>" + friendlyName + "</friendlyName>\r\n";
    xml += "    <manufacturer>ArcadeMatrix</manufacturer>\r\n";
    xml += "    <manufacturerURL>http://arcadematrix.local</manufacturerURL>\r\n";
    xml += "    <modelDescription>ArcadeMatrix LED Matrix Audio Renderer</modelDescription>\r\n";
    xml += "    <modelName>ArcadeMatrix MediaRenderer</modelName>\r\n";
    xml += "    <modelNumber>3.0</modelNumber>\r\n";
    xml += "    <modelURL>http://arcadematrix.local</modelURL>\r\n";
    xml += "    <UDN>uuid:" + uuid + "</UDN>\r\n";
    xml += "    <dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">DMR-1.50</dlna:X_DLNADOC>\r\n";
    xml += "    <serviceList>\r\n";
    xml += "      <service>\r\n";
    xml += "        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>\r\n";
    xml += "        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>\r\n";
    xml += "        <SCPDURL>/dlna/avtransport.xml</SCPDURL>\r\n";
    xml += "        <controlURL>/dlna/control/AVTransport</controlURL>\r\n";
    xml += "        <eventSubURL>/dlna/events/AVTransport</eventSubURL>\r\n";
    xml += "      </service>\r\n";
    xml += "      <service>\r\n";
    xml += "        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>\r\n";
    xml += "        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>\r\n";
    xml += "        <SCPDURL>/dlna/renderingcontrol.xml</SCPDURL>\r\n";
    xml += "        <controlURL>/dlna/control/RenderingControl</controlURL>\r\n";
    xml += "        <eventSubURL>/dlna/events/RenderingControl</eventSubURL>\r\n";
    xml += "      </service>\r\n";
    xml += "      <service>\r\n";
    xml += "        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>\r\n";
    xml += "        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>\r\n";
    xml += "        <SCPDURL>/dlna/connectionmanager.xml</SCPDURL>\r\n";
    xml += "        <controlURL>/dlna/control/ConnectionManager</controlURL>\r\n";
    xml += "        <eventSubURL>/dlna/events/ConnectionManager</eventSubURL>\r\n";
    xml += "      </service>\r\n";
    xml += "    </serviceList>\r\n";
    xml += "  </device>\r\n";
    xml += "</root>\r\n";
    return xml;
}

String DLNAService::getAVTransportSCPD() {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
           "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
           "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
           "  <actionList>\r\n"
           "    <action><name>SetAVTransportURI</name></action>\r\n"
           "    <action><name>Play</name></action>\r\n"
           "    <action><name>Pause</name></action>\r\n"
           "    <action><name>Stop</name></action>\r\n"
           "    <action><name>GetTransportInfo</name></action>\r\n"
           "    <action><name>GetPositionInfo</name></action>\r\n"
           "    <action><name>GetMediaInfo</name></action>\r\n"
           "  </actionList>\r\n"
           "</scpd>\r\n";
}

String DLNAService::getRenderingControlSCPD() {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
           "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
           "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
           "  <actionList>\r\n"
           "    <action><name>SetVolume</name></action>\r\n"
           "    <action><name>GetVolume</name></action>\r\n"
           "    <action><name>SetMute</name></action>\r\n"
           "    <action><name>GetMute</name></action>\r\n"
           "  </actionList>\r\n"
           "</scpd>\r\n";
}

String DLNAService::getConnectionManagerSCPD() {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
           "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
           "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
           "  <actionList>\r\n"
           "    <action><name>GetProtocolInfo</name></action>\r\n"
           "    <action><name>GetCurrentConnectionIDs</name></action>\r\n"
           "  </actionList>\r\n"
           "</scpd>\r\n";
}

void DLNAService::registerRoutes(AsyncWebServer* server) {
    if (!server) return;

    // 1. XML Descriptors
    server->on("/dlna/description.xml", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String xml = getDeviceDescriptionXML(_friendlyName, _uuid, WiFi.localIP());
        request->send(200, "text/xml", xml);
    });

    server->on("/dlna/avtransport.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/xml", getAVTransportSCPD());
    });

    server->on("/dlna/renderingcontrol.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/xml", getRenderingControlSCPD());
    });

    server->on("/dlna/connectionmanager.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/xml", getConnectionManagerSCPD());
    });

    // 2. Control SOAP Handlers
    server->on("/dlna/control/AVTransport", HTTP_POST, 
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String bodyAccumulator = "";
            if (index == 0) bodyAccumulator = "";
            bodyAccumulator += String((const char*)data, len);

            if (index + len >= total) {
                handleAVTransportSOAP(request, bodyAccumulator);
                bodyAccumulator = "";
            }
        }
    );

    server->on("/dlna/control/RenderingControl", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String bodyAccumulator = "";
            if (index == 0) bodyAccumulator = "";
            bodyAccumulator += String((const char*)data, len);

            if (index + len >= total) {
                handleRenderingControlSOAP(request, bodyAccumulator);
                bodyAccumulator = "";
            }
        }
    );

    server->on("/dlna/control/ConnectionManager", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String bodyAccumulator = "";
            if (index == 0) bodyAccumulator = "";
            bodyAccumulator += String((const char*)data, len);

            if (index + len >= total) {
                handleConnectionManagerSOAP(request, bodyAccumulator);
                bodyAccumulator = "";
            }
        }
    );
}

void DLNAService::handleAVTransportSOAP(AsyncWebServerRequest *request, const String& body) {
    String responseXml;

    if (body.indexOf("SetAVTransportURI") != -1) {
        int uriStart = body.indexOf("<CurrentURI>");
        int uriEnd = body.indexOf("</CurrentURI>");
        if (uriStart != -1 && uriEnd != -1) {
            _currentUri = body.substring(uriStart + 12, uriEnd);
            _currentUri.trim();
        }

        // Extract Title and Artist from Metadata if available
        int titleStart = body.indexOf("&lt;dc:title&gt;");
        if (titleStart == -1) titleStart = body.indexOf("<dc:title>");
        if (titleStart != -1) {
            int titleEnd = body.indexOf("&lt;/dc:title&gt;", titleStart);
            if (titleEnd == -1) titleEnd = body.indexOf("</dc:title>", titleStart);
            if (titleEnd != -1) {
                int tagLen = (body.charAt(titleStart) == '&') ? 16 : 10;
                _currentTitle = body.substring(titleStart + tagLen, titleEnd);
            }
        } else {
            _currentTitle = "DLNA Stream";
        }

        int artistStart = body.indexOf("&lt;upnp:artist&gt;");
        if (artistStart == -1) artistStart = body.indexOf("<upnp:artist>");
        if (artistStart != -1) {
            int artistEnd = body.indexOf("&lt;/upnp:artist&gt;", artistStart);
            if (artistEnd == -1) artistEnd = body.indexOf("</upnp:artist>", artistStart);
            if (artistEnd != -1) {
                int tagLen = (body.charAt(artistStart) == '&') ? 19 : 13;
                _currentArtist = body.substring(artistStart + tagLen, artistEnd);
            }
        } else {
            _currentArtist = "";
        }

        LOGI("DLNAService", "SetAVTransportURI: \"%s\" (Title: %s, Artist: %s)",
             _currentUri.c_str(), _currentTitle.c_str(), _currentArtist.c_str());

        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:SetAVTransportURIResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body></s:Envelope>";
    } else if (body.indexOf("Play") != -1) {
        LOGI("DLNAService", "Play command received. Starting stream: %s", _currentUri.c_str());
        if (!_currentUri.isEmpty()) {
            webRadioService.play(_currentUri, _currentTitle.isEmpty() ? "DLNA Stream" : _currentTitle);
        }
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:PlayResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body></s:Envelope>";
    } else if (body.indexOf("Stop") != -1) {
        LOGI("DLNAService", "Stop command received.");
        webRadioService.stop();
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:StopResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body></s:Envelope>";
    } else if (body.indexOf("Pause") != -1) {
        LOGI("DLNAService", "Pause command received.");
        webRadioService.stop();
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:PauseResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/></s:Body></s:Envelope>";
    } else if (body.indexOf("GetTransportInfo") != -1) {
        String state = webRadioService.isPlaying() ? "PLAYING" : "STOPPED";
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:GetTransportInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
                      "<CurrentTransportState>" + state + "</CurrentTransportState>"
                      "<CurrentTransportStatus>OK</CurrentTransportStatus>"
                      "<CurrentSpeed>1</CurrentSpeed>"
                      "</u:GetTransportInfoResponse></s:Body></s:Envelope>";
    } else if (body.indexOf("GetPositionInfo") != -1) {
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:GetPositionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
                      "<Track>1</Track>"
                      "<TrackDuration>00:00:00</TrackDuration>"
                      "<TrackMetaData></TrackMetaData>"
                      "<TrackURI>" + _currentUri + "</TrackURI>"
                      "<RelTime>00:00:00</RelTime>"
                      "<AbsTime>00:00:00</AbsTime>"
                      "<RelCount>0</RelCount>"
                      "<AbsCount>0</AbsCount>"
                      "</u:GetPositionInfoResponse></s:Body></s:Envelope>";
    } else {
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
                      "<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring></s:Fault>"
                      "</s:Body></s:Envelope>";
    }

    request->send(200, "text/xml", responseXml);
}

void DLNAService::handleRenderingControlSOAP(AsyncWebServerRequest *request, const String& body) {
    String responseXml;

    if (body.indexOf("SetVolume") != -1) {
        int volStart = body.indexOf("<DesiredVolume>");
        int volEnd = body.indexOf("</DesiredVolume>");
        if (volStart != -1 && volEnd != -1) {
            int vol = body.substring(volStart + 15, volEnd).toInt();
            if (vol < 0) vol = 0;
            if (vol > 100) vol = 100;
            audioHub.setVolume((uint8_t)vol);
            LOGI("DLNAService", "SetVolume: %d%%", vol);
        }
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:SetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\"/></s:Body></s:Envelope>";
    } else if (body.indexOf("GetVolume") != -1) {
        AudioPlaybackState st = audioHub.getPlaybackStateSnapshot();
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:GetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
                      "<CurrentVolume>" + String(st.volume) + "</CurrentVolume>"
                      "</u:GetVolumeResponse></s:Body></s:Envelope>";
    } else if (body.indexOf("GetMute") != -1) {
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:GetMuteResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
                      "<CurrentMute>0</CurrentMute>"
                      "</u:GetMuteResponse></s:Body></s:Envelope>";
    } else {
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
                      "<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring></s:Fault>"
                      "</s:Body></s:Envelope>";
    }

    request->send(200, "text/xml", responseXml);
}

void DLNAService::handleConnectionManagerSOAP(AsyncWebServerRequest *request, const String& body) {
    String responseXml;

    if (body.indexOf("GetProtocolInfo") != -1) {
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:GetProtocolInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
                      "<Source></Source>"
                      "<Sink>http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_FLAGS=01700000000000000000000000000000,http-get:*:audio/mp3:*,http-get:*:audio/x-wav:*,http-get:*:audio/wav:*,http-get:*:audio/aac:*</Sink>"
                      "</u:GetProtocolInfoResponse></s:Body></s:Envelope>";
    } else if (body.indexOf("GetCurrentConnectionIDs") != -1) {
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                      "<s:Body><u:GetCurrentConnectionIDsResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
                      "<ConnectionIDs>0</ConnectionIDs>"
                      "</u:GetCurrentConnectionIDsResponse></s:Body></s:Envelope>";
    } else {
        responseXml = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
                      "<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring></s:Fault>"
                      "</s:Body></s:Envelope>";
    }

    request->send(200, "text/xml", responseXml);
}
