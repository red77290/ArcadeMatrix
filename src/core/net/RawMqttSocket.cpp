#include "RawMqttSocket.h"

#include <cstdio>
#include <cstring>

#if defined(ESP32) && !defined(UNIT_TEST)
#include <Arduino.h>
#include <WiFi.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include "../../engines/WebUiSyncEngine.h"
#include "../../hal/HardwareHAL.h"
#endif

namespace net {

size_t encodeMqttRemainingLength(uint8_t* out, size_t remLen) {
    if (!out) return 0U;
    size_t idx = 0U;
    do {
        uint8_t enc = static_cast<uint8_t>(remLen % 128U);
        remLen /= 128U;
        if (remLen > 0U) {
            enc |= 0x80U;
        }
        out[idx++] = enc;
    } while (remLen > 0U && idx < 4U);
    return idx;
}

RawMqttSocket::~RawMqttSocket() {
    closeAbort();
}

#if defined(ESP32) && !defined(UNIT_TEST)

namespace {

inline size_t rawSockFree8BitHeap() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

inline size_t rawSockLargest8BitBlock() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

} // namespace

void RawMqttSocket::closeAbort() {
    if (fd >= 0) {
        struct linger lin = {1, 0};
        lwip_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        lwip_close(fd);
        fd = -1;
    }
    rxOff = 0U;
    rxEnd = 0U;
    peerClosed = true;
}

bool RawMqttSocket::connectHost(
    const char* host,
    uint16_t port,
    const std::atomic<bool>& wantActive,
    uint32_t timeoutMs
) {
    closeAbort();
    peerClosed = false;
    if (!host || host[0] == '\0' || port == 0U) {
        peerClosed = true;
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_aton(host, &addr.sin_addr) == 0) {
        IPAddress resolvedIp;
        if (rawSockFree8BitHeap() < 5120U || !WiFi.hostByName(host, resolvedIp)) {
            peerClosed = true;
            return false;
        }
        addr.sin_addr.s_addr = static_cast<uint32_t>(resolvedIp);
    }

    fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        peerClosed = true;
        return false;
    }

    int nodelay = 1;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    int flags = lwip_fcntl(fd, F_GETFL, 0);
    lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = lwip_connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        closeAbort();
        return false;
    }
    if (rc != 0) {
        uint32_t t0 = millis();
        bool ok = false;
        while ((millis() - t0) < timeoutMs && wantActive.load(std::memory_order_acquire)) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv = {0, 50000};
            int sel = lwip_select(fd + 1, nullptr, &wfds, nullptr, &tv);
            if (sel > 0 && FD_ISSET(fd, &wfds)) {
                int soErr = -1;
                socklen_t errLen = sizeof(soErr);
                if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &errLen) == 0 && soErr == 0) {
                    ok = true;
                }
                break;
            }
            if (sel < 0 && errno != EINTR) {
                break;
            }
        }
        if (!ok) {
            closeAbort();
            return false;
        }
    }

    lwip_fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval rcvTv = {0, 40000};
    struct timeval sndTv = {0, 500000};
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvTv, sizeof(rcvTv));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sndTv, sizeof(sndTv));
    return true;
}

size_t RawMqttSocket::available() {
    size_t buffered = (rxEnd > rxOff) ? (rxEnd - rxOff) : 0U;
    if (buffered >= 4U || fd < 0 || peerClosed) {
        return buffered;
    }
    if (buffered > 0U && rxOff > 0U) {
        memmove(rxBuf, rxBuf + rxOff, buffered);
        rxOff = 0U;
        rxEnd = buffered;
    } else if (buffered == 0U) {
        rxOff = 0U;
        rxEnd = 0U;
    }
    if (rxEnd < sizeof(rxBuf)) {
        int r = lwip_recv(fd, rxBuf + rxEnd, sizeof(rxBuf) - rxEnd, MSG_DONTWAIT);
        if (r > 0) {
            rxEnd += static_cast<size_t>(r);
        } else if (r == 0) {
            peerClosed = true;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            peerClosed = true;
        }
    }
    return (rxEnd > rxOff) ? (rxEnd - rxOff) : 0U;
}

int RawMqttSocket::readByte() {
    if (rxOff >= rxEnd) {
        (void)available();
        if (rxOff >= rxEnd) {
            return -1;
        }
    }
    return static_cast<int>(rxBuf[rxOff++]);
}

