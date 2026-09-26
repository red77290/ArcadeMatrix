#include "GoogleCastEngine.h"
#include "../core/drawing/IDrawingSurface.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#include "../core/SpiRamJsonDocument.h"
#include "../services/ArtworkService.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <vector>

struct CastMessage {
    int protocolVersion = 0;
    String sourceId;
    String destinationId;
    String namespaceUri;
    String payloadUtf8;
};

static void encodeVarint(uint64_t val, std::vector<uint8_t>& buf) {
    while (val >= 0x80) {
        buf.push_back((uint8_t)((val & 0x7F) | 0x80));
        val >>= 7;
    }
    buf.push_back((uint8_t)val);
}

static void encodeString(uint8_t tag, const String& str, std::vector<uint8_t>& buf) {
    buf.push_back(tag);
    encodeVarint(str.length(), buf);
    const uint8_t* p = (const uint8_t*)str.c_str();
    buf.insert(buf.end(), p, p + str.length());
}

static std::vector<uint8_t> encodeCastMessage(const String& sourceId, const String& destinationId, const String& namespaceUri, const String& payloadUtf8) {
    std::vector<uint8_t> payload;
    // 1: protocol_version = 0 (tag = 8)
    payload.push_back(8);
    encodeVarint(0, payload);

    // 2: source_id (tag = 18)
    encodeString(18, sourceId, payload);

    // 3: destination_id (tag = 26)
    encodeString(26, destinationId, payload);

    // 4: namespace (tag = 34)
    encodeString(34, namespaceUri, payload);

    // 5: payload_type = 0 (tag = 40)
    payload.push_back(40);
    encodeVarint(0, payload);

    // 6: payload_utf8 (tag = 50)
    encodeString(50, payloadUtf8, payload);

    // Prefix with 4-byte big-endian length
    uint32_t len = payload.size();
    std::vector<uint8_t> frame;
    frame.reserve(4 + len);
    frame.push_back((len >> 24) & 0xFF);
    frame.push_back((len >> 16) & 0xFF);
    frame.push_back((len >> 8) & 0xFF);
    frame.push_back(len & 0xFF);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

static bool decodeVarint(const uint8_t* bytes, size_t len, size_t& idx, uint64_t& result) {
    result = 0;
    int shift = 0;
    while (idx < len) {
        uint8_t b = bytes[idx++];
        result |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

static bool decodeCastMessage(const uint8_t* bytes, size_t len, CastMessage& msg) {
    size_t idx = 0;
    while (idx < len) {
        uint64_t tagWire = 0;
        if (!decodeVarint(bytes, len, idx, tagWire)) return false;
        uint32_t fieldNum = tagWire >> 3;
        uint32_t wireType = tagWire & 7;

        if (wireType == 0) { // Varint
            uint64_t val = 0;
            if (!decodeVarint(bytes, len, idx, val)) return false;
            if (fieldNum == 1) msg.protocolVersion = (int)val;
        } else if (wireType == 2) { // Length-delimited string/bytes
            uint64_t strLen = 0;
            if (!decodeVarint(bytes, len, idx, strLen)) return false;
            if (idx + strLen > len) return false;
            String val = "";
            val.concat((const char*)&bytes[idx], (unsigned int)strLen);
            idx += strLen;

            if (fieldNum == 2) msg.sourceId = val;
            else if (fieldNum == 3) msg.destinationId = val;
            else if (fieldNum == 4) msg.namespaceUri = val;
            else if (fieldNum == 6) msg.payloadUtf8 = val;
        } else {
            return false;
        }
    }
    return true;
}

static bool readCastMessage(WiFiClientSecure& client, CastMessage& msg, uint32_t timeoutMs = 800) {
    uint32_t start = millis();
    while (client.connected() && client.available() < 4) {
        if (millis() - start > timeoutMs) return false;
        delay(5);
    }
    if (client.available() < 4) return false;

    uint8_t lenBuf[4];
    if (client.read(lenBuf, 4) != 4) return false;
    uint32_t len = ((uint32_t)lenBuf[0] << 24) | ((uint32_t)lenBuf[1] << 16) | ((uint32_t)lenBuf[2] << 8) | (uint32_t)lenBuf[3];

    if (len == 0 || len > 32768) return false;

    std::vector<uint8_t> payload(len);
    size_t bytesRead = 0;
    while (client.connected() && bytesRead < len) {
        int avail = client.available();
        if (avail > 0) {
            int toRead = min((int)(len - bytesRead), avail);
            int r = client.read(&payload[bytesRead], toRead);
            if (r > 0) bytesRead += r;
        } else {
            if (millis() - start > timeoutMs) return false;
            delay(5);
        }
    }
    if (bytesRead < len) return false;

    return decodeCastMessage(payload.data(), payload.size(), msg);
}

GoogleCastEngine::GoogleCastEngine() {
}

GoogleCastEngine::~GoogleCastEngine() {
    if (m_pollTaskHandle && !m_taskStopped.load(std::memory_order_acquire)) {
        m_taskRunning = false;
        m_isActive = false;
        xTaskNotifyGive(m_pollTaskHandle);
        for (int i = 0; i < 30 && !m_taskStopped.load(std::memory_order_acquire); ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

bool GoogleCastEngine::shutdownForDestruction() {
    if (m_pollTaskHandle && !m_taskStopped.load(std::memory_order_acquire)) {
        m_taskRunning = false;
        m_isActive = false;
        xTaskNotifyGive(m_pollTaskHandle);
        uint32_t start = millis();
        while (!m_taskStopped.load(std::memory_order_acquire) && (millis() - start < 300)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!m_taskStopped.load(std::memory_order_acquire)) {
            LOGE("GoogleCast", "CRITICAL: CastPoll task failed to stop within 300ms!");
            return false; // Quarantined: do not delete object, no UAF
        }
    }
    return true;
}

void GoogleCastEngine::applyConfig(const EngineConfig* config) {
    if (!config) return;
    String oldIp = m_deviceIp;
    String oldName = m_deviceName;
    m_deviceIp = config->getString("device_ip", "");
    m_deviceName = config->getString("device_name", "");
    m_showAlbumArt = config->getBool("show_album_art", true);
    m_showProgress = config->getBool("show_progress", true);
    m_showVolume = config->getBool("show_volume", true);
    m_showVisualizer = config->getBool("show_visualizer", true);

    if (oldIp != m_deviceIp || oldName != m_deviceName) {
        m_resolvedIp = m_deviceIp;
        m_lastMdnsQuery = 0;
        if (m_client.connected()) {
            m_client.stop();
        }
        m_lastTransportId = "";
    }
}

void GoogleCastEngine::pollTaskStatic(void* pvParameters) {
    auto* self = static_cast<GoogleCastEngine*>(pvParameters);
    if (self) {
        self->pollTaskLoop();
    }
}

void GoogleCastEngine::pollTaskLoop() {
    while (m_taskRunning) {
        if (m_isActive && WiFi.status() == WL_CONNECTED) {
            pollCastStatus();
            static uint32_t lastHwmLog = 0;
            uint32_t now = millis();
            if (now - lastHwmLog > 30000) {
                lastHwmLog = now;
                UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
                LOGD("GoogleCast", "CastPoll stack HWM: %u words (%u bytes free)",
                     (unsigned)hwm, (unsigned)(hwm * sizeof(StackType_t)));
            }
        } else {
            // When deactivated, CastPoll (the sole operational owner of m_client) closes the TLS session
            if (m_client.connected()) {
                LOGI("GoogleCast", "Engine deactivated: CastPoll closing TLS session to reclaim DRAM.");
                m_client.stop();
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
    }

    // Task exiting cooperatively: close client and signal completion
    if (m_client.connected()) {
        m_client.stop();
    }
    m_pollTaskHandle = nullptr;
    m_taskStopped.store(true, std::memory_order_release);
    vTaskDelete(NULL);
}

EngineError GoogleCastEngine::initialize(EngineContext* context, const EngineConfig* config) {
    applyConfig(config);
    m_hasPsram = (context && context->hasPsram()) || psramFound();

    if (!m_pollTaskHandle) {
        m_taskRunning = true;
        m_taskStopped.store(false, std::memory_order_release);
        BaseType_t ret = xTaskCreatePinnedToCore(
            pollTaskStatic,
            "CastPoll",
            8192,
            this,
            1,
            &m_pollTaskHandle,
            0
        );
        if (ret != pdPASS) {
            LOGE("GoogleCast", "Failed to create CastPoll worker task!");
            m_taskRunning = false;
        }
    }

    LOGI("GoogleCast", "Initialized. PSRAM Mode: %s, Device IP: %s, Device Name: '%s'",
         m_hasPsram ? "ENABLED" : "DISABLED", m_deviceIp.c_str(), m_deviceName.c_str());
    return EngineError::OK;
}

void GoogleCastEngine::activate() {
    m_marqueeOffset = 0;
    m_lastMarqueeTick = millis();
    m_lastAnimTick = millis();
    m_lastMdnsQuery = 0;
    m_isActive = true;
    if (m_pollTaskHandle) {
        xTaskNotifyGive(m_pollTaskHandle);
    }
}

void GoogleCastEngine::deactivate() {
    // Non-blocking state transition on Core 1: wake CastPoll on Core 0 to close m_client
    m_isActive = false;
    m_lastTransportId = "";
    if (m_pollTaskHandle) {
        xTaskNotifyGive(m_pollTaskHandle);
    }
}

void GoogleCastEngine::onConfigChanged(const EngineConfig* config) {
    applyConfig(config);
}

void GoogleCastEngine::discoverDevice() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!m_deviceIp.isEmpty()) {
        m_resolvedIp = m_deviceIp;
        m_resolvedPort = 8009;
        return;
    }

    LOGI("GoogleCast", "Running mDNS query for Google Cast devices (target name: '%s')...", m_deviceName.c_str());
    int count = MDNS.queryService("googlecast", "tcp");
    if (count <= 0) {
        LOGW("GoogleCast", "No Google Cast devices discovered via mDNS");
        return;
    }

    for (int i = 0; i < count; i++) {
        String fn = MDNS.txt(i, "fn");
        String host = MDNS.hostname(i);
        IPAddress ip = MDNS.IP(i);
        uint16_t port = MDNS.port(i);
        if (port == 0) port = 8009;

        LOGI("GoogleCast", "Found Cast device #%d: '%s' (%s) at %s:%u", i, fn.c_str(), host.c_str(), ip.toString().c_str(), port);

        if (m_deviceName.isEmpty() || fn.equalsIgnoreCase(m_deviceName) || host.equalsIgnoreCase(m_deviceName) || fn.indexOf(m_deviceName) != -1 || host.indexOf(m_deviceName) != -1) {
            m_resolvedIp = ip.toString();
            m_resolvedPort = port;
            LOGI("GoogleCast", "Matched target Cast device '%s' -> %s:%u", m_deviceName.c_str(), m_resolvedIp.c_str(), m_resolvedPort);
            return;
        }
    }

    if (count > 0 && m_resolvedIp.isEmpty()) {
        m_resolvedIp = MDNS.IP(0).toString();
        m_resolvedPort = MDNS.port(0) > 0 ? MDNS.port(0) : 8009;
        LOGI("GoogleCast", "Fallback to first discovered Cast device '%s' -> %s:%u", MDNS.txt(0, "fn").c_str(), m_resolvedIp.c_str(), m_resolvedPort);
    }
}

void GoogleCastEngine::pollCastStatus() {
    if (WiFi.status() != WL_CONNECTED) {
        if (m_client.connected()) {
            m_client.stop();
            m_lastTransportId = "";
        }
        return;
    }
    uint32_t now = millis();
    m_state.lastPollTime = now;

    // Discover device via mDNS only if we don't have a resolved IP yet
    if (m_resolvedIp.isEmpty()) {
        if (m_lastMdnsQueryMs == 0 || (now - m_lastMdnsQueryMs >= 300000UL)) {
            m_lastMdnsQueryMs = now;
            discoverDevice();
        }
        return;
    }

    // 1. Establish or recover persistent TLS connection if not connected
    if (!m_client.connected()) {
        if (m_nextReconnectMs == 0) {
            // Active session dropped; give LwIP and sockets 5s to settle before first reconnect attempt
            m_reconnectFailures = 1;
            m_nextReconnectMs = now + 5000;
            return;
        }
        if (now < m_nextReconnectMs) {
            return;
        }
        m_lastConnectAttemptMs = now;
        m_lastTransportId = "";

        const uint32_t preFree = NetworkBudget::freeInternal();
        const uint32_t preLargest = NetworkBudget::largestInternalBlock();
        const uint32_t preFreeDma = NetworkBudget::freeDmaInternal();
        const uint32_t preLargestDma = NetworkBudget::largestDmaInternalBlock();

        // Check internal DRAM and DMA admission before initiating TLS handshake
        if (!NetworkBudget::canStartTlsSession()) {
            LOGW("GoogleCast", "[TLS Telemetry] pre: free=%u, largest=%u, dma=%u, largestDma=%u | result=REJECTED_BY_BUDGET",
                 preFree, preLargest, preFreeDma, preLargestDma);
            m_nextReconnectMs = now + 5000;
            return;
        }

        // Serialize this (rare, reconnect-only) handshake against every other TLS user in
        // the system -- see HardwareHAL::begin() for why mbedTLS must stay internal-DRAM-only.
        NetworkBudget::ScopedTlsHandshakeLock tlsLock;
        if (!tlsLock) {
            LOGW("GoogleCast", "Skipping reconnect: another TLS handshake is in progress.");
            m_nextReconnectMs = now + 2000;
            return;
        }

        m_client.stop();
        m_client.setInsecure();

        LOGI("GoogleCast", "Opening persistent TLS connection to %s:%u...", m_resolvedIp.c_str(), m_resolvedPort);
        if (!m_client.connect(m_resolvedIp.c_str(), m_resolvedPort)) {
            const uint32_t postFree = NetworkBudget::freeInternal();
            const uint32_t postLargest = NetworkBudget::largestInternalBlock();
            const uint32_t postFreeDma = NetworkBudget::freeDmaInternal();
            const uint32_t postLargestDma = NetworkBudget::largestDmaInternalBlock();
            LOGW("GoogleCast", "[TLS Telemetry] pre: free=%u, largest=%u, dma=%u, largestDma=%u | result=HANDSHAKE_FAILED | post: free=%u, largest=%u, dma=%u, largestDma=%u",
                 preFree, preLargest, preFreeDma, preLargestDma,
                 postFree, postLargest, postFreeDma, postLargestDma);
            m_client.stop();

            m_reconnectFailures++;
            uint32_t backoffSec = 5;
            if (m_reconnectFailures == 2) backoffSec = 10;
            else if (m_reconnectFailures == 3) backoffSec = 20;
            else if (m_reconnectFailures >= 4) backoffSec = 60;
            m_nextReconnectMs = now + (backoffSec * 1000UL);

            static unsigned long lastConnectWarn = 0;
            if (now - lastConnectWarn > 10000) {
                lastConnectWarn = now;
                LOGW("GoogleCast", "Failed to connect to Cast device %s:%u (attempt #%u, backoff %us).",
                     m_resolvedIp.c_str(), m_resolvedPort, m_reconnectFailures, backoffSec);
            }
            // If cached endpoint fails repeatedly (>= 5 failures) and cooldown expired (>= 300s) and user hasn't hardcoded an IP
            if (m_deviceIp.isEmpty() && m_reconnectFailures >= 5 && (now - m_lastMdnsQueryMs >= 300000UL)) {
                m_lastMdnsQueryMs = now;
                LOGI("GoogleCast", "Cached endpoint failed repeatedly (%u times); scheduling mDNS refresh.", m_reconnectFailures);
                m_resolvedIp = "";
            }
            return;
        }

        m_reconnectFailures = 0;
        m_nextReconnectMs = 0;

        const uint32_t postFree = NetworkBudget::freeInternal();
        const uint32_t postLargest = NetworkBudget::largestInternalBlock();
        const uint32_t postFreeDma = NetworkBudget::freeDmaInternal();
        const uint32_t postLargestDma = NetworkBudget::largestDmaInternalBlock();
        LOGI("GoogleCast", "[TLS Telemetry] pre: free=%u, largest=%u, dma=%u, largestDma=%u | result=SUCCESS | post: free=%u, largest=%u, dma=%u, largestDma=%u",
             preFree, preLargest, preFreeDma, preLargestDma,
             postFree, postLargest, postFreeDma, postLargestDma);

        m_client.setTimeout(1200);

        LOGI("GoogleCast", "Connected to Cast device %s:%u (persistent session active).", m_resolvedIp.c_str(), m_resolvedPort);

        // Send initial CONNECT to receiver-0
        auto connMsg = encodeCastMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.connection", "{\"type\":\"CONNECT\"}");
        m_client.write(connMsg.data(), connMsg.size());

        m_lastPingMs = now;
        m_lastStatusMs = 0;
    }

    // 2. Proactive heartbeat PING every 5000 ms (mandated by CastV2 contract to prevent speaker drop)
    if (now - m_lastPingMs >= 5000) {
        m_lastPingMs = now;
        auto ping = encodeCastMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.heartbeat", "{\"type\":\"PING\"}");
        m_client.write(ping.data(), ping.size());
    }

    // 3. Consume any pending unsolicited frames or heartbeat PINGs
    while (m_client.available()) {
        CastMessage resp;
        if (!readCastMessage(m_client, resp, 100)) break;
        if (resp.namespaceUri == "urn:x-cast:com.google.cast.tp.heartbeat" && resp.payloadUtf8.indexOf("PING") != -1) {
            auto pong = encodeCastMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.heartbeat", "{\"type\":\"PONG\"}");
            m_client.write(pong.data(), pong.size());
        }
    }

    // 4. Pace status queries: poll every 5000 ms or immediately upon fresh connect
    bool shouldQueryStatus = (m_lastStatusMs == 0 || (now - m_lastStatusMs >= 5000));
    if (!shouldQueryStatus && !m_lastTransportId.isEmpty()) {
        m_state.localTimestampMs = millis();
        return;
    }
    m_lastStatusMs = now;

    // Send GET_STATUS to receiver-0
    m_requestId++;
    String getStatus = "{\"type\":\"GET_STATUS\",\"requestId\":" + String(m_requestId) + "}";
    auto reqMsg = encodeCastMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.receiver", getStatus);
    m_client.write(reqMsg.data(), reqMsg.size());

    String transportId = "";
    String appName = "";
    float volumeLevel = m_state.volumeLevel;

    // Read receiver status responses (up to 4 frames)
    for (int i = 0; i < 4; i++) {
        CastMessage resp;
        if (!readCastMessage(m_client, resp, 400)) break;

        if (resp.namespaceUri == "urn:x-cast:com.google.cast.tp.heartbeat" && resp.payloadUtf8.indexOf("PING") != -1) {
            auto pong = encodeCastMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.heartbeat", "{\"type\":\"PONG\"}");
            m_client.write(pong.data(), pong.size());
        } else if (resp.namespaceUri == "urn:x-cast:com.google.cast.receiver") {
            SpiRamJsonDocument doc(4096);
            if (deserializeJson(doc, resp.payloadUtf8) == DeserializationError::Ok) {
                if (doc["status"]["volume"].is<JsonObject>()) {
                    volumeLevel = doc["status"]["volume"]["level"] | volumeLevel;
                }
                if (doc["status"]["applications"].is<JsonArray>()) {
                    for (JsonObject app : doc["status"]["applications"].as<JsonArray>()) {
                        bool isIdle = app["isIdleScreen"] | false;
                        const char* appId = app["appId"] | "";
                        const char* tId = app["transportId"] | "";
                        if (!isIdle && strcmp(appId, "E8C28D3C") != 0 && strlen(tId) > 0) {
                            transportId = String(tId);
                            appName = app["displayName"] | "";
                            break;
                        }
                    }
                }
            }
            break;
        }
    }

    if (!transportId.isEmpty()) {
        // Connect to transportId if new
        if (transportId != m_lastTransportId) {
            auto tConn = encodeCastMessage("sender-0", transportId, "urn:x-cast:com.google.cast.tp.connection", "{\"type\":\"CONNECT\"}");
            m_client.write(tConn.data(), tConn.size());
            m_lastTransportId = transportId;
        }

        // Send GET_STATUS to transportId on media namespace
        m_requestId++;
        String getMediaStatus = "{\"type\":\"GET_STATUS\",\"requestId\":" + String(m_requestId) + "}";
        auto mReq = encodeCastMessage("sender-0", transportId, "urn:x-cast:com.google.cast.media", getMediaStatus);
        m_client.write(mReq.data(), mReq.size());

        for (int i = 0; i < 6; i++) {
            CastMessage resp;
            if (!readCastMessage(m_client, resp, 400)) break;

            if (resp.namespaceUri == "urn:x-cast:com.google.cast.tp.heartbeat" && resp.payloadUtf8.indexOf("PING") != -1) {
                auto pong = encodeCastMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.heartbeat", "{\"type\":\"PONG\"}");
                m_client.write(pong.data(), pong.size());
            } else if (resp.namespaceUri == "urn:x-cast:com.google.cast.media") {
                SpiRamJsonDocument doc(4096);
                if (deserializeJson(doc, resp.payloadUtf8) == DeserializationError::Ok) {
                    if (doc["status"].is<JsonArray>() && doc["status"].size() > 0) {
                        JsonObject mediaStat = doc["status"][0];
                        String playerState = mediaStat["playerState"] | "";
                        bool isPlaying = (playerState == "PLAYING" || playerState == "BUFFERING");

                        m_state.isActive = true;
                        m_state.isPlaying = isPlaying;
                        m_state.currentTimeSec = mediaStat["currentTime"] | 0.0f;
                        m_state.volumeLevel = volumeLevel;
                        strncpy(m_state.appName, appName.c_str(), sizeof(m_state.appName) - 1);
                        m_state.appName[sizeof(m_state.appName) - 1] = '\0';

                        if (mediaStat["media"].is<JsonObject>()) {
                            JsonObject media = mediaStat["media"];
                            m_state.durationSec = media["duration"] | 0.0f;
                            String imgUrl = "";

                            if (media["metadata"].is<JsonObject>()) {
                                JsonObject meta = media["metadata"];
                                const char* t = meta["title"] | (meta["songName"] | (media["customData"]["title"] | ""));
                                const char* a = meta["artist"] | (meta["subtitle"] | (meta["artistName"] | (meta["albumArtist"] | "")));
                                const char* al = meta["albumName"] | (meta["albumTitle"] | "");

                                strncpy(m_state.title, t, sizeof(m_state.title) - 1);
                                m_state.title[sizeof(m_state.title) - 1] = '\0';
                                strncpy(m_state.artist, a, sizeof(m_state.artist) - 1);
                                m_state.artist[sizeof(m_state.artist) - 1] = '\0';
                                strncpy(m_state.album, al, sizeof(m_state.album) - 1);
                                m_state.album[sizeof(m_state.album) - 1] = '\0';

                                if (meta["images"].is<JsonArray>() && meta["images"].size() > 0) {
                                    imgUrl = meta["images"][0]["url"] | (meta["images"][0]["href"] | "");
                                }
                                if (imgUrl.isEmpty()) {
                                    imgUrl = meta["imageUrl"] | (meta["image"] | (meta["albumArtUrl"] | (meta["posterUrl"] | "")));
                                }
                            }
                            if (imgUrl.isEmpty() && media["images"].is<JsonArray>() && media["images"].size() > 0) {
                                imgUrl = media["images"][0]["url"] | (media["images"][0]["href"] | "");
                            }
                            if (imgUrl.isEmpty() && media["customData"].is<JsonObject>()) {
                                imgUrl = media["customData"]["imageUrl"] | (media["customData"]["thumbnail"] | (media["customData"]["poster"] | ""));
                            }
                            strncpy(m_state.imageUrl, imgUrl.c_str(), sizeof(m_state.imageUrl) - 1);
                            m_state.imageUrl[sizeof(m_state.imageUrl) - 1] = '\0';
                        }
                    }
                }
                break;
            }
        }

        if (!m_state.isActive && !appName.isEmpty()) {
            m_state.isActive = true;
            strncpy(m_state.appName, appName.c_str(), sizeof(m_state.appName) - 1);
            m_state.appName[sizeof(m_state.appName) - 1] = '\0';
            if (m_state.title[0] == '\0') {
                strncpy(m_state.title, appName.c_str(), sizeof(m_state.title) - 1);
                m_state.title[sizeof(m_state.title) - 1] = '\0';
            }
        }
    } else {
        m_state.isActive = false;
        m_state.isPlaying = false;
        m_state.title[0] = '\0';
        m_state.artist[0] = '\0';
        m_state.imageUrl[0] = '\0';
        m_lastTransportId = "";
    }

    // If socket was disconnected by remote peer, clean up
    if (!m_client.connected()) {
        m_client.stop();
        m_lastTransportId = "";
    }

    // Asynchronously load artwork on Core 0
    if (m_hasPsram && m_showAlbumArt && m_state.imageUrl[0] != '\0') {
        if (strcmp(m_state.imageUrl, m_loadedImageUrl.c_str()) != 0) {
            m_loadedImageUrl = m_state.imageUrl;
            int imgSize = 52;
            LOGI("GoogleCast", "Fetching album cover: %s", m_loadedImageUrl.c_str());
            m_artworkId = artworkService.loadArtwork(m_loadedImageUrl, imgSize, imgSize);
            if (!m_artworkId.isEmpty()) {
                LOGI("GoogleCast", "Album cover loaded successfully (ID: %s)", m_artworkId.c_str());
            } else {
                LOGW("GoogleCast", "Failed to load album cover from: %s", m_loadedImageUrl.c_str());
            }
        }
    } else if (m_state.imageUrl[0] == '\0' || !m_hasPsram || !m_showAlbumArt) {
        m_artworkId = "";
        m_loadedImageUrl = "";
    }

    m_state.localTimestampMs = millis();

    // Lock-free generational double-buffer publish to Core 1 (Invariant 1)
    uint8_t nextSlot = 1 - m_publishedSlot.load(std::memory_order_relaxed);
    m_slots[nextSlot] = m_state;
    m_publishedSlot.store(nextSlot, std::memory_order_release);
}

void GoogleCastEngine::update(EngineContext* context) {
    uint32_t now = millis();
    const auto& st = m_slots[m_publishedSlot.load(std::memory_order_acquire)];

    // Marquee scrolling tick (~40ms per pixel)
    if (now - m_lastMarqueeTick >= 40) {
        m_marqueeOffset++;
        m_lastMarqueeTick = now;
    }

    // Visualizer animation tick (~80ms)
    if (st.isPlaying && (now - m_lastAnimTick >= 80)) {
        m_animFrame = (m_animFrame + 1) % 100;
        m_lastAnimTick = now;
    }

    // Log newly playing track without spam
    if (st.isActive && st.title[0] != '\0') {
        if (m_lastLoggedTrack != st.title) {
            m_lastLoggedTrack = st.title;
            LOGI("GoogleCast", "📻 Now Streaming -> \"%s\" by \"%s\" (App: %s)",
                 st.title,
                 st.artist[0] == '\0' ? "Unknown Artist" : st.artist,
                 st.appName[0] == '\0' ? "Cast" : st.appName);
        }
    } else if (!m_lastLoggedTrack.isEmpty()) {
        m_lastLoggedTrack = "";
    }
}

#include <glcdfont.c>

static void drawClippedString(Adafruit_GFX* display, const char* text, int x, int y, int clipMinX, int clipMaxX, uint16_t color) {
    if (!display || !text || text[0] == '\0' || clipMinX >= clipMaxX) return;
    int curX = x;
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c >= 128) c = '?'; // Clamp non-ASCII to prevent negative/out-of-bounds font index
        if (curX >= clipMinX && curX + 6 <= clipMaxX) {
            display->drawChar(curX, y, (char)c, color, 0, 1);
        } else if (curX + 6 > clipMinX && curX < clipMaxX) {
            for (int col = 0; col < 5; col++) {
                int px = curX + col;
                if (px >= clipMinX && px < clipMaxX) {
                    uint8_t line = pgm_read_byte(&font[c * 5 + col]);
                    for (int row = 0; row < 8; row++) {
                        if (line & 1) {
                            display->drawPixel(px, y + row, color);
                        }
                        line >>= 1;
                    }
                }
            }
        }
        curX += 6;
    }
}

static void renderMarquee(Adafruit_GFX* display, const char* text, int y, int clipMinX, int clipMaxX, int availW, int offset, uint16_t color) {
    if (!display || !text || text[0] == '\0' || clipMinX >= clipMaxX || availW <= 0) return;
    int textW = (int)strlen(text) * 6;
    if (textW <= availW) {
        drawClippedString(display, text, clipMinX, y, clipMinX, clipMaxX, color);
    } else {
        int gap = 20; // 20px seamless spacing between loop repetitions (matches RPi)
        int totalW = textW + gap;
        int dx = (offset % totalW + totalW) % totalW;

        // Draw primary text instance
        int drawX1 = clipMinX - dx;
        drawClippedString(display, text, drawX1, y, clipMinX, clipMaxX, color);

        // Draw trailing secondary instance for circular looping
        int drawX2 = drawX1 + totalW;
        if (drawX2 < clipMaxX) {
            drawClippedString(display, text, drawX2, y, clipMinX, clipMaxX, color);
        }
    }
}

void GoogleCastEngine::render(EngineContext* context) {
    if (!context) return;
    auto* display = context->getSurface();
    if (!display) return;

    int w = display->width();
    int h = display->height();

    // Lock-free generational read from Core 1 (Invariant 1)
    const auto& st = m_slots[m_publishedSlot.load(std::memory_order_acquire)];

    if (!st.isActive || st.title[0] == '\0') {
        // Idle screen
        const char* title = "Google Cast";
        const char* subtitle = (m_deviceName.length() > 0) ? m_deviceName.c_str() : "Ready to stream";

        int titleW = (int)strlen(title) * 6;
        int yIdleTitle = (h >= 64) ? ((h / 2) - 10) : 4;
        int yIdleSub = (h >= 64) ? ((h / 2) + 4) : 16;

        if (titleW <= w - 4) {
            int xTitle = (w - titleW) / 2;
            drawClippedString(display, title, xTitle, yIdleTitle, 2, w - 2, display->color565(66, 133, 244));
        } else {
            renderMarquee(display, title, yIdleTitle, 2, w - 2, w - 4, m_marqueeOffset, display->color565(66, 133, 244));
        }

        int subW = (int)strlen(subtitle) * 6;
        int clipMinX = 2;
        int clipMaxX = w - 2;
        int availW = clipMaxX - clipMinX;

        if (subW <= availW) {
            int xSub = (w - subW) / 2;
            drawClippedString(display, subtitle, xSub, yIdleSub, clipMinX, clipMaxX, display->color565(160, 160, 175));
        } else {
            renderMarquee(display, subtitle, yIdleSub, clipMinX, clipMaxX, availW, m_marqueeOffset / 2, display->color565(160, 160, 175));
        }
        return;
    }

    int textX = 2;
    bool hasArt = false;
    if (m_hasPsram && m_showAlbumArt && st.imageUrl[0] != '\0') {
        ArtworkSnapshot snap = artworkService.getSnapshot();
        if (snap.bitmap && snap.width > 0 && snap.height > 0) {
            hasArt = true;
            int imgSize = (h >= 64) ? 52 : 24;
            int imgX = 1;
            int imgY = (h >= 64) ? ((h - 4 - imgSize) / 2) : max(1, (h - 3 - imgSize) / 2);

            display->drawRect(imgX - 1, imgY - 1, imgSize + 2, imgSize + 2, display->color565(40, 40, 50));
            int drawW = min(imgSize, snap.width);
            int drawH = min(imgSize, snap.height);
            display->drawRGBBitmap(imgX, imgY, snap.bitmap, drawW, drawH);
            textX = imgX + imgSize + 4;
        }
    }

    bool isCompact = (w <= 64);
    int rightReserved = 2;
    if (m_showVisualizer && st.isPlaying) {
        rightReserved = (isCompact && hasArt) ? 8 : 15;
    } else if (m_showVolume) {
        rightReserved = 24;
    }

    // Strict viewport for text
    int clipMinX = textX;
    int clipMaxX = w - rightReserved;
    if (clipMaxX <= clipMinX) {
        clipMaxX = w - 2; // Fallback so text is never crushed
    }
    int availW = max(16, clipMaxX - clipMinX);

    int yTitle = (h >= 64) ? 8 : 3;
    int yArtist = (h >= 64) ? 22 : 13;

    // 2. Title (Marquee)
    renderMarquee(display, st.title, yTitle, clipMinX, clipMaxX, availW, m_marqueeOffset, display->color565(255, 255, 255));

    // 3. Artist / Subtitle (Marquee)
    const char* artistStr = (st.artist[0] != '\0') ? st.artist : ((st.appName[0] != '\0') ? st.appName : "Google Nest");
    renderMarquee(display, artistStr, yArtist, clipMinX, clipMaxX, availW, m_marqueeOffset / 2, display->color565(66, 180, 255));

    // 4. Equalizer Visualizer on the right
    if (m_showVisualizer && st.isPlaying) {
        int eqBaseY = (h >= 64) ? 44 : 21;

        if (isCompact && hasArt) {
            // 3 compact bars (2px wide each, 1px spacing)
            int eqX = w - 7;
            int barHeights[3] = {
                (int)((m_animFrame * 3) % 7 + 2),
                (int)((m_animFrame * 5) % 9 + 3),
                (int)((m_animFrame * 2) % 6 + 2)
            };

            for (int i = 0; i < 3; i++) {
                int bx = eqX + (i * 2);
                int bh = barHeights[i];
                for (int by = 0; by < bh; by++) {
                    int py = eqBaseY - by;
                    if (py >= 0) {
                        uint16_t color = (by > 6) ? display->color565(255, 60, 60) : (by > 3) ? display->color565(255, 200, 0) : display->color565(0, 255, 120);
                        display->drawPixel(bx, py, color);
                    }
                }
            }
        } else {
            // 4 wide bars (2px wide + 1px spacing)
            int eqX = w - 13;
            int barHeights[4] = {
                (int)((m_animFrame * 3) % 8 + 2),
                (int)((m_animFrame * 5) % 11 + 3),
                (int)((m_animFrame * 2) % 9 + 2),
                (int)((m_animFrame * 7) % 7 + 2)
            };

            for (int i = 0; i < 4; i++) {
                int bx = eqX + (i * 3);
                int bh = barHeights[i];
                for (int by = 0; by < bh; by++) {
                    int py = eqBaseY - by;
                    if (py >= 0) {
                        uint16_t color = (by > 7) ? display->color565(255, 50, 50) : (by > 4) ? display->color565(255, 190, 0) : display->color565(0, 240, 110);
                        display->drawPixel(bx, py, color);
                        display->drawPixel(bx + 1, py, color);
                    }
                }
            }
        }
    } else if (m_showVolume) {
        int volPct = (int)(st.volumeLevel * 100.0f);
        char vBuf[8];
        snprintf(vBuf, sizeof(vBuf), "%d%%", volPct);
        int vLen = strlen(vBuf);
        display->setTextColor(display->color565(180, 180, 180));
        display->setCursor(w - (vLen * 6) - 1, yTitle);
        display->print(vBuf);
    }

    // 5. Progress Bar (Bottom 2 pixels) with smooth time interpolation
    if (m_showProgress && st.durationSec > 0.0f) {
        float curTime = st.currentTimeSec;
        if (st.isPlaying && st.localTimestampMs > 0) {
            curTime += (millis() - st.localTimestampMs) / 1000.0f;
        }
        float progress = constrain(curTime / st.durationSec, 0.0f, 1.0f);
        int barW = (int)((w - 2) * progress);

        display->drawFastHLine(1, h - 2, w - 2, display->color565(35, 35, 40));
        display->drawFastHLine(1, h - 1, w - 2, display->color565(20, 20, 25));

        if (barW > 0) {
            display->drawFastHLine(1, h - 2, barW, display->color565(66, 133, 244));
            display->drawFastHLine(1, h - 1, barW, display->color565(33, 90, 180));
        }
    }
}

EngineDescriptor GoogleCastDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"google_cast", "Google Cast", "media", "3.0.0"};
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.realtime = true;

    desc.requirements.needsNetwork = true;
    desc.requirements.needsPsram = false; // Adaptive: works on both ESP32 classic and S3!

    desc.schema.fields = {
        ConfigField("device_ip", ConfigType::STRING, "Device IP (Optional)", "Static IP of your Google Home / Nest Audio. Leave empty for automatic discovery.", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("device_name", ConfigType::STRING, "Device Name Filter", "Filter by friendly name when discovering Google Nest devices on LAN.", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("show_album_art", ConfigType::BOOLEAN, "Show Album Cover", "Download and display album cover art (requires PSRAM).", "true", false, "", "", "", "", "", false, "psram=true", ValidationPolicy::FallbackDefault),
        ConfigField("show_progress", ConfigType::BOOLEAN, "Show Progress Bar", "Render track playback progress bar at the bottom.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_visualizer", ConfigType::BOOLEAN, "Animated Equalizer", "Display dancing equalizer frequency bars while music is playing.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_volume", ConfigType::BOOLEAN, "Show Volume Indicator", "Display Google Nest current volume percentage.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault)
    };

    desc.factory = []() {
        return std::unique_ptr<IEngine>(new GoogleCastEngine());
    };

    return desc;
}
