#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core::drawing {

struct DmdzStreamHeader {
    uint8_t version = 0;
    uint8_t fps = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t totalFrames = 0;
    uint16_t frameDurMs = 0;
    uint16_t keyframeInterval = 0;
    bool isDmdz = false;
};

/**
 * @brief Lossless, zero-allocation, chunk-resumable RGB565 stream decoder (`DMDZ` v1).
 *
 * Decodes compressed Showcase `dmdz1` frames directly in-place inside the caller's `uint16_t*`
 * RGB565 frame buffer (`width * height` pixels) using ~152 bytes of stack/member POD state and
 * zero heap allocations. Supports arbitrary network chunk boundaries down to 1 byte.
 */
class DmdzStreamDecoder {
public:
    static constexpr uint8_t kSupportedVersion = 1;
    static constexpr uint8_t kFrameDelta = 0;
    static constexpr uint8_t kFrameScroll = 1;
    static constexpr uint8_t kFrameKey = 2;

    /**
     * @brief Parses a 16-byte `DMDZ` or `DMDS` container header.
     * @return true if `raw16` is a valid `DMDZ` (v1) or `DMDS` header with non-zero geometry.
     */
    static bool parseHeader(const uint8_t raw16[16], DmdzStreamHeader& out);

    static inline uint8_t hashPixel(uint16_t p) {
        return static_cast<uint8_t>(
            ((((p >> 11) & 0x1FU) * 3U) + (((p >> 5) & 0x3FU) * 5U) + ((p & 0x1FU) * 7U)) & 63U
        );
    }

    void resetStream(uint16_t width, uint16_t height);
    inline void init(uint16_t width, uint16_t height) { resetStream(width, height); }
    bool beginFrame(uint16_t payloadLen);
    bool feed(const uint8_t* src, size_t len, uint16_t* frame565);
    bool finishFrame() const;

    bool hasError() const { return m_error; }
    uint32_t pixelsDecoded() const { return m_pos; }

private:
    enum class State : uint8_t {
        FrameType = 0,
        ScrollDy,
        Op,
        LitLo,
        LitHi,
        LongSkipLo,
        LongSkipHi,
    };

    uint16_t m_cache[64] = {0};
    uint32_t m_numPixels = 0;
    uint32_t m_pos = 0;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    uint16_t m_bytesRemaining = 0;
    uint16_t m_last = 0;
    uint8_t m_litRemaining = 0;
    uint8_t m_loByte = 0;
    State m_state = State::FrameType;
    bool m_error = false;
};

/**
 * @brief Converts a Showcase HTTP stream path (`/api/v1/{domain}/stream/{item_id}.rgb565.stream?...`)
 *        into the ZeDMD-style credit-acknowledged WebSocket path (`/api/v1/{domain}/ws/{item_id}.dmdz?...`).
 * @return true if `rawPath` matched the Showcase stream route and `outWsPath` was populated.
 */
bool buildDmdzWebSocketPath(
    const char* rawPath,
    const char* clientId,
    char* outWsPath,
    size_t outCap,
    uint8_t credits = 2U
);

/**
 * @brief Zero-allocation RFC 6455 binary WebSocket frame demuxer with per-message credit ACK
 *        counting and automatic PING->PONG / CLOSE handling.
 */
class DmdzWsDemuxer {
public:
    static const uint8_t kMaskKey[4];
    // Masked RFC 6455 binary frame carrying 1 byte `'A'` (`0x41`).
    static const uint8_t kMaskedCreditAckFrame[7];
    // Masked RFC 6455 CLOSE frame (`0` payload bytes).
    static const uint8_t kMaskedCloseFrame[6];

    void reset();

    size_t decode(
        const uint8_t* src,
        size_t srcLen,
        size_t& outSrcConsumed,
        uint8_t* dst,
        size_t dstCap,
        uint8_t& outCreditsToAck
    );

    bool isClosed() const { return m_closed; }
    bool hasError() const { return m_error; }
    bool hasPendingPong() const { return m_pongPending; }
    size_t takePendingPongFrame(uint8_t outFrame[131]);

private:
    enum class WsState : uint8_t {
        Hdr0 = 0,
        Hdr1,
        ExtLen16,
        ExtLen64,
        MaskKey,
        DataPayload,
        CtrlPayload,
    };

    void enterPayloadOrMask(uint8_t& outCreditsToAck);
    void enterPayloadAfterHeader(uint8_t& outCreditsToAck);

    uint8_t m_pongBuf[125] = {0};
    uint8_t m_peerMaskKey[4] = {0};
    size_t m_frameRem = 0;
    WsState m_state = WsState::Hdr0;
    uint8_t m_opcode = 0;
    uint8_t m_lenBytesRem = 0;
    uint8_t m_maskIdx = 0;
    uint8_t m_ctrlLen = 0;
    uint8_t m_pongLen = 0;
    bool m_fin = true;
    bool m_masked = false;
    bool m_pongPending = false;
    bool m_closed = false;
    bool m_error = false;
};

} // namespace core::drawing