bool RawMqttSocket::readExact(
    uint8_t* dst,
    size_t len,
    const std::atomic<bool>& wantActive,
    uint32_t timeoutMs
) {
    size_t got = 0U;
    if (rxOff < rxEnd && len > 0U) {
        size_t avail = rxEnd - rxOff;
        size_t take = (avail < len) ? avail : len;
        memcpy(dst, rxBuf + rxOff, take);
        rxOff += take;
        got += take;
    }
    uint32_t lastProg = millis();
    while (got < len) {
        if (!wantActive.load(std::memory_order_acquire) || fd < 0 || peerClosed) {
            return false;
        }
        const size_t rem = len - got;
        if (rem >= sizeof(rxBuf)) {
            int r = lwip_recv(fd, dst + got, rem, 0);
            if (r > 0) {
                got += static_cast<size_t>(r);
                lastProg = millis();
                continue;
            } else if (r == 0) {
                peerClosed = true;
                return false;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                peerClosed = true;
                return false;
            }
        } else {
            rxOff = 0U;
            rxEnd = 0U;
            int r = lwip_recv(fd, rxBuf, sizeof(rxBuf), 0);
            if (r > 0) {
                rxEnd = static_cast<size_t>(r);
                size_t take = (rxEnd < rem) ? rxEnd : rem;
                memcpy(dst + got, rxBuf, take);
                rxOff = take;
                got += take;
                lastProg = millis();
                continue;
            } else if (r == 0) {
                peerClosed = true;
                return false;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                peerClosed = true;
                return false;
            }
        }
        if ((millis() - lastProg) >= timeoutMs) {
            return false;
        }
    }
    return true;
}

bool RawMqttSocket::drain(
    size_t len,
    const std::atomic<bool>& wantActive,
    uint32_t timeoutMs
) {
    uint8_t sink[64];
    size_t rem = len;
    while (rem > 0U) {
        size_t chunk = (rem < sizeof(sink)) ? rem : sizeof(sink);
        if (!readExact(sink, chunk, wantActive, timeoutMs)) {
            return false;
        }
        rem -= chunk;
    }
    return true;
}

bool RawMqttSocket::writeAll(
    const uint8_t* data,
    size_t len,
    const std::atomic<bool>& wantActive,
    uint32_t timeoutMs
) {
    if (!data || len == 0U) return true;
    size_t sent = 0U;
    uint32_t lastProg = millis();
    while (sent < len) {
        if (!wantActive.load(std::memory_order_acquire) || fd < 0 || peerClosed) {
            return false;
        }
        int n = lwip_send(fd, data + sent, len - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            lastProg = millis();
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if ((millis() - lastProg) >= timeoutMs) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(4));
        } else {
            peerClosed = true;
            return false;
        }
    }
    return true;
}

bool sendMqttPublishPacket(
    RawMqttSocket& cli,
    const char* topic,
    const char* payload,
    bool retain,
    const std::atomic<bool>& wantActive,
    char* ioScratch,
    size_t scratchCap
) {
    if (!cli.connected() || !topic || !payload) return false;
    uint16_t tLen = static_cast<uint16_t>(strlen(topic));
    size_t pLen = strlen(payload);
    size_t remLen = 2U + tLen + pLen;
    uint8_t hdr[8];
    hdr[0] = static_cast<uint8_t>(0x30U | (retain ? 0x01U : 0x00U));
    size_t rlBytes = encodeMqttRemainingLength(hdr + 1, remLen);
    size_t hdrTotal = 1U + rlBytes + 2U + tLen;
    size_t totalLen = hdrTotal + pLen;

    // Coalesce MQTT fixed header + topic + payload into a single TCP segment (1 LwIP pbuf)
    if (ioScratch != nullptr && totalLen <= scratchCap) {
        if (pLen > 0U) {
            memmove(ioScratch + hdrTotal, payload, pLen);
        }
        memcpy(ioScratch, hdr, 1U + rlBytes);
        ioScratch[1U + rlBytes] = static_cast<uint8_t>((tLen >> 8) & 0xFFU);
        ioScratch[1U + rlBytes + 1U] = static_cast<uint8_t>(tLen & 0xFFU);
        memcpy(ioScratch + 1U + rlBytes + 2U, topic, tLen);
        return cli.writeAll(reinterpret_cast<const uint8_t*>(ioScratch), totalLen, wantActive);
    }

    uint8_t hdrBuf[136];
    if (hdrTotal <= sizeof(hdrBuf)) {
        memcpy(hdrBuf, hdr, 1U + rlBytes);
        hdrBuf[1U + rlBytes] = static_cast<uint8_t>((tLen >> 8) & 0xFFU);
        hdrBuf[1U + rlBytes + 1U] = static_cast<uint8_t>(tLen & 0xFFU);
        memcpy(hdrBuf + 1U + rlBytes + 2U, topic, tLen);
        if (!cli.writeAll(hdrBuf, hdrTotal, wantActive)) return false;
    } else {
        uint8_t tLenBe[2] = {static_cast<uint8_t>((tLen >> 8) & 0xFFU), static_cast<uint8_t>(tLen & 0xFFU)};
        if (!cli.writeAll(hdr, 1U + rlBytes, wantActive)) return false;
        if (!cli.writeAll(tLenBe, 2U, wantActive)) return false;
        if (!cli.writeAll(reinterpret_cast<const uint8_t*>(topic), tLen, wantActive)) return false;
    }
    return (pLen == 0U) || cli.writeAll(reinterpret_cast<const uint8_t*>(payload), pLen, wantActive);
}

