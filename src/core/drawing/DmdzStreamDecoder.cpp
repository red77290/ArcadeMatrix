#include "DmdzStreamDecoder.h"

#include <stdio.h>
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

const uint8_t DmdzWsDemuxer::kMaskKey[4] = {0x37U, 0xFAU, 0x21U, 0x3DU};
const uint8_t DmdzWsDemuxer::kMaskedCreditAckFrame[7] = {
    0x82U, 0x81U, 0x37U, 0xFAU, 0x21U, 0x3DU, static_cast<uint8_t>(0x41U ^ 0x37U)
};
const uint8_t DmdzWsDemuxer::kMaskedCloseFrame[6] = {
    0x88U, 0x80U, 0x37U, 0xFAU, 0x21U, 0x3DU
};

size_t DmdzWsDemuxer::buildMaskedCreditAckFrame(uint8_t creditCount, uint8_t outFrame[22]) {
    if (!outFrame || creditCount == 0U) {
        return 0U;
    }
    const uint8_t n = (creditCount <= 16U) ? creditCount : 16U;
    outFrame[0] = 0x82U; // FIN + Opcode BINARY (0x2)
    outFrame[1] = static_cast<uint8_t>(0x80U | n);
    memcpy(outFrame + 2, kMaskKey, 4U);
    for (uint8_t i = 0U; i < n; ++i) {
        outFrame[6U + i] = static_cast<uint8_t>(0x41U ^ kMaskKey[i & 3U]);
    }
    return static_cast<size_t>(6U + n);
}

bool buildDmdzWebSocketPath(
    const char* rawPath,
    const char* clientId,
    char* outWsPath,
    size_t outCap,
    uint8_t credits
) {
    if (!outWsPath || outCap == 0U) {
        return false;
    }
    outWsPath[0] = '\0';
    if (!rawPath || rawPath[0] == '\0') {
        return false;
    }

    const char* apiPrefix = strstr(rawPath, "/api/v1/");
    if (!apiPrefix) {
        return false;
    }
    const char* domainStart = apiPrefix + 8;
    const char* domainSlash = strchr(domainStart, '/');
    if (!domainSlash || domainSlash == domainStart) {
        return false;
    }
    const size_t domainLen = static_cast<size_t>(domainSlash - domainStart);
    if (domainLen > 24U || strncmp(domainSlash + 1, "stream/", 7) != 0) {
        return false;
    }

    const char* itemStart = domainSlash + 8;
    const char* qMark = strchr(itemStart, '?');
    const char* pathEnd = qMark ? qMark : (itemStart + strlen(itemStart));
    constexpr size_t kSuffixLen = 14U; // strlen(".rgb565.stream")
    if (static_cast<size_t>(pathEnd - itemStart) <= kSuffixLen ||
        strncmp(pathEnd - kSuffixLen, ".rgb565.stream", kSuffixLen) != 0) {
        return false;
    }
    const size_t itemLen = static_cast<size_t>((pathEnd - kSuffixLen) - itemStart);
    if (itemLen == 0U || itemLen > 128U) {
        return false;
    }

    bool purge = true;
    char apiKeyParam[80] = "";
    if (qMark && qMark[1] != '\0') {
        const char* query = qMark + 1;
        if (strstr(query, "purge=false") != nullptr || strstr(query, "purge=0") != nullptr) {
            purge = false;
        }
        const char* ak = strstr(query, "api_key=");
        if (ak != nullptr) {
            const char* akEnd = strchr(ak, '&');
            size_t akLen = akEnd ? static_cast<size_t>(akEnd - ak) : strlen(ak);
            if (akLen > 8U && akLen + 2U < sizeof(apiKeyParam)) {
                apiKeyParam[0] = '&';
                memcpy(apiKeyParam + 1, ak, akLen);
                apiKeyParam[akLen + 1] = '\0';
            }
        }
    }

    const uint8_t clampedCredits = (credits < 1U) ? 1U : (credits > 16U ? 16U : credits);
    char creditsParam[16] = "";
    if (clampedCredits != 2U) {
        snprintf(creditsParam, sizeof(creditsParam), "&credits=%u", static_cast<unsigned>(clampedCredits));
    }

    const int n = (clientId && clientId[0] != '\0')
        ? snprintf(
              outWsPath,
              outCap,
              "/api/v1/%.*s/ws/%.*s.dmdz?purge=%s&client_id=%s%s%s",
              static_cast<int>(domainLen),
              domainStart,
              static_cast<int>(itemLen),
              itemStart,
              purge ? "true" : "false",
              clientId,
              creditsParam,
              apiKeyParam
          )
        : snprintf(
              outWsPath,
              outCap,
              "/api/v1/%.*s/ws/%.*s.dmdz?purge=%s%s%s",
              static_cast<int>(domainLen),
              domainStart,
              static_cast<int>(itemLen),
              itemStart,
              purge ? "true" : "false",
              creditsParam,
              apiKeyParam
          );
    if (n <= 0 || static_cast<size_t>(n) >= outCap) {
        outWsPath[0] = '\0';
        return false;
    }
    return true;
}

