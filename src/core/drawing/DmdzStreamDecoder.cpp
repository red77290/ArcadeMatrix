#include "DmdzStreamDecoder.h"

#include <string.h>

namespace core::drawing {

bool DmdzStreamDecoder::parseHeader(const uint8_t raw16[16], DmdzStreamHeader& out) {
    out = DmdzStreamHeader{};
    if (!raw16) {
        return false;
    }
    const bool isDmdz = (memcmp(raw16, "DMDZ", 4) == 0);
    const bool isDmds = (!isDmdz && memcmp(raw16, "DMDS", 4) == 0);
    if (!isDmdz && !isDmds) {
        return false;
    }
    out.isDmdz = isDmdz;
    out.version = raw16[4];
    if (isDmdz && out.version != kSupportedVersion) {
        return false;
    }
    out.fps = (raw16[5] > 0U) ? raw16[5] : 15U;
    out.width = static_cast<uint16_t>(raw16[6] | (static_cast<uint16_t>(raw16[7]) << 8));
    out.height = static_cast<uint16_t>(raw16[8] | (static_cast<uint16_t>(raw16[9]) << 8));
    out.totalFrames = static_cast<uint16_t>(raw16[10] | (static_cast<uint16_t>(raw16[11]) << 8));
    out.frameDurMs = static_cast<uint16_t>(raw16[12] | (static_cast<uint16_t>(raw16[13]) << 8));
    out.keyframeInterval = static_cast<uint16_t>(raw16[14] | (static_cast<uint16_t>(raw16[15]) << 8));
    return (out.width > 0U && out.height > 0U && out.totalFrames > 0U);
}

void DmdzStreamDecoder::resetStream(uint16_t width, uint16_t height) {
    m_width = width;
    m_height = height;
    m_numPixels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    m_pos = 0;
    m_bytesRemaining = 0;
    m_last = 0;
    m_litRemaining = 0;
    m_loByte = 0;
    m_state = State::FrameType;
    m_error = (m_numPixels == 0U);
    memset(m_cache, 0, sizeof(m_cache));
}

bool DmdzStreamDecoder::beginFrame(uint16_t payloadLen) {
    if (m_numPixels == 0U || payloadLen == 0U) {
        m_error = true;
        return false;
    }
    memset(m_cache, 0, sizeof(m_cache));
    m_pos = 0;
    m_bytesRemaining = payloadLen;
    m_last = 0;
    m_litRemaining = 0;
    m_loByte = 0;
    m_state = State::FrameType;
    m_error = false;
    return true;
}

bool DmdzStreamDecoder::feed(const uint8_t* src, size_t len, uint16_t* frame565) {
    if (m_error || !frame565 || (!src && len > 0U) || len > m_bytesRemaining) {
        m_error = true;
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = src[i];
        --m_bytesRemaining;

        switch (m_state) {
            case State::FrameType: {
                if (b == kFrameDelta || b == kFrameKey) {
                    m_state = State::Op;
                } else if (b == kFrameScroll) {
                    m_state = State::ScrollDy;
                } else {
                    m_error = true;
                    return false;
                }
                break;
            }
            case State::ScrollDy: {
                const int dy = static_cast<int>(static_cast<int8_t>(b));
                if (dy > 0) {
                    const uint16_t s = static_cast<uint16_t>(dy);
                    if (s >= m_height) {
                        m_error = true;
                        return false;
                    }
                    memmove(
                        frame565,
                        frame565 + static_cast<size_t>(s) * m_width,
                        static_cast<size_t>(m_height - s) * m_width * sizeof(uint16_t)
                    );
                } else if (dy < 0) {
                    const uint16_t s = static_cast<uint16_t>(-dy);
                    if (s >= m_height) {
                        m_error = true;
                        return false;
                    }
                    memmove(
                        frame565 + static_cast<size_t>(s) * m_width,
                        frame565,
                        static_cast<size_t>(m_height - s) * m_width * sizeof(uint16_t)
                    );
                }
                m_state = State::Op;
                break;
            }
            case State::Op: {
                if (b < 0x40U) {
                    const uint32_t count = static_cast<uint32_t>(b) + 1U;
                    if (m_pos + count > m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    m_pos += count;
                    m_last = frame565[m_pos - 1U];
                    m_cache[hashPixel(m_last)] = m_last;
                } else if (b < 0x80U) {
                    const uint32_t count = static_cast<uint32_t>(b & 0x3FU) + 1U;
                    if (m_pos + count > m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    for (uint32_t k = 0; k < count; ++k) {
                        frame565[m_pos++] = m_last;
                    }
                } else if (b < 0xC0U) {
                    if (m_pos >= m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    m_last = m_cache[b & 0x3FU];
                    frame565[m_pos++] = m_last;
                } else if (b < 0xE0U) {
                    const uint8_t x = static_cast<uint8_t>(b & 0x1FU);
                    if (x > 26U || m_pos >= m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    const int dr = static_cast<int>(x / 9U) - 1;
                    const int dg = static_cast<int>((x / 3U) % 3U) - 1;
                    const int db = static_cast<int>(x % 3U) - 1;
                    const uint16_t r = static_cast<uint16_t>((static_cast<int>((m_last >> 11) & 0x1FU) + dr) & 0x1F);
                    const uint16_t g = static_cast<uint16_t>((static_cast<int>((m_last >> 5) & 0x3FU) + dg) & 0x3F);
                    const uint16_t bl = static_cast<uint16_t>((static_cast<int>(m_last & 0x1FU) + db) & 0x1F);
                    const uint16_t p = static_cast<uint16_t>((r << 11) | (g << 5) | bl);
                    frame565[m_pos++] = p;
                    m_cache[hashPixel(p)] = p;
                    m_last = p;
                } else if (b < 0xF0U) {
                    const uint8_t count = static_cast<uint8_t>((b & 0x0FU) + 1U);
                    if (m_pos + count > m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    m_litRemaining = count;
                    m_state = State::LitLo;
                } else if (b == 0xF0U) {
                    m_state = State::LongSkipLo;
                } else {
                    m_error = true;
                    return false;
                }
                break;
            }
            case State::LitLo: {
                m_loByte = b;
                m_state = State::LitHi;
                break;
            }
            case State::LitHi: {
                const uint16_t p = static_cast<uint16_t>(m_loByte) | (static_cast<uint16_t>(b) << 8);
                frame565[m_pos++] = p;
                m_cache[hashPixel(p)] = p;
                m_last = p;
                --m_litRemaining;
                m_state = (m_litRemaining > 0U) ? State::LitLo : State::Op;
                break;
            }
            case State::LongSkipLo: {
                m_loByte = b;
                m_state = State::LongSkipHi;
                break;
            }
            case State::LongSkipHi: {
                const uint32_t count = static_cast<uint32_t>(m_loByte) | (static_cast<uint32_t>(b) << 8);
                if (count == 0U || m_pos + count > m_numPixels) {
                    m_error = true;
                    return false;
                }
                m_pos += count;
                m_last = frame565[m_pos - 1U];
                m_cache[hashPixel(m_last)] = m_last;
                m_state = State::Op;
                break;
            }
        }
    }
    return true;
}

bool DmdzStreamDecoder::finishFrame() const {
    return !m_error && (m_bytesRemaining == 0U) && (m_state == State::Op) && (m_pos == m_numPixels);
}

} // namespace core::drawing