bool sendMqttSubscribePacket(
    RawMqttSocket& cli,
    uint16_t pktId,
    const char* topic,
    const std::atomic<bool>& wantActive
) {
    if (!cli.connected() || !topic) return false;
    uint16_t tLen = static_cast<uint16_t>(strlen(topic));
    size_t remLen = 2U + 2U + tLen + 1U;
    uint8_t pkt[144];
    pkt[0] = 0x82U;
    size_t rlBytes = encodeMqttRemainingLength(pkt + 1, remLen);
    size_t totalLen = 1U + rlBytes + remLen;
    if (totalLen > sizeof(pkt)) return false;
    size_t off = 1U + rlBytes;
    pkt[off++] = static_cast<uint8_t>((pktId >> 8) & 0xFFU);
    pkt[off++] = static_cast<uint8_t>(pktId & 0xFFU);
    pkt[off++] = static_cast<uint8_t>((tLen >> 8) & 0xFFU);
    pkt[off++] = static_cast<uint8_t>(tLen & 0xFFU);
    memcpy(pkt + off, topic, tLen);
    off += tLen;
    pkt[off++] = 0U; // QoS 0
    return cli.writeAll(pkt, off, wantActive);
}

bool sendMqttUnsubscribePacket(
    RawMqttSocket& cli,
    uint16_t pktId,
    const char* topic,
    const std::atomic<bool>& wantActive
) {
    if (!cli.connected() || !topic) return false;
    uint16_t tLen = static_cast<uint16_t>(strlen(topic));
    size_t remLen = 2U + 2U + tLen;
    uint8_t pkt[144];
    pkt[0] = 0xA2U; // MQTT 3.1.1 §3.10 UNSUBSCRIBE (reserved flags 0010 mandatory)
    size_t rlBytes = encodeMqttRemainingLength(pkt + 1, remLen);
    size_t totalLen = 1U + rlBytes + remLen;
    if (totalLen > sizeof(pkt)) return false;
    size_t off = 1U + rlBytes;
    pkt[off++] = static_cast<uint8_t>((pktId >> 8) & 0xFFU);
    pkt[off++] = static_cast<uint8_t>(pktId & 0xFFU);
    pkt[off++] = static_cast<uint8_t>((tLen >> 8) & 0xFFU);
    pkt[off++] = static_cast<uint8_t>(tLen & 0xFFU);
    memcpy(pkt + off, topic, tLen);
    off += tLen;
    return cli.writeAll(pkt, off, wantActive);
}

bool sendMqttConnectPacket(
    RawMqttSocket& cli,
    const char* clientId,
    const std::atomic<bool>& wantActive
) {
    if (!clientId) return false;
    uint16_t cidLen = static_cast<uint16_t>(strlen(clientId));
    size_t remLen = 10U + 2U + cidLen;
    uint8_t pkt[128];
    pkt[0] = 0x10U;
    size_t rlBytes = encodeMqttRemainingLength(pkt + 1, remLen);
    size_t totalLen = 1U + rlBytes + remLen;
    if (totalLen > sizeof(pkt)) return false;
    const uint8_t varHdr[10] = {0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02, 0x00, 0x3C};
    size_t off = 1U + rlBytes;
    memcpy(pkt + off, varHdr, sizeof(varHdr));
    off += sizeof(varHdr);
    pkt[off++] = static_cast<uint8_t>((cidLen >> 8) & 0xFFU);
    pkt[off++] = static_cast<uint8_t>(cidLen & 0xFFU);
    memcpy(pkt + off, clientId, cidLen);
    off += cidLen;
    return cli.writeAll(pkt, off, wantActive);
}

void sendMqttDisconnectAndStop(
    RawMqttSocket& cli,
    const std::atomic<bool>& wantActive
) {
    if (cli.connected()) {
        const uint8_t discPkt[2] = {0xE0U, 0x00U};
        if (cli.writeAll(discPkt, sizeof(discPkt), wantActive, 150U) &&
            wantActive.load(std::memory_order_acquire)) {
            uint8_t sink[16];
            uint32_t t0 = millis();
            while ((millis() - t0) < 20U && cli.fd >= 0 && !cli.peerClosed) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(cli.fd, &rfds);
                struct timeval tv = {0, 5000};
                int sel = lwip_select(cli.fd + 1, &rfds, nullptr, nullptr, &tv);
                if (sel > 0 && FD_ISSET(cli.fd, &rfds)) {
                    int r = lwip_recv(cli.fd, sink, sizeof(sink), MSG_DONTWAIT);
                    if (r <= 0) {
                        cli.peerClosed = true;
                        break;
                    }
                }
            }
        }
    }
    cli.closeAbort();
}

