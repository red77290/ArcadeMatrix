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

    int recvNonBlocking(uint8_t* dst, size_t maxLen);
    bool sendNonBlockingAll(const uint8_t* data, size_t len);

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

using MqttPublishFn = void (*)(
    void* ctx,
    const char* topic,
    const char* payload,
    size_t payloadLen,
    bool truncated
);

using MqttPubAckFn = void (*)(void* ctx, uint16_t pktId);

/**
 * @struct MqttStreamParser
 * @brief Zero-heap incremental MQTT 3.1.1 client packet state machine.
 *
 * Designed for Core 1 non-blocking polling (`0 ms` stall) and Core 0 handshake pumping:
 * - Handles arbitrary TCP segmentation (1-byte fragments or coalesced multi-packet bursts).
 * - Drains oversized `PUBLISH` payloads cleanly without disconnecting or losing packet framing.
 * - Tracks `CONNACK`, `SUBACK`, `PINGRESP`, and emits `PUBACK` callbacks for QoS 1 frames.
 */
struct MqttStreamParser {
    enum class State : uint8_t {
        FixedHeaderType = 0,
        RemainingLength,
        PublishTopicLenHi,
        PublishTopicLenLo,
        PublishTopicBody,
        PublishPktIdHi,
        PublishPktIdLo,
        PublishPayload,
        ControlBody,
    };

    char* payloadBuf = nullptr;
    size_t payloadCap = 0U;
    char topicBuf[128] = {0};
    uint8_t ctrlBuf[8] = {0};

    State state = State::FixedHeaderType;
    uint8_t pktHeader = 0U;
    uint32_t remLen = 0U;
    uint32_t remMult = 1U;
    uint8_t remBytesCount = 0U;
    uint32_t remBytesLeft = 0U;
    uint16_t topicRem = 0U;
    uint16_t topicStored = 0U;
    uint16_t pktId = 0U;
    size_t payloadStored = 0U;
    uint8_t ctrlStored = 0U;
    bool payloadTruncated = false;
    bool protocolError = false;

    bool connAckReceived = false;
    uint8_t lastConnAckRc = 0xFFU;
    uint16_t subAckCount = 0U;
    uint16_t lastSubAckPktId = 0U;
    uint8_t lastSubAckRc = 0x80U;
    uint32_t pingRespCount = 0U;

    void attachBuffer(char* buf, size_t cap);
    void reset();
    bool feed(
        const uint8_t* data,
        size_t len,
        MqttPublishFn onPublish,
        MqttPubAckFn onPubAck,
        void* ctx
    );
    bool takePingResp();
};

bool buildMqttConnectPacket(
    uint8_t* outBuf,
    size_t outCap,
    size_t& outLen,
    const char* clientId,
    const char* user = nullptr,
    const char* pass = nullptr,
    uint16_t keepAliveSec = 45U
);

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

bool sendMqttConnectAuthPacket(
    RawMqttSocket& cli,
    const char* clientId,
    const char* user,
    const char* pass,
    uint16_t keepAliveSec,
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

