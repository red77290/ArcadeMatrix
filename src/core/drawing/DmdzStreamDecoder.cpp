#include "DmdzStreamDecoder.h"

#include <string.h>

namespace core::drawing {

namespace {

static constexpr int8_t kTDiffVecs[8][3] = {
    {-1, -2, -1},
    {-1, -1, -1},
    { 0, -1,  0},
    { 0,  1,  0},
    { 1,  1,  1},
    { 1,  2,  1},
    { 0, -2,  0},
    { 0,  2,  0},
};

inline uint16_t decodeLumaDeltaPixel(uint16_t base, uint8_t uDgHi, uint8_t b2) {
    const uint8_t uDg = static_cast<uint8_t>(((uDgHi & 0x03U) << 2) | (b2 >> 6));
    const int dg = static_cast<int>(uDg) - 8;
    const int dgRb = static_cast<int>(uDg >> 1U) - 4;
    const int dr = dgRb + static_cast<int>((b2 >> 3) & 0x07U) - 4;
    const int db = dgRb + static_cast<int>(b2 & 0x07U) - 4;
    const uint16_t r = static_cast<uint16_t>((static_cast<int>((base >> 11) & 0x1FU) + dr) & 0x1FU);
    const uint16_t g = static_cast<uint16_t>((static_cast<int>((base >> 5) & 0x3FU) + dg) & 0x3FU);
    const uint16_t bl = static_cast<uint16_t>((static_cast<int>(base & 0x1FU) + db) & 0x1FU);
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

bool applyVerticalScrollInPlace(uint16_t* frame565, uint16_t width, uint16_t height, int dy) {
    if (dy > 0) {
        const uint16_t s = static_cast<uint16_t>(dy);
        if (s >= height) {
            return false;
        }
        memmove(
            frame565,
            frame565 + static_cast<size_t>(s) * width,
            static_cast<size_t>(height - s) * width * sizeof(uint16_t)
        );
    } else if (dy < 0) {
        const uint16_t s = static_cast<uint16_t>(-dy);
        if (s >= height) {
            return false;
        }
        memmove(
            frame565 + static_cast<size_t>(s) * width,
            frame565,
            static_cast<size_t>(height - s) * width * sizeof(uint16_t)
        );
    }
    return true;
}

bool applyScroll2DInPlace(uint16_t* frame565, uint16_t width, uint16_t height, int dx, int dy) {
    const uint16_t absDx = static_cast<uint16_t>(dx >= 0 ? dx : -dx);
    if (absDx >= width || !applyVerticalScrollInPlace(frame565, width, height, dy)) {
        return false;
    }
    if (dx > 0) {
        const size_t copyBytes = static_cast<size_t>(width - absDx) * sizeof(uint16_t);
        for (uint16_t row = 0U; row < height; ++row) {
            uint16_t* rowPtr = frame565 + static_cast<size_t>(row) * width;
            memmove(rowPtr, rowPtr + absDx, copyBytes);
        }
    } else if (dx < 0) {
        const size_t copyBytes = static_cast<size_t>(width - absDx) * sizeof(uint16_t);
        for (uint16_t row = 0U; row < height; ++row) {
            uint16_t* rowPtr = frame565 + static_cast<size_t>(row) * width;
            memmove(rowPtr + absDx, rowPtr, copyBytes);
        }
    }
    return true;
}

} // namespace

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
    if (isDmdz && (out.version < kMinSupportedVersion || out.version > kMaxSupportedVersion)) {
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

void DmdzStreamDecoder::resetStream(uint16_t width, uint16_t height, uint8_t version) {
    m_width = width;
    m_height = height;
    m_numPixels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    m_pos = 0;
    m_bytesRemaining = 0;
    m_last = 0;
    m_version = version;
    m_litRemaining = 0;
    m_loByte = 0;
    m_opHeader = 0;
    m_scrollDx = 0;
    m_state = State::FrameType;
    m_error = (m_numPixels == 0U || version < kMinSupportedVersion || version > kMaxSupportedVersion);
    memset(m_cache, 0, sizeof(m_cache));
}

bool DmdzStreamDecoder::beginFrame(uint16_t payloadLen) {
    if (m_numPixels == 0U || payloadLen == 0U ||
        m_version < kMinSupportedVersion || m_version > kMaxSupportedVersion) {
        m_error = true;
        return false;
    }
    memset(m_cache, 0, sizeof(m_cache));
    m_pos = 0;
    m_bytesRemaining = payloadLen;
    m_last = 0;
    m_litRemaining = 0;
    m_loByte = 0;
    m_opHeader = 0;
    m_scrollDx = 0;
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
                } else if (b == kFrameScroll2D) {
                    m_state = State::Scroll2DDx;
                } else if (b == kFrameRaw) {
                    if (static_cast<uint32_t>(m_bytesRemaining) != (m_numPixels * 2U)) {
                        m_error = true;
                        return false;
                    }
                    m_state = State::RawLo;
                } else {
                    m_error = true;
                    return false;
                }
                break;
            }
            case State::ScrollDy: {
                const int dy = static_cast<int>(static_cast<int8_t>(b));
                if (!applyVerticalScrollInPlace(frame565, m_width, m_height, dy)) {
                    m_error = true;
                    return false;
                }
                m_state = State::Op;
                break;
            }
            case State::Scroll2DDx: {
                m_scrollDx = static_cast<int8_t>(b);
                m_state = State::Scroll2DDy;
                break;
            }
            case State::Scroll2DDy: {
                const int dx = static_cast<int>(m_scrollDx);
                const int dy = static_cast<int>(static_cast<int8_t>(b));
                if (!applyScroll2DInPlace(frame565, m_width, m_height, dx, dy)) {
                    m_error = true;
                    return false;
                }
                m_state = State::Op;
                break;
            }
            case State::RawLo: {
                m_loByte = b;
                m_state = State::RawHi;
                break;
            }
            case State::RawHi: {
                frame565[m_pos++] = static_cast<uint16_t>(m_loByte) | (static_cast<uint16_t>(b) << 8);
                m_state = (m_pos < m_numPixels) ? State::RawLo : State::Op;
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
                } else if (b <= 0xDAU) {
                    const uint8_t x = static_cast<uint8_t>(b & 0x1FU);
                    if (m_pos >= m_numPixels) {
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
                } else if (b >= 0xDCU && b <= 0xDFU) {
                    if (m_pos >= m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    m_opHeader = static_cast<uint8_t>(b - 0xDCU);
                    m_state = State::LumaLo;
                } else if (b >= 0xE0U && b < 0xF0U) {
                    const uint8_t count = static_cast<uint8_t>((b & 0x0FU) + 1U);
                    if (m_pos + count > m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    m_litRemaining = count;
                    m_state = State::LitLo;
                } else if (b == 0xF0U) {
                    m_state = State::LongSkipLo;
                } else if (b >= 0xF1U && b <= 0xF4U) {
                    if (m_pos >= m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    m_opHeader = static_cast<uint8_t>(b - 0xF1U);
                    m_state = State::TLumaLo;
                } else if (b == 0xF6U || b == 0xF7U) {
                    m_opHeader = static_cast<uint8_t>(b & 0x01U);
                    m_state = State::LzLo;
                } else if (b >= 0xF8U) {
                    if (m_pos >= m_numPixels) {
                        m_error = true;
                        return false;
                    }
                    const int8_t* vec = kTDiffVecs[b & 0x07U];
                    const uint16_t refP = frame565[m_pos];
                    const uint16_t r = static_cast<uint16_t>(
                        (static_cast<int>((refP >> 11) & 0x1FU) + static_cast<int>(vec[0])) & 0x1FU
                    );
                    const uint16_t g = static_cast<uint16_t>(
                        (static_cast<int>((refP >> 5) & 0x3FU) + static_cast<int>(vec[1])) & 0x3FU
                    );
                    const uint16_t bl = static_cast<uint16_t>(
                        (static_cast<int>(refP & 0x1FU) + static_cast<int>(vec[2])) & 0x1FU
                    );
                    const uint16_t p = static_cast<uint16_t>((r << 11) | (g << 5) | bl);
                    frame565[m_pos++] = p;
                    m_cache[hashPixel(p)] = p;
                    m_last = p;
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
            case State::LumaLo: {
                const uint16_t p = decodeLumaDeltaPixel(m_last, m_opHeader, b);
                frame565[m_pos++] = p;
                m_cache[hashPixel(p)] = p;
                m_last = p;
                m_state = State::Op;
                break;
            }
            case State::TLumaLo: {
                const uint16_t p = decodeLumaDeltaPixel(frame565[m_pos], m_opHeader, b);
                frame565[m_pos++] = p;
                m_cache[hashPixel(p)] = p;
                m_last = p;
                m_state = State::Op;
                break;
            }
            case State::LzLo: {
                m_loByte = b;
                m_state = State::LzHi;
                break;
            }
            case State::LzHi: {
                const uint32_t packed17 = (static_cast<uint32_t>(m_opHeader) << 16) |
                                          (static_cast<uint32_t>(b) << 8) |
                                          static_cast<uint32_t>(m_loByte);
                const uint32_t count = ((packed17 >> 11) & 0x3FU) + 3U;
                const uint32_t dist = (packed17 & 0x7FFU) + 1U;
                if (dist > m_pos || (m_pos + count) > m_numPixels) {
                    m_error = true;
                    return false;
                }
                const uint32_t srcIdx = m_pos - dist;
                for (uint32_t k = 0U; k < count; ++k) {
                    frame565[m_pos++] = frame565[srcIdx + k];
                }
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
    return !m_error &&
           (m_bytesRemaining == 0U) &&
           (m_state == State::Op) &&
           (m_pos == m_numPixels);
}

} // namespace core::drawing