void DmdzWsDemuxer::reset() {
    m_frameRem = 0;
    m_state = WsState::Hdr0;
    m_opcode = 0;
    m_lenBytesRem = 0;
    m_maskIdx = 0;
    m_ctrlLen = 0;
    m_pongLen = 0;
    m_fin = true;
    m_masked = false;
    m_pongPending = false;
    m_closed = false;
    m_error = false;
}

void DmdzWsDemuxer::enterPayloadOrMask(uint8_t& outCreditsToAck) {
    if (m_masked) {
        m_lenBytesRem = 4U;
        m_maskIdx = 0U;
        m_state = WsState::MaskKey;
    } else {
        enterPayloadAfterHeader(outCreditsToAck);
    }
}

void DmdzWsDemuxer::enterPayloadAfterHeader(uint8_t& outCreditsToAck) {
    m_ctrlLen = 0U;
    m_maskIdx = 0U;
    if (m_frameRem == 0U) {
        if (m_opcode == 0x8U) {
            m_closed = true;
        } else if (m_opcode == 0x9U) {
            m_pongLen = 0U;
            m_pongPending = true;
        } else if (m_opcode <= 0x2U && m_fin) {
            ++outCreditsToAck;
        }
        m_state = WsState::Hdr0;
    } else {
        m_state = (m_opcode >= 0x8U) ? WsState::CtrlPayload : WsState::DataPayload;
    }
}

