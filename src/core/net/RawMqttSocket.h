#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace net {

/**
 * @brief Encodes an MQTT 3.1.1 variable-length Remaining Length field (1..4 bytes).
 */
size_t encodeMqttRemainingLength(uint8_t* out, size_t remLen);

/**
 * @struct RawMqttSocket
 * @brief Zero-heap, zero-TIME_WAIT stack TCP socket replacing Arduino `WiFiClient` for scoped
 *        MQTT 3.1.1 sessions and one-shot HTTP control requests.
 *
 * Eliminates the 1,460 B `WiFiClientRxBuffer` heap allocation and reclaims LwIP `tcp_pcb`s
 * synchronously via `SO_LINGER {1, 0}` so internal 8-bit DRAM never fragments across rotations.
 */
struct RawMqttSocket {
    int fd = -1;
    uint8_t rxBuf[128] = {0};
    size_t rxOff = 0;
    size_t rxEnd = 0;
    bool peerClosed = false;

    RawMqttSocket() = default;
    ~RawMqttSocket();

    RawMqttSocket(const RawMqttSocket&) = delete;
    RawMqttSocket& operator=(const RawMqttSocket&) = delete;

    bool connected() const { return (fd >= 0) && !peerClosed; }

    void closeAbort();

    bool connectHost(
        const char* host,
        uint16_t port,
        const std::atomic<bool>& wantActive,
        uint32_t timeoutMs
    );

    size_t available();
    int readByte();

    bool readExact(
        uint8_t* dst,
        size_t len,
        const std::atomic<bool>& wantActive,
        uint32_t timeoutMs = 2000U
    );

    bool drain(
        size_t len,
        const std::atomic<bool>& wantActive,
        uint32_t timeoutMs = 1500U
    );

    bool writeAll(
        const uint8_t* data,
        size_t len,
        const std::atomic<bool>& wantActive,
        uint32_t timeoutMs = 1200U
    );
};

bool sendMqttPublishPacket(
    RawMqttSocket& cli,
    const char* topic,
    const char* payload,
    bool retain,
    const std::atomic<bool>& wantActive,
    char* ioScratch = nullptr,
    size_t scratchCap = 0
);

bool sendMqttSubscribePacket(
    RawMqttSocket& cli,
    uint16_t pktId,
    const char* topic,
    const std::atomic<bool>& wantActive
);

bool sendMqttUnsubscribePacket(
    RawMqttSocket& cli,
    uint16_t pktId,
    const char* topic,
    const std::atomic<bool>& wantActive
);

bool sendMqttConnectPacket(
    RawMqttSocket& cli,
    const char* clientId,
    const std::atomic<bool>& wantActive
);

void sendMqttDisconnectAndStop(
    RawMqttSocket& cli,
    const std::atomic<bool>& wantActive
);

bool connectMqttBrokerSession(
    RawMqttSocket& cli,
    const char* mqttHost,
    uint16_t mqttPort,
    const char* sessionId,
    uint32_t& ioConnSeq,
    const std::atomic<bool>& wantActive,
    uint32_t tcpTimeoutMs = 1500U,
    uint32_t connAckTimeoutMs = 900U
);

/**
 * @brief Sends a zero-heap HTTP/1.1 `DELETE` request over `RawMqttSocket` and reclaims the
 *        LwIP `tcp_pcb` synchronously via `SO_LINGER {1, 0}` (`0 B` heap, `0` `TIME_WAIT` PCBs).
 */
void sendHttpDeleteOneShotAbort(
    const char* host,
    uint16_t port,
    const char* path,
    uint32_t connectTimeoutMs = 800U,
    uint32_t waitResponseMs = 300U
);

} // namespace net