bool connectMqttBrokerSession(
    RawMqttSocket& cli,
    const char* mqttHost,
    uint16_t mqttPort,
    const char* sessionId,
    uint32_t& ioConnSeq,
    const std::atomic<bool>& wantActive,
    uint32_t tcpTimeoutMs,
    uint32_t connAckTimeoutMs
) {
    if (!wantActive.load(std::memory_order_acquire) || WebUiSyncEngine::isRequested() ||
        !mqttHost || mqttHost[0] == '\0') {
        return false;
    }
    if (!hardwareHAL.capabilities().hasPsram &&
        (rawSockFree8BitHeap() < 5120U || rawSockLargest8BitBlock() < 2560U)) {
        vTaskDelay(pdMS_TO_TICKS(120));
        return false;
    }
    if (!cli.connectHost(mqttHost, mqttPort, wantActive, tcpTimeoutMs)) {
        return false;
    }
    char mqttConnectId[64];
    snprintf(mqttConnectId, sizeof(mqttConnectId), "%s_%u", sessionId, (++ioConnSeq) % 1000U);
    if (!sendMqttConnectPacket(cli, mqttConnectId, wantActive)) {
        cli.closeAbort();
        return false;
    }
    uint8_t connAck[4] = {0};
    if (!cli.readExact(connAck, 4U, wantActive, connAckTimeoutMs) ||
        connAck[0] != 0x20U || connAck[3] != 0x00U) {
        cli.closeAbort();
        return false;
    }
    return true;
}

void sendHttpDeleteOneShotAbort(
    const char* host,
    uint16_t port,
    const char* path,
    uint32_t connectTimeoutMs,
    uint32_t waitResponseMs
) {
    if (!host || host[0] == '\0' || !path || path[0] == '\0' || port == 0U) {
        return;
    }
    std::atomic<bool> alwaysActive{true};
    RawMqttSocket sock;
    if (!sock.connectHost(host, port, alwaysActive, connectTimeoutMs)) {
        return;
    }
    char req[256];
    int n = snprintf(
        req,
        sizeof(req),
        "DELETE %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
        path,
        host,
        static_cast<unsigned>(port)
    );
    if (n > 0 && static_cast<size_t>(n) < sizeof(req) &&
        sock.writeAll(reinterpret_cast<const uint8_t*>(req), static_cast<size_t>(n), alwaysActive, 400U)) {
        uint32_t t0 = millis();
        while (sock.connected() && sock.available() == 0U && (millis() - t0) < waitResponseMs) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    sock.closeAbort();
}

#else

void RawMqttSocket::closeAbort() {
    fd = -1;
    rxOff = 0U;
    rxEnd = 0U;
    peerClosed = true;
}

bool RawMqttSocket::connectHost(const char*, uint16_t, const std::atomic<bool>&, uint32_t) { return false; }
size_t RawMqttSocket::available() { return 0U; }
int RawMqttSocket::readByte() { return -1; }
bool RawMqttSocket::readExact(uint8_t*, size_t, const std::atomic<bool>&, uint32_t) { return false; }
bool RawMqttSocket::drain(size_t, const std::atomic<bool>&, uint32_t) { return false; }
bool RawMqttSocket::writeAll(const uint8_t*, size_t, const std::atomic<bool>&, uint32_t) { return false; }

bool sendMqttPublishPacket(RawMqttSocket&, const char*, const char*, bool, const std::atomic<bool>&, char*, size_t) { return false; }
bool sendMqttSubscribePacket(RawMqttSocket&, uint16_t, const char*, const std::atomic<bool>&) { return false; }
bool sendMqttUnsubscribePacket(RawMqttSocket&, uint16_t, const char*, const std::atomic<bool>&) { return false; }
bool sendMqttConnectPacket(RawMqttSocket&, const char*, const std::atomic<bool>&) { return false; }
void sendMqttDisconnectAndStop(RawMqttSocket& cli, const std::atomic<bool>&) { cli.closeAbort(); }
bool connectMqttBrokerSession(RawMqttSocket&, const char*, uint16_t, const char*, uint32_t&, const std::atomic<bool>&, uint32_t, uint32_t) { return false; }
void sendHttpDeleteOneShotAbort(const char*, uint16_t, const char*, uint32_t, uint32_t) {}

#endif

} // namespace net