size_t DmdzWsDemuxer::decode(
    const uint8_t* src,
    size_t srcLen,
    size_t& outSrcConsumed,
    uint8_t* dst,
    size_t dstCap,
    uint8_t& outCreditsToAck
) {
    outSrcConsumed = 0U;
    outCreditsToAck = 0U;
    if (!src || srcLen == 0U || !dst || dstCap == 0U || m_closed || m_error || m_pongPending) {
        return 0U;
    }

    size_t written = 0U;
    while (outSrcConsumed < srcLen && !m_closed && !m_error && !m_pongPending) {
        if (m_state == WsState::DataPayload) {
            if (written >= dstCap) {
                break;
            }
            const size_t avail = srcLen - outSrcConsumed;
            const size_t space = dstCap - written;
            size_t take = (avail < m_frameRem) ? avail : m_frameRem;
            if (take > space) {
                take = space;
            }
            if (!m_masked) {
                memcpy(dst + written, src + outSrcConsumed, take);
            } else {
                for (size_t k = 0U; k < take; ++k) {
                    dst[written + k] = static_cast<uint8_t>(
                        src[outSrcConsumed + k] ^ m_peerMaskKey[(m_maskIdx + k) & 3U]
                    );
                }
                m_maskIdx = static_cast<uint8_t>((m_maskIdx + take) & 3U);
            }
            written += take;
            outSrcConsumed += take;
            m_frameRem -= take;
            if (m_frameRem == 0U) {
                if (m_fin) {
                    ++outCreditsToAck;
                }
                m_state = WsState::Hdr0;
            }
            if (written >= dstCap) {
                break;
            }
            continue;
        }

        const uint8_t b = src[outSrcConsumed++];
        switch (m_state) {
            case WsState::Hdr0: {
                if ((b & 0x70U) != 0U) {
                    m_error = true;
                    break;
                }
                m_fin = (b & 0x80U) != 0U;
                m_opcode = static_cast<uint8_t>(b & 0x0FU);
                if ((m_opcode > 0x2U && m_opcode != 0x8U && m_opcode != 0x9U && m_opcode != 0xAU) ||
                    (m_opcode >= 0x8U && !m_fin)) {
                    m_error = true;
                    break;
                }
                m_state = WsState::Hdr1;
                break;
            }
            case WsState::Hdr1: {
                m_masked = (b & 0x80U) != 0U;
                const uint8_t len7 = static_cast<uint8_t>(b & 0x7FU);
                if (m_opcode >= 0x8U && len7 > 125U) {
                    m_error = true;
                    break;
                }
                if (len7 < 126U) {
                    m_frameRem = len7;
                    enterPayloadOrMask(outCreditsToAck);
                } else if (len7 == 126U) {
                    m_frameRem = 0U;
                    m_lenBytesRem = 2U;
                    m_state = WsState::ExtLen16;
                } else {
                    m_frameRem = 0U;
                    m_lenBytesRem = 8U;
                    m_state = WsState::ExtLen64;
                }
                break;
            }
            case WsState::ExtLen16: {
                m_frameRem = (m_frameRem << 8) | static_cast<size_t>(b);
                if (--m_lenBytesRem == 0U) {
                    enterPayloadOrMask(outCreditsToAck);
                }
                break;
            }
            case WsState::ExtLen64: {
                if (m_frameRem > 0xFFFFFFU) {
                    m_error = true;
                    break;
                }
                m_frameRem = (m_frameRem << 8) | static_cast<size_t>(b);
                if (m_frameRem > 65535U) {
                    m_error = true;
                    break;
                }
                if (--m_lenBytesRem == 0U) {
                    enterPayloadOrMask(outCreditsToAck);
                }
                break;
            }
            case WsState::MaskKey: {
                m_peerMaskKey[m_maskIdx++] = b;
                if (--m_lenBytesRem == 0U) {
                    enterPayloadAfterHeader(outCreditsToAck);
                }
                break;
            }
            case WsState::CtrlPayload: {
                const uint8_t unmasked = m_masked
                    ? static_cast<uint8_t>(b ^ m_peerMaskKey[m_maskIdx++ & 3U])
                    : b;
                if (m_opcode == 0x9U && m_ctrlLen < sizeof(m_pongBuf)) {
                    m_pongBuf[m_ctrlLen++] = unmasked;
                }
                if (--m_frameRem == 0U) {
                    if (m_opcode == 0x8U) {
                        m_closed = true;
                    } else if (m_opcode == 0x9U) {
                        m_pongLen = m_ctrlLen;
                        m_pongPending = true;
                    }
                    m_state = WsState::Hdr0;
                }
                break;
            }
            case WsState::DataPayload:
                break;
        }
    }
    return written;
}

size_t DmdzWsDemuxer::takePendingPongFrame(uint8_t outFrame[131]) {
    if (!m_pongPending || !outFrame) {
        return 0U;
    }
    m_pongPending = false;
    const uint8_t len = (m_pongLen <= 125U) ? m_pongLen : 125U;
    outFrame[0] = 0x8AU; // FIN + Opcode PONG (0xA)
    outFrame[1] = static_cast<uint8_t>(0x80U | len);
    memcpy(outFrame + 2, kMaskKey, 4U);
    for (uint8_t i = 0U; i < len; ++i) {
        outFrame[6U + i] = static_cast<uint8_t>(m_pongBuf[i] ^ kMaskKey[i & 3U]);
    }
    return static_cast<size_t>(6U + len);
}

} // namespace core::drawing
