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
 * @brief Lossless, zero-allocation, chunk-resumable 16-bit True-RGB565 stream decoder (`DMDZ` v2).
 *
 * Decodes compressed Showcase `dmdz2` frames (`FRAME_DELTA`, `FRAME_SCROLL`, `FRAME_KEY`,
 * `FRAME_SCROLL_2D`, `FRAME_RAW`) directly in-place inside the caller's `uint16_t*` RGB565
 * frame buffer (`width * height` pixels) using ~148 bytes of POD member state and zero heap
 * allocations. Supports arbitrary chunk boundaries down to 1 byte.
 */
class DmdzStreamDecoder {
public:
    static constexpr uint8_t kSupportedVersion = 2;
    static constexpr uint8_t kMinSupportedVersion = 2;
    static constexpr uint8_t kMaxSupportedVersion = 2;
    static constexpr uint8_t kFrameDelta = 0;
    static constexpr uint8_t kFrameScroll = 1;
    static constexpr uint8_t kFrameKey = 2;
    static constexpr uint8_t kFrameScroll2D = 3;
    static constexpr uint8_t kFrameRaw = 4;

    /**
     * @brief Parses a 16-byte `DMDZ` (v2) or `DMDS` container header.
     * @return true if `raw16` is a valid `DMDZ` v2 or `DMDS` header with non-zero geometry.
     */
    static bool parseHeader(const uint8_t raw16[16], DmdzStreamHeader& out);

    static inline uint8_t hashPixel(uint16_t p) {
        return static_cast<uint8_t>(
            ((((p >> 11) & 0x1FU) * 3U) + (((p >> 5) & 0x3FU) * 5U) + ((p & 0x1FU) * 7U)) & 63U
        );
    }

    void resetStream(uint16_t width, uint16_t height, uint8_t version = kSupportedVersion);
    inline void init(uint16_t width, uint16_t height, uint8_t version = kSupportedVersion) {
        resetStream(width, height, version);
    }
    bool beginFrame(uint16_t payloadLen);
    bool feed(const uint8_t* src, size_t len, uint16_t* frame565);
    bool finishFrame() const;

    bool hasError() const { return m_error; }
    uint32_t pixelsDecoded() const { return m_pos; }
    uint8_t streamVersion() const { return m_version; }

private:
    enum class State : uint8_t {
        FrameType = 0,
        ScrollDy,
        Scroll2DDx,
        Scroll2DDy,
        RawLo,
        RawHi,
        Op,
        LitLo,
        LitHi,
        LongSkipLo,
        LongSkipHi,
        LumaLo,
        TLumaLo,
        LzLo,
        LzHi,
    };

    uint16_t m_cache[64] = {0};
    uint32_t m_numPixels = 0;
    uint32_t m_pos = 0;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    uint16_t m_bytesRemaining = 0;
    uint16_t m_last = 0;
    uint8_t m_version = kSupportedVersion;
    uint8_t m_litRemaining = 0;
    uint8_t m_loByte = 0;
    uint8_t m_opHeader = 0;
    int8_t m_scrollDx = 0;
    State m_state = State::FrameType;
    bool m_error = false;
};

} // namespace core::drawing
