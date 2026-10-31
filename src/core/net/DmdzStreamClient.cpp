#include "DmdzStreamClient.h"

#include "../drawing/DmdzStreamDecoder.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#if defined(ESP32)
#include <Arduino.h>
#include "../Logger.h"
#include "../NetworkBudget.h"
#include "SecureHttpClient.h"
#include "ShowcaseDiscovery.h"
#include <esp_heap_caps.h>
#if !defined(UNIT_TEST)
#include <WiFi.h>
#include <errno.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#endif
#endif

namespace net {

namespace {

void copySafeLocalStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0U) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1U);
    dst[cap - 1U] = '\0';
}

#if defined(ESP32) && !defined(UNIT_TEST)
inline size_t streamClientFree8BitHeap() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void formatRandomWsKeyBase64(char outKey[25]) {
    static constexpr char kB64Table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t nonce[16];
    for (int w = 0; w < 4; ++w) {
        const uint32_t rnd = esp_random();
        nonce[w * 4 + 0] = static_cast<uint8_t>(rnd & 0xFFU);
        nonce[w * 4 + 1] = static_cast<uint8_t>((rnd >> 8) & 0xFFU);
        nonce[w * 4 + 2] = static_cast<uint8_t>((rnd >> 16) & 0xFFU);
        nonce[w * 4 + 3] = static_cast<uint8_t>((rnd >> 24) & 0xFFU);
    }
    size_t o = 0U;
    for (int i = 0; i < 15; i += 3) {
        const uint32_t v = (static_cast<uint32_t>(nonce[i]) << 16) |
                           (static_cast<uint32_t>(nonce[i + 1]) << 8) |
                           static_cast<uint32_t>(nonce[i + 2]);
        outKey[o++] = kB64Table[(v >> 18) & 0x3FU];
        outKey[o++] = kB64Table[(v >> 12) & 0x3FU];
        outKey[o++] = kB64Table[(v >> 6) & 0x3FU];
        outKey[o++] = kB64Table[v & 0x3FU];
    }
    const uint32_t last = static_cast<uint32_t>(nonce[15]) << 16;
    outKey[o++] = kB64Table[(last >> 18) & 0x3FU];
    outKey[o++] = kB64Table[(last >> 12) & 0x3FU];
    outKey[o++] = '=';
    outKey[o++] = '=';
    outKey[o] = '\0';
}

const char* stopReasonLabel(StreamStopReason r) {
    switch (r) {
        case StreamStopReason::Deactivated:   return "deactivated";
        case StreamStopReason::WebUiYield:    return "webui";
        case StreamStopReason::ConfigChanged: return "cfg";
        default:                              return "eof";
    }
}

bool resolveShowcaseTargetAddr(
    const char* serverUrl,
    const char* streamPath,
    char* outHost,
    size_t hostCap,
    uint16_t& outPort,
    const char*& outRawPath,
    struct sockaddr_in& outAddr
) {
    outHost[0] = '\0';
    outPort = 8890U;
    outRawPath = streamPath;
    if (!serverUrl || !streamPath || streamPath[0] == '\0') {
        return false;
    }

    if (strncasecmp(streamPath, "http://", 7) == 0 ||
        strncasecmp(streamPath, "https://", 8) == 0) {
        if (!parseEndpointHostAndPort(streamPath, 8890U, outHost, hostCap, outPort)) {
            return false;
        }
        if (!ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, outPort, true)) {
            return false;
        }
        const char* afterScheme = strstr(streamPath, "://");
        const char* slash = afterScheme ? strchr(afterScheme + 3, '/') : nullptr;
        outRawPath = slash ? slash : "/";
    } else {
        char baseServerUrl[112] = "";
        bool resolvedOnline = false;
        if (!ShowcaseDiscovery::resolveUrl(
                serverUrl,
                baseServerUrl,
                sizeof(baseServerUrl),
                outHost,
                hostCap,
                outPort,
                resolvedOnline,
                nullptr,
                0U,
                true,
                false) ||
            ShowcaseDiscovery::isAutoDiscoverableHost(outHost)) {
            return false;
        }
    }

    memset(&outAddr, 0, sizeof(outAddr));
    outAddr.sin_family = AF_INET;
    outAddr.sin_port = htons(outPort);
    if (inet_aton(outHost, &outAddr.sin_addr) == 0) {
        IPAddress resolvedIp;
        if (streamClientFree8BitHeap() < 5120U || !WiFi.hostByName(outHost, resolvedIp)) {
            return false;
        }
        outAddr.sin_addr.s_addr = static_cast<uint32_t>(resolvedIp);
    }
    return true;
}

bool sendAllSocketBytes(
    int fd,
    const void* rawBuf,
    size_t totalLen,
    int& outErrno,
    StreamStopReason (*checkStop)(void*),
    void* userCtx,
    uint32_t maxSendMs = 1500U
) {
    const uint8_t* ptr = static_cast<const uint8_t*>(rawBuf);
    size_t sent = 0U;
    const uint32_t t0 = millis();
    while (sent < totalLen) {
        if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
            return false;
        }
        const int w = lwip_send(fd, ptr + sent, totalLen - sent, 0);
        if (w > 0) {
            sent += static_cast<size_t>(w);
        } else if (w == 0) {
            return false;
        } else {
            outErrno = errno;
            if (outErrno != EAGAIN && outErrno != EWOULDBLOCK && outErrno != EINTR) {
                return false;
            }
            if ((millis() - t0) >= maxSendMs) {
                return false;
            }
        }
    }
    return true;
}

int connectAndReadHttpHeaders(
    const struct sockaddr_in& addr,
    char* hdrScratch,
    size_t scratchCap,
    size_t reqBytes,
    size_t& outScratchUsed,
    size_t& outBodyStart,
    int& outErrno,
    StreamStopReason (*checkStop)(void*),
    void* userCtx
) {
    outScratchUsed = 0U;
    outBodyStart = 0U;
    if (!hdrScratch || scratchCap < 512U || reqBytes == 0U || reqBytes >= scratchCap) {
        return -1;
    }

    int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        outErrno = errno;
        return -1;
    }
    auto abortClose = [&]() {
        struct linger lin = {1, 0};
        lwip_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        lwip_close(fd);
        fd = -1;
    };

    int nodelay = 1;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    const int flags = lwip_fcntl(fd, F_GETFL, 0);
    lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    const int rc = lwip_connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        outErrno = errno;
        abortClose();
        return -1;
    }
    if (rc != 0) {
        const uint32_t connT0 = millis();
        bool connected = false;
        while ((millis() - connT0) < 1200U &&
               (!checkStop || checkStop(userCtx) == StreamStopReason::Continue)) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv = {0, 40000};
            const int sel = lwip_select(fd + 1, nullptr, &wfds, nullptr, &tv);
            if (sel > 0 && FD_ISSET(fd, &wfds)) {
                int soErr = -1;
                socklen_t errLen = sizeof(soErr);
                if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &errLen) == 0 && soErr == 0) {
                    connected = true;
                }
                break;
            }
        }
        if (!connected) {
            abortClose();
            return -1;
        }
    }

    lwip_fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval rcvTv = {0, 35000};
    struct timeval sndTv = {0, 120000};
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvTv, sizeof(rcvTv));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sndTv, sizeof(sndTv));

    if (!sendAllSocketBytes(fd, hdrScratch, reqBytes, outErrno, checkStop, userCtx, 1500U)) {
        abortClose();
        return -1;
    }
    hdrScratch[0] = '\0';

    bool foundHeaderEnd = false;
    const uint32_t hdrT0 = millis();
    while (!foundHeaderEnd &&
           (millis() - hdrT0) < 3000U &&
           (!checkStop || checkStop(userCtx) == StreamStopReason::Continue)) {
        if (outScratchUsed >= scratchCap - 1U) {
            break;
        }
        const int r = lwip_recv(fd, hdrScratch + outScratchUsed, scratchCap - 1U - outScratchUsed, 0);
        if (r > 0) {
            outScratchUsed += static_cast<size_t>(r);
            hdrScratch[outScratchUsed] = '\0';
            char* sep = strstr(hdrScratch, "\r\n\r\n");
            if (sep != nullptr) {
                foundHeaderEnd = true;
                outBodyStart = static_cast<size_t>((sep - hdrScratch) + 4);
                *sep = '\0';
            }
        } else if (r == 0) {
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            outErrno = errno;
            break;
        }
    }
    if (!foundHeaderEnd) {
        abortClose();
        return -1;
    }
    return fd;
}

inline size_t minFreeForSpscCredit(size_t ringCap) {
    return (ringCap >= 4096U)
        ? kSpscWsCreditMinRingFreeBytes
        : std::min<size_t>(kWsCreditMinRingFreeBytes, ringCap / 2U);
}

bool flushSpscWsControlAndCredits(
    DmdzSpscStreamSession& s,
    DmdzSpscByteRing& spscRing,
    bool forceAllCredits,
    StreamStopReason (*checkStop)(void*),
    void* userCtx
) {
    if (!s.isWebSocket || s.fd < 0) {
        return true;
    }
    while (s.wsDemux.hasPendingPong()) {
        uint8_t pongFrame[131];
        const size_t pongLen = s.wsDemux.takePendingPongFrame(pongFrame);
        if (pongLen > 0U &&
            !sendAllSocketBytes(s.fd, pongFrame, pongLen, s.lastSockErrno, checkStop, userCtx, 1000U)) {
            s.lastFailReason = "sock_err";
            return false;
        }
    }
    const size_t minFree = minFreeForSpscCredit(spscRing.capacity());
    while (canFlushWsCredits(
               s.pendingWsCredits,
               s.ackBatch,
               forceAllCredits,
               spscRing.producerSize(),
               spscRing.producerFreeBytes(),
               spscRing.capacity(),
               minFree)) {
        const uint8_t batch = (s.pendingWsCredits > 16U) ? 16U : static_cast<uint8_t>(s.pendingWsCredits);
        uint8_t ackFrame[22];
        const size_t ackLen = core::drawing::DmdzWsDemuxer::buildMaskedCreditAckFrame(batch, ackFrame);
        if (ackLen > 0U &&
            !sendAllSocketBytes(s.fd, ackFrame, ackLen, s.lastSockErrno, checkStop, userCtx, 1000U)) {
            s.lastFailReason = "sock_err";
            return false;
        }
        s.pendingWsCredits = static_cast<uint16_t>(s.pendingWsCredits - batch);
    }
    return true;
}

bool feedSpscRawBytes(
    DmdzSpscStreamSession& s,
    DmdzSpscByteRing& spscRing,
    const uint8_t* rawSrc,
    size_t rawLen,
    bool flushCreditsNow,
    StreamStopReason (*checkStop)(void*),
    void* userCtx
) {
    if (s.isWebSocket) {
        size_t off = 0U;
        while (off < rawLen && !s.streamEof) {
            const size_t contig = spscRing.contiguousWriteCap();
            if (contig == 0U) {
                s.lastFailReason = "ring_full";
                return false;
            }
            size_t consumed = 0U;
            uint8_t creditsToAck = 0U;
            const size_t produced = s.wsDemux.decode(
                rawSrc + off,
                rawLen - off,
                consumed,
                spscRing.writePtr(),
                contig,
                creditsToAck
            );
            off += consumed;
            if (produced > 0U) {
                spscRing.commitWrite(produced);
            }
            s.pendingWsCredits = static_cast<uint16_t>(s.pendingWsCredits + creditsToAck);
            if (s.wsDemux.hasError()) {
                s.lastFailReason = "ws_err";
                return false;
            }
            if (flushCreditsNow && s.wsDemux.hasPendingPong() &&
                !flushSpscWsControlAndCredits(s, spscRing, false, checkStop, userCtx)) {
                return false;
            }
            if (s.wsDemux.isClosed()) {
                s.streamEof = true;
                break;
            }
            if (consumed == 0U && produced == 0U) {
                s.lastFailReason = "ws_err";
                return false;
            }
        }
        return flushCreditsNow ? flushSpscWsControlAndCredits(s, spscRing, false, checkStop, userCtx) : true;
    }

    if (s.isChunked) {
        size_t off = 0U;
        while (off < rawLen && !s.streamEof) {
            const size_t contig = spscRing.contiguousWriteCap();
            if (contig == 0U) {
                s.lastFailReason = "ring_full";
                return false;
            }
            size_t consumed = 0U;
            const size_t produced = s.chunkDec.decode(
                rawSrc + off,
                rawLen - off,
                consumed,
                spscRing.writePtr(),
                contig
            );
            off += consumed;
            if (produced > 0U) {
                spscRing.commitWrite(produced);
            }
            if (s.chunkDec.hasError()) {
                s.lastFailReason = "chunk_err";
                return false;
            }
            if (s.chunkDec.isDone()) {
                s.streamEof = true;
                break;
            }
            if (consumed == 0U && produced == 0U) {
                s.lastFailReason = "chunk_err";
                return false;
            }
        }
        return true;
    }

    spscRing.push(rawSrc, rawLen);
    return true;
}

#if defined(BOARD_HAS_PSRAM)
bool parseContentLengthHeader(const char* rawHeaders, size_t& outLen) {
    outLen = 0U;
    if (!rawHeaders) return false;
    for (const char* p = rawHeaders; *p != '\0'; ++p) {
        if ((p == rawHeaders || *(p - 1) == '\n') &&
            (*p == 'C' || *p == 'c') &&
            strncasecmp(p, "content-length:", 15) == 0) {
            const char* v = p + 15;
            while (*v == ' ' || *v == '\t') ++v;
            if (*v < '0' || *v > '9') return false;
            unsigned long parsed = strtoul(v, nullptr, 10);
            outLen = static_cast<size_t>(parsed);
            return true;
        }
    }
    return false;
}
#endif
#endif // defined(ESP32) && !defined(UNIT_TEST)

#if defined(ESP32) && defined(UNIT_TEST)
bool readExactStreamForUnitTest(
    Stream& st,
    uint8_t* dst,
    size_t len,
    StreamStopReason (*checkStop)(void*),
    void* userCtx,
    uint32_t timeoutMs = 2500U
) {
    size_t got = 0U;
    uint32_t lastProgress = millis();
    while (got < len) {
        if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
            return false;
        }
        int avail = st.available();
        if (avail > 0) {
            size_t toRead = len - got;
            if (static_cast<size_t>(avail) < toRead) toRead = static_cast<size_t>(avail);
            size_t r = st.readBytes(dst + got, toRead);
            if (r > 0U) {
                got += r;
                lastProgress = millis();
                continue;
            }
        }
        int c = st.read();
        if (c >= 0) {
            dst[got++] = static_cast<uint8_t>(c);
            lastProgress = millis();
        } else if ((millis() - lastProgress) > timeoutMs) {
            return false;
        }
    }
    return true;
}
#endif

} // namespace

bool formatDmdzHttpStreamPath(
    const char* rawPath,
    const char* clientId,
    bool purge,
    bool paced,
    const char* profile,
    char* outPath,
    size_t outCap
) {
    if (!outPath || outCap == 0U) {
        return false;
    }
    outPath[0] = '\0';
    if (!rawPath || rawPath[0] == '\0') {
        return false;
    }

    const char* pathStart = rawPath;
    if (strncasecmp(rawPath, "http://", 7) == 0 || strncasecmp(rawPath, "https://", 8) == 0) {
        const char* afterScheme = strstr(rawPath, "://");
        const char* slash = afterScheme ? strchr(afterScheme + 3, '/') : nullptr;
        if (!slash) {
            return false;
        }
        pathStart = slash;
    }

    const char* qMark = strchr(pathStart, '?');
    const size_t baseLen = qMark ? static_cast<size_t>(qMark - pathStart) : strlen(pathStart);
    if (baseLen == 0U || baseLen >= outCap) {
        return false;
    }

    char apiKeyParam[80] = "";
    if (qMark && qMark[1] != '\0') {
        const char* ak = strstr(qMark + 1, "api_key=");
        if (ak != nullptr) {
            const char* akEnd = strchr(ak, '&');
            const size_t akLen = akEnd ? static_cast<size_t>(akEnd - ak) : strlen(ak);
            if (akLen > 8U && akLen + 2U < sizeof(apiKeyParam)) {
                apiKeyParam[0] = '&';
                memcpy(apiKeyParam + 1, ak, akLen);
                apiKeyParam[akLen + 1] = '\0';
            }
        }
    }

    char clientParam[64] = "";
    if (clientId && clientId[0] != '\0') {
        const int cn = snprintf(clientParam, sizeof(clientParam), "&client_id=%s", clientId);
        if (cn <= 0 || static_cast<size_t>(cn) >= sizeof(clientParam)) {
            clientParam[0] = '\0';
        }
    }

    char profileParam[32] = "";
    if (profile && profile[0] != '\0') {
        const int pn = snprintf(profileParam, sizeof(profileParam), "&profile=%s", profile);
        if (pn <= 0 || static_cast<size_t>(pn) >= sizeof(profileParam)) {
            profileParam[0] = '\0';
        }
    }

    const int n = snprintf(
        outPath,
        outCap,
        "%.*s?purge=%s&paced=%u&codec=dmdz2%s%s%s",
        static_cast<int>(baseLen),
        pathStart,
        purge ? "true" : "false",
        paced ? 1U : 0U,
        clientParam,
        apiKeyParam,
        profileParam
    );
    if (n <= 0 || static_cast<size_t>(n) >= outCap) {
        outPath[0] = '\0';
        return false;
    }
    return true;
}

size_t HttpChunkedDecoder::decode(
    const uint8_t* src,
    size_t srcLen,
    size_t& outSrcConsumed,
    uint8_t* dst,
    size_t dstCap
) {
    outSrcConsumed = 0U;
    if (!src || srcLen == 0U || !dst || dstCap == 0U) {
        return 0U;
    }

    size_t written = 0U;
    while (outSrcConsumed < srcLen && written < dstCap && state != State::Done && state != State::Error) {
        if (state == State::Payload) {
            size_t avail = srcLen - outSrcConsumed;
            size_t space = dstCap - written;
            size_t take = (avail < chunkRem) ? avail : chunkRem;
            if (take > space) take = space;
            memcpy(dst + written, src + outSrcConsumed, take);
            written += take;
            outSrcConsumed += take;
            chunkRem -= take;
            if (chunkRem == 0U) {
                state = State::PayloadCr;
            }
            continue;
        }

        uint8_t b = src[outSrcConsumed++];
        switch (state) {
            case State::SizeHex: {
                if (hexDigits == 0U && (b == ' ' || b == '\t')) {
                    break;
                }
                int nib = -1;
                if (b >= '0' && b <= '9') nib = static_cast<int>(b - '0');
                else if (b >= 'a' && b <= 'f') nib = static_cast<int>(b - 'a' + 10);
                else if (b >= 'A' && b <= 'F') nib = static_cast<int>(b - 'A' + 10);

                if (nib >= 0) {
                    if (hexDigits >= 8U) {
                        state = State::Error;
                        break;
                    }
                    chunkRem = (chunkRem << 4) | static_cast<size_t>(nib);
                    ++hexDigits;
                } else if (b == ';' && hexDigits > 0U) {
                    state = State::SizeExt;
                } else if (b == '\r' && hexDigits > 0U) {
                    state = State::SizeLf;
                } else if (b == '\n' && hexDigits > 0U) {
                    state = (chunkRem == 0U) ? State::Done : State::Payload;
                } else {
                    state = State::Error;
                }
                break;
            }
            case State::SizeExt: {
                if (b == '\r') {
                    state = State::SizeLf;
                } else if (b == '\n') {
                    state = (chunkRem == 0U) ? State::Done : State::Payload;
                }
                break;
            }
            case State::SizeLf: {
                if (b == '\n') {
                    state = (chunkRem == 0U) ? State::Done : State::Payload;
                } else {
                    state = State::Error;
                }
                break;
            }
            case State::PayloadCr: {
                if (b == '\r') {
                    state = State::PayloadLf;
                } else if (b == '\n') {
                    chunkRem = 0U;
                    hexDigits = 0U;
                    state = State::SizeHex;
                } else {
                    state = State::Error;
                }
                break;
            }
            case State::PayloadLf: {
                if (b == '\n') {
                    chunkRem = 0U;
                    hexDigits = 0U;
                    state = State::SizeHex;
                } else {
                    state = State::Error;
                }
                break;
            }
            default:
                break;
        }
    }
    return written;
}

bool hasChunkedTransferEncoding(const char* rawHeaders) {
    if (!rawHeaders) return false;
    for (const char* p = rawHeaders; *p != '\0'; ++p) {
        if ((*p == 'T' || *p == 't') && strncasecmp(p, "transfer-encoding:", 18) == 0) {
            const char* lineEnd = strpbrk(p + 18, "\r\n");
            size_t lineLen = lineEnd ? static_cast<size_t>(lineEnd - (p + 18)) : strlen(p + 18);
            for (size_t i = 0U; i + 7U <= lineLen; ++i) {
                if (strncasecmp(p + 18 + i, "chunked", 7) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool parseEndpointHostAndPort(
    const char* rawUrl,
    uint16_t defaultPort,
    char* outHost,
    size_t hostCap,
    uint16_t& outPort,
    bool* outIsHttps
) {
    if (outIsHttps) *outIsHttps = false;
    if (!rawUrl || !outHost || hostCap == 0U) return false;
    while (*rawUrl == ' ') ++rawUrl;
    if (*rawUrl == '\0') return false;

    const char* p = rawUrl;
    if (strncmp(p, "https://", 8) == 0) {
        if (outIsHttps) *outIsHttps = true;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "mqtt://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "tcp://", 6) == 0) {
        p += 6;
    }

    const char* slash = strchr(p, '/');
    size_t authLen = slash ? static_cast<size_t>(slash - p) : strlen(p);
    if (authLen == 0U) return false;

    char auth[96];
    size_t copyLen = (authLen < sizeof(auth) - 1U) ? authLen : (sizeof(auth) - 1U);
    memcpy(auth, p, copyLen);
    auth[copyLen] = '\0';

    outPort = defaultPort;
    char* colon = strrchr(auth, ':');
    if (colon) {
        *colon = '\0';
        int parsed = atoi(colon + 1);
        if (parsed > 0 && parsed <= 65535) {
            outPort = static_cast<uint16_t>(parsed);
        }
    }
    if (auth[0] == '\0') return false;
    copySafeLocalStr(outHost, hostCap, auth);
    return true;
}

void DmdzSpscStreamSession::close(bool sendGracefulWsClose) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (fd >= 0) {
        if (sendGracefulWsClose && isWebSocket) {
            int dummyErr = 0;
            (void)sendAllSocketBytes(
                fd,
                core::drawing::DmdzWsDemuxer::kMaskedCloseFrame,
                sizeof(core::drawing::DmdzWsDemuxer::kMaskedCloseFrame),
                dummyErr,
                nullptr,
                nullptr,
                200U
            );
        }
        struct linger lin = {1, 0};
        lwip_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        lwip_close(fd);
        fd = -1;
    }
#else
    (void)sendGracefulWsClose;
    fd = -1;
#endif
}

bool DmdzSpscStreamSession::open(
    const char* serverUrl,
    const char* streamPath,
    const char* clientId,
    const char* profile,
    uint8_t wsCredits,
    uint16_t wsChunkBytes,
    DmdzSpscByteRing& spscRing,
    StreamStopReason (*checkStop)(void*),
    void* userCtx,
    const char* logTag
) {
    close(false);
    pendingWsCredits = 0U;
    isWebSocket = false;
    isChunked = false;
    serverPaced = false;
    streamEof = false;
    lastSockErrno = 0;
    lastFailReason = "open_err";
    header = core::drawing::DmdzStreamHeader{};
    wsDemux.reset();
    chunkDec.reset();

    if (!spscRing.buf || spscRing.capacity() < 1024U) {
        return false;
    }
    if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
        lastFailReason = "stop";
        return false;
    }

#if defined(ESP32) && !defined(UNIT_TEST)
    const char* tag = (logTag && logTag[0] != '\0') ? logTag : "Showcase";
    const char* rawPath = streamPath;
    struct sockaddr_in addr;
    if (!resolveShowcaseTargetAddr(
            serverUrl,
            streamPath,
            resolvedHost,
            sizeof(resolvedHost),
            resolvedPort,
            rawPath,
            addr)) {
        return false;
    }

    const uint8_t clampedCredits = clampWsCredits(wsCredits);
    ackBatch = computeWsAckBatch(clampedCredits);
    const uint16_t clampedChunk = (wsChunkBytes >= 256U)
        ? wsChunkBytes
        : ((spscRing.capacity() >= 4608U) ? 1360U : 1024U);

    // Use up to 2 KB of `spscRing.buf` as HTTP/101 header scratch BEFORE publishing to consumer.
    char* hdrScratch = reinterpret_cast<char*>(spscRing.buf);
    const size_t hdrScratchCap = std::min<size_t>(spscRing.capacity(), 2048U);
    size_t scratchUsed = 0U;
    size_t bodyStart = 0U;

    if (core::drawing::buildDmdzWebSocketPath(
            rawPath,
            clientId,
            activePath,
            sizeof(activePath),
            clampedCredits,
            2U,
            clampedChunk,
            profile)) {
        char wsKey[25];
        formatRandomWsKeyBase64(wsKey);
        const int wsReqLen = snprintf(
            hdrScratch,
            hdrScratchCap,
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%u\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n",
            activePath,
            resolvedHost,
            static_cast<unsigned>(resolvedPort),
            wsKey
        );
        if (wsReqLen > 0 && static_cast<size_t>(wsReqLen) < hdrScratchCap) {
            fd = connectAndReadHttpHeaders(
                addr,
                hdrScratch,
                hdrScratchCap,
                static_cast<size_t>(wsReqLen),
                scratchUsed,
                bodyStart,
                lastSockErrno,
                checkStop,
                userCtx
            );
            if (fd >= 0 && strncmp(hdrScratch, "HTTP/1.1 101", 12) == 0) {
                isWebSocket = true;
                serverPaced = true;
                wsDemux.reset();
            } else {
                close(false);
                if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
                    return false;
                }
            }
        }
    }

    if (!isWebSocket) {
        const bool wantPurge = (strstr(rawPath, "purge=false") == nullptr);
        if (!formatDmdzHttpStreamPath(rawPath, clientId, wantPurge, true, profile, activePath, sizeof(activePath))) {
            return false;
        }
        const int reqLen = snprintf(
            hdrScratch,
            hdrScratchCap,
            "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
            activePath,
            resolvedHost,
            static_cast<unsigned>(resolvedPort)
        );
        if (reqLen <= 0 || static_cast<size_t>(reqLen) >= hdrScratchCap) {
            return false;
        }
        fd = connectAndReadHttpHeaders(
            addr,
            hdrScratch,
            hdrScratchCap,
            static_cast<size_t>(reqLen),
            scratchUsed,
            bodyStart,
            lastSockErrno,
            checkStop,
            userCtx
        );
        if (fd < 0 ||
            (strncmp(hdrScratch, "HTTP/1.1 200", 12) != 0 && strncmp(hdrScratch, "HTTP/1.0 200", 12) != 0)) {
            LOGW(tag, "SPSC stream HTTP rejected: path=%s status=%.40s", activePath, hdrScratch);
            close(false);
            return false;
        }
        isChunked = hasChunkedTransferEncoding(hdrScratch);
        serverPaced = true;
    }

    ShowcaseDiscovery::touchVerifiedHost(resolvedHost, resolvedPort);

    const size_t initialRawLen = (scratchUsed > bodyStart) ? (scratchUsed - bodyStart) : 0U;
    const size_t tailStashStart = spscRing.capacity() - initialRawLen;
    if (initialRawLen > 0U) {
        memmove(spscRing.buf + tailStashStart, hdrScratch + bodyStart, initialRawLen);
    }
    spscRing.reset();

    size_t rawOff = 0U;
    while (rawOff < initialRawLen && !streamEof) {
        const size_t rem = initialRawLen - rawOff;
        const size_t step = (rem < sizeof(rawRxBuf)) ? rem : sizeof(rawRxBuf);
        memcpy(rawRxBuf, spscRing.buf + tailStashStart + rawOff, step);
        rawOff += step;
        if (!feedSpscRawBytes(*this, spscRing, rawRxBuf, step, false, checkStop, userCtx)) {
            close(false);
            return false;
        }
    }

    const uint32_t hdrWaitT0 = millis();
    while (spscRing.consumerSize() < 16U) {
        if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
            close(false);
            return false;
        }
        if (streamEof || (millis() - hdrWaitT0) >= 3000U) {
            close(false);
            return false;
        }
        if (!pump(spscRing, 20U, checkStop, userCtx)) {
            close(false);
            return false;
        }
    }

    uint8_t rawHdr16[16] = {0};
    spscRing.pop(rawHdr16, 16U);
    if (!core::drawing::DmdzStreamDecoder::parseHeader(rawHdr16, header)) {
        LOGW(tag, "SPSC stream bad header magic (proto=%s path=%s)", isWebSocket ? "ws" : "http", activePath);
        close(false);
        return false;
    }
    return true;
#else
    (void)serverUrl;
    (void)streamPath;
    (void)clientId;
    (void)profile;
    (void)wsCredits;
    (void)wsChunkBytes;
    (void)logTag;
    return false;
#endif
}

bool DmdzSpscStreamSession::pump(
    DmdzSpscByteRing& spscRing,
    uint32_t maxWaitMs,
    StreamStopReason (*checkStop)(void*),
    void* userCtx
) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (fd < 0) {
        return streamEof;
    }

    for (int pass = 0; pass < 2; ++pass) {
        if (!flushSpscWsControlAndCredits(*this, spscRing, pass > 0, checkStop, userCtx)) {
            return false;
        }
        while (!streamEof && spscRing.producerFreeBytes() > 0U) {
            if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
                lastFailReason = "stop";
                return false;
            }
            if (!isWebSocket && !isChunked) {
                const size_t contig = spscRing.contiguousWriteCap();
                const size_t toRead = (contig < 1024U) ? contig : 1024U;
                const int r = lwip_recv(fd, spscRing.writePtr(), toRead, MSG_DONTWAIT);
                if (r > 0) {
                    spscRing.commitWrite(static_cast<size_t>(r));
                    continue;
                }
                if (r == 0) {
                    streamEof = true;
                    break;
                }
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                lastSockErrno = errno;
                lastFailReason = "sock_err";
                return false;
            }

            const size_t space = spscRing.producerFreeBytes();
            if (isWebSocket && space < 64U) {
                break;
            }
            const size_t maxPayload = isWebSocket ? (space - 16U) : space;
            const size_t toRead = (maxPayload < sizeof(rawRxBuf)) ? maxPayload : sizeof(rawRxBuf);
            if (toRead == 0U) {
                break;
            }
            const int r = lwip_recv(fd, rawRxBuf, toRead, MSG_DONTWAIT);
            if (r > 0) {
                if (!feedSpscRawBytes(*this, spscRing, rawRxBuf, static_cast<size_t>(r), true, checkStop, userCtx)) {
                    return false;
                }
                continue;
            }
            if (r == 0) {
                streamEof = true;
                break;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            lastSockErrno = errno;
            lastFailReason = "sock_err";
            return false;
        }

        if (isWebSocket && pendingWsCredits > 0U &&
            !flushSpscWsControlAndCredits(*this, spscRing, true, checkStop, userCtx)) {
            return false;
        }
        if (pass > 0 || maxWaitMs == 0U || streamEof || spscRing.producerFreeBytes() < 64U) {
            break;
        }
        if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
            lastFailReason = "stop";
            return false;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv;
        tv.tv_sec = static_cast<long>(maxWaitMs / 1000U);
        tv.tv_usec = static_cast<long>((maxWaitMs % 1000U) * 1000U);
        const int sel = lwip_select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (errno == EINTR) {
                break;
            }
            lastSockErrno = errno;
            lastFailReason = "sock_err";
            return false;
        }
        if (sel == 0 || !FD_ISSET(fd, &rfds)) {
            break;
        }
    }
    return true;
#else
    (void)spscRing;
    (void)maxWaitMs;
    (void)checkStop;
    (void)userCtx;
    return false;
#endif
}

bool DmdzStreamClient::stream(const DmdzStreamParams& params) {
    struct DueResetGuard {
        void (*fn)(void*, uint32_t);
        void* ctx;
        DueResetGuard(void (*cb)(void*, uint32_t), void* c) : fn(cb), ctx(c) {
            if (fn) {
                fn(ctx, 0U);
            }
        }
        ~DueResetGuard() {
            if (fn) {
                fn(ctx, 0U);
            }
        }
    } dueResetGuard{params.onNextFrameDueMs, params.userCtx};

    if (!params.serverUrl || !params.streamPath || params.streamPath[0] == '\0' ||
        !params.frameBuf0 || params.maxFramePixels == 0U) {
        return false;
    }

#if defined(ESP32) && !defined(UNIT_TEST)
    if (!params.ioScratch || params.ioScratchCap < 1024U) {
        return false;
    }
    auto checkStop = [&]() -> StreamStopReason {
        return params.checkStop ? params.checkStop(params.userCtx) : StreamStopReason::Continue;
    };
    if (checkStop() != StreamStopReason::Continue) {
        return false;
    }

    NetworkBudget::ScopedDmdNetActivity streamAct(NetworkBudget::DmdNetActivity::Stream);
    const char* logTag = (params.logTag && params.logTag[0] != '\0') ? params.logTag : "Showcase";

    DmdzSpscByteRing rxRing;
    rxRing.init(reinterpret_cast<uint8_t*>(params.ioScratch), params.ioScratchCap);
    DmdzSpscStreamSession sess;
    if (!sess.open(
            params.serverUrl,
            params.streamPath,
            params.clientId,
            params.profile,
            params.wsCredits,
            params.wsChunkBytes,
            rxRing,
            params.checkStop,
            params.userCtx,
            logTag)) {
        return false;
    }

    const core::drawing::DmdzStreamHeader& parsedHdr = sess.header;
    const uint16_t w = parsedHdr.width;
    const uint16_t h = parsedHdr.height;
    const uint16_t totalFrames = parsedHdr.totalFrames;
    const size_t framePixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t frameBytes = framePixels * sizeof(uint16_t);
    if (frameBytes == 0U || framePixels > params.maxFramePixels || totalFrames == 0U) {
        sess.close(false);
        return false;
    }

    core::drawing::DmdzStreamDecoder dmdzDec;
    if (parsedHdr.isDmdz) {
        dmdzDec.init(w, h, parsedHdr.version);
    }

    const bool singleBuffer = (params.frameBuf1 == nullptr);
    const uint32_t safeFps = std::max<uint32_t>(1U, static_cast<uint32_t>(parsedHdr.fps));
    const uint32_t headerExpectedMs = (static_cast<uint32_t>(totalFrames) * 1000U) / safeFps;
    const uint32_t maxAllowedMs = std::min<uint32_t>(
        300000U,
        std::max<uint32_t>(params.configuredStreamMs + 4000U, (headerExpectedMs * 2U) + 4500U)
    );
    const uint32_t streamStartMs = millis();
    const size_t minFreeCredit = minFreeForSpscCredit(rxRing.capacity());
    const size_t safePrebufCap = (rxRing.capacity() > (minFreeCredit + 64U))
        ? (rxRing.capacity() - minFreeCredit - 64U)
        : (rxRing.capacity() / 2U);

    const char* lastReadFailReason = "idle_body";
    auto ensureRingBytes = [&](size_t minBytes, uint32_t maxIdleMs) -> bool {
        if (minBytes > rxRing.capacity()) {
            minBytes = rxRing.capacity();
        }
        uint32_t lastProg = millis();
        while (rxRing.consumerSize() < minBytes) {
            const StreamStopReason sr = checkStop();
            if (sr != StreamStopReason::Continue) {
                lastReadFailReason = stopReasonLabel(sr);
                return false;
            }
            const uint32_t elapsed = millis() - lastProg;
            if (elapsed >= maxIdleMs) {
                lastReadFailReason = "idle_body";
                return false;
            }
            const size_t before = rxRing.consumerSize();
            const uint32_t waitMs = std::min<uint32_t>(20U, maxIdleMs - elapsed);
            if (!sess.pump(rxRing, waitMs, params.checkStop, params.userCtx)) {
                lastReadFailReason = sess.lastFailReason;
                return false;
            }
            if (rxRing.consumerSize() > before) {
                lastProg = millis();
            } else if (sess.streamEof && rxRing.consumerSize() < minBytes) {
                lastReadFailReason = "eof_early";
                return false;
            }
        }
        return true;
    };

    auto readExactBody = [&](uint8_t* dst, size_t wantLen, uint32_t maxIdleMs) -> bool {
        size_t got = 0U;
        while (got < wantLen) {
            if (rxRing.consumerSize() == 0U && !ensureRingBytes(1U, maxIdleMs)) {
                return false;
            }
            got += rxRing.pop(dst + got, wantLen - got);
        }
        return true;
    };

    uint32_t scheduleT0 = 0U;
    uint32_t publishedOnClock = 0U;
    const char* endReason = "eof";
    size_t minFree8 = streamClientFree8BitHeap();
    uint32_t lateFrames = 0U;
    uint16_t framesRead = 0U;

    auto dueMsFor = [&](uint32_t k) -> uint32_t {
        return scheduleT0 + ((k * 1000U) / safeFps);
    };

    for (uint16_t f = 0U; f < totalFrames; ++f) {
        {
            const StreamStopReason sr = checkStop();
            if (sr != StreamStopReason::Continue) {
                endReason = stopReasonLabel(sr);
                break;
            }
        }
        if ((millis() - streamStartMs) >= maxAllowedMs) {
            endReason = "cap";
            break;
        }

        if (publishedOnClock > 0U) {
            const uint32_t due = dueMsFor(publishedOnClock);
            if (params.onNextFrameDueMs) {
                params.onNextFrameDueMs(params.userCtx, (due != 0U) ? due : 1U);
            }
            bool abortPacing = false;
            while (true) {
                const StreamStopReason sr = checkStop();
                if (sr != StreamStopReason::Continue) {
                    endReason = stopReasonLabel(sr);
                    abortPacing = true;
                    break;
                }
                if ((millis() - streamStartMs) >= maxAllowedMs) {
                    endReason = "cap";
                    abortPacing = true;
                    break;
                }
                const int32_t remMs = static_cast<int32_t>(due - millis());
                if (remMs <= 0) {
                    break;
                }
                const uint32_t sliceMs = (remMs > 10) ? 10U : static_cast<uint32_t>(remMs);
                if (!sess.streamEof && rxRing.producerFreeBytes() >= 64U) {
                    if (!sess.pump(rxRing, sliceMs, params.checkStop, params.userCtx)) {
                        endReason = sess.lastFailReason;
                        abortPacing = true;
                        break;
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(sliceMs > 1U ? (sliceMs - 1U) : 1U));
                }
            }
            if (abortPacing) {
                break;
            }
        }

        if (parsedHdr.isDmdz) {
            uint16_t nextPayloadLen = 0U;
            if (!ensureRingBytes(2U, 5000U) || !rxRing.peekFrameLength(nextPayloadLen)) {
                endReason = lastReadFailReason;
                break;
            }
            const size_t wantPrebuf = std::min<size_t>(
                static_cast<size_t>(2U) + static_cast<size_t>(nextPayloadLen),
                safePrebufCap
            );
            if (!ensureRingBytes(wantPrebuf, 5000U)) {
                endReason = lastReadFailReason;
                break;
            }
        } else {
            if (!ensureRingBytes(std::min<size_t>(frameBytes, safePrebufCap), 5000U)) {
                endReason = lastReadFailReason;
                break;
            }
        }

        uint16_t* dstBuf = singleBuffer
            ? params.frameBuf0
            : (((f & 1U) == 0U) ? params.frameBuf0 : params.frameBuf1);

        if (params.onBeginFrameWrite) {
            params.onBeginFrameWrite(params.userCtx, f, dstBuf, singleBuffer, safeFps);
        }

        bool okFrame = false;
        const char* frameFailReason = "idle_body";
        if (parsedHdr.isDmdz) {
            if (!singleBuffer && f > 0U) {
                const uint16_t* prevBuf = ((f & 1U) == 0U) ? params.frameBuf1 : params.frameBuf0;
                memcpy(dstBuf, prevBuf, frameBytes);
            }
            uint8_t lenBytes[2] = {0U, 0U};
            if (!readExactBody(lenBytes, 2U, 5000U)) {
                frameFailReason = lastReadFailReason;
            } else {
                const uint16_t payloadLen = static_cast<uint16_t>(lenBytes[0]) |
                                            (static_cast<uint16_t>(lenBytes[1]) << 8);
                if (!dmdzDec.beginFrame(payloadLen)) {
                    frameFailReason = "decode_err";
                } else {
                    size_t rem = payloadLen;
                    okFrame = true;
                    while (rem > 0U) {
                        if (rxRing.consumerSize() == 0U && !ensureRingBytes(1U, 5000U)) {
                            frameFailReason = lastReadFailReason;
                            okFrame = false;
                            break;
                        }
                        const size_t contig = rxRing.contiguousReadCap();
                        const size_t take = (rem < contig) ? rem : contig;
                        if (take == 0U || !dmdzDec.feed(rxRing.readPtr(), take, dstBuf)) {
                            frameFailReason = "decode_err";
                            okFrame = false;
                            break;
                        }
                        rxRing.consumeRead(take);
                        rem -= take;
                    }
                    if (okFrame && !dmdzDec.finishFrame()) {
                        frameFailReason = "decode_err";
                        okFrame = false;
                    }
                }
            }
        } else {
            okFrame = readExactBody(reinterpret_cast<uint8_t*>(dstBuf), frameBytes, 5000U);
            if (!okFrame) {
                frameFailReason = lastReadFailReason;
            }
        }

        if (okFrame) {
            const StreamStopReason srBeforePub = checkStop();
            if (srBeforePub != StreamStopReason::Continue) {
                okFrame = false;
                frameFailReason = stopReasonLabel(srBeforePub);
            } else {
                const uint32_t now = millis();
                if (publishedOnClock == 0U) {
                    scheduleT0 = now;
                } else {
                    const uint32_t due = dueMsFor(publishedOnClock);
                    if (static_cast<int32_t>(now - due) > static_cast<int32_t>(1000U / safeFps)) {
                        scheduleT0 = now - ((publishedOnClock * 1000U) / safeFps);
                        ++lateFrames;
                    }
                }
                if (params.onNextFrameDueMs) {
                    if (static_cast<uint32_t>(f) + 1U < static_cast<uint32_t>(totalFrames)) {
                        const uint32_t nextDue = dueMsFor(publishedOnClock + 1U);
                        params.onNextFrameDueMs(params.userCtx, (nextDue != 0U) ? nextDue : 1U);
                    } else {
                        params.onNextFrameDueMs(params.userCtx, 0U);
                    }
                }
            }
        }

        if (params.onFinishFrameWrite) {
            params.onFinishFrameWrite(params.userCtx, f, dstBuf, w, h, okFrame, singleBuffer);
        }
        if (!okFrame) {
            const StreamStopReason sr = checkStop();
            endReason = (sr != StreamStopReason::Continue) ? stopReasonLabel(sr) : frameFailReason;
            break;
        }

        if (!sess.streamEof && (static_cast<uint32_t>(f) + 1U < static_cast<uint32_t>(totalFrames))) {
            (void)sess.pump(rxRing, 0U, params.checkStop, params.userCtx);
        }

        ++framesRead;
        ++publishedOnClock;
        const size_t free8 = streamClientFree8BitHeap();
        if (free8 < minFree8) minFree8 = free8;
        if ((f & 0x07U) == 0U) {
            taskYIELD();
        }
    }

    sess.close(framesRead == totalFrames);
    if (framesRead > 0U) {
        ShowcaseDiscovery::touchVerifiedHost(sess.resolvedHost, sess.resolvedPort);
        if (params.getPresentedFrames) {
            const uint32_t tailWaitBudgetMs = std::min<uint32_t>(16U, 1000U / safeFps);
            const uint32_t tailT0 = millis();
            while (checkStop() == StreamStopReason::Continue &&
                   params.getPresentedFrames(params.userCtx) < static_cast<uint32_t>(framesRead) &&
                   (millis() - tailT0) < tailWaitBudgetMs) {
                vTaskDelay(1);
            }
        }
    }
    LOGI(
        logTag,
        "TCP stream done (%s, errno=%d): proto=%s rxFrames=%u/%u late=%u minFree8=%u",
        endReason,
        sess.lastSockErrno,
        sess.isWebSocket ? "ws" : "http",
        static_cast<unsigned>(framesRead),
        static_cast<unsigned>(totalFrames),
        static_cast<unsigned>(lateFrames),
        static_cast<unsigned>(minFree8)
    );
    return (framesRead > 0U);
#elif defined(ESP32) && defined(UNIT_TEST)
    auto checkStop = [&]() -> StreamStopReason {
        return params.checkStop ? params.checkStop(params.userCtx) : StreamStopReason::Continue;
    };
    SecureHttpResponse sResp;
    {
        char fullUrl[320];
        if (strncmp(params.streamPath, "http", 4) == 0) {
            copySafeLocalStr(fullUrl, sizeof(fullUrl), params.streamPath);
        } else {
            snprintf(fullUrl, sizeof(fullUrl), "%s%s", params.serverUrl, params.streamPath);
        }
        SecureHttpOptions httpOpts;
        httpOpts.ownerId = static_cast<HttpOwnerId>(params.ownerId);
        httpOpts.requestTimeoutMs = 4500;
        httpOpts.keepAlive = false;
        sResp = SecureHttpClient::get(fullUrl, httpOpts);
    }
    if (!sResp.ok()) {
        return false;
    }

    Stream& st = sResp.stream();
    uint8_t hdr[16];
    core::drawing::DmdzStreamHeader parsedHdr;
    if (!readExactStreamForUnitTest(st, hdr, sizeof(hdr), params.checkStop, params.userCtx) ||
        !core::drawing::DmdzStreamDecoder::parseHeader(hdr, parsedHdr)) {
        return false;
    }
    const uint16_t w = parsedHdr.width;
    const uint16_t h = parsedHdr.height;
    const uint16_t totalFrames = parsedHdr.totalFrames;
    const size_t framePixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t frameBytes = framePixels * sizeof(uint16_t);
    if (frameBytes == 0U || framePixels > params.maxFramePixels || totalFrames == 0U) {
        return false;
    }

    core::drawing::DmdzStreamDecoder dmdzDec;
    if (parsedHdr.isDmdz) {
        dmdzDec.init(w, h, parsedHdr.version);
    }
    const bool singleBuffer = (params.frameBuf1 == nullptr);
    const uint32_t safeFps = std::max<uint32_t>(1U, static_cast<uint32_t>(parsedHdr.fps));

    uint16_t framesRead = 0U;
    for (uint16_t f = 0U; f < totalFrames; ++f) {
        if (checkStop() != StreamStopReason::Continue) break;
        uint16_t* dstBuf = singleBuffer
            ? params.frameBuf0
            : (((f & 1U) == 0U) ? params.frameBuf0 : params.frameBuf1);
        if (params.onBeginFrameWrite) {
            params.onBeginFrameWrite(params.userCtx, f, dstBuf, singleBuffer, safeFps);
        }
        bool okFrame = false;
        if (parsedHdr.isDmdz) {
            if (!singleBuffer && f > 0U) {
                const uint16_t* prevBuf = ((f & 1U) == 0U) ? params.frameBuf1 : params.frameBuf0;
                memcpy(dstBuf, prevBuf, frameBytes);
            }
            uint8_t lenBytes[2] = {0U, 0U};
            if (readExactStreamForUnitTest(st, lenBytes, 2U, params.checkStop, params.userCtx)) {
                const uint16_t payloadLen = static_cast<uint16_t>(lenBytes[0]) |
                                            (static_cast<uint16_t>(lenBytes[1]) << 8);
                dmdzDec.beginFrame(payloadLen);
                uint8_t decChunk[256];
                size_t rem = payloadLen;
                okFrame = true;
                while (rem > 0U) {
                    size_t take = (rem < sizeof(decChunk)) ? rem : sizeof(decChunk);
                    if (!readExactStreamForUnitTest(st, decChunk, take, params.checkStop, params.userCtx) ||
                        !dmdzDec.feed(decChunk, take, dstBuf)) {
                        okFrame = false;
                        break;
                    }
                    rem -= take;
                }
                if (okFrame && !dmdzDec.finishFrame()) {
                    okFrame = false;
                }
            }
        } else {
            okFrame = readExactStreamForUnitTest(
                st,
                reinterpret_cast<uint8_t*>(dstBuf),
                frameBytes,
                params.checkStop,
                params.userCtx
            );
        }
        if (params.onFinishFrameWrite) {
            params.onFinishFrameWrite(params.userCtx, f, dstBuf, w, h, okFrame, singleBuffer);
        }
        if (!okFrame) break;
        ++framesRead;
    }
    return (framesRead > 0U);
#else
    return false;
#endif
}

void DmdzPreloadSession::closeSocket() {
#if defined(ESP32) && !defined(UNIT_TEST) && defined(BOARD_HAS_PSRAM)
    if (fd >= 0) {
        struct linger lin = {1, 0};
        lwip_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        lwip_close(fd);
        fd = -1;
    }
#else
    fd = -1;
#endif
}

void DmdzPreloadSession::releasePreloadBuffer() {
    closeSocket();
    if (chunkScratch) {
        free(chunkScratch);
        chunkScratch = nullptr;
    }
    if (preloadBuf) {
        free(preloadBuf);
        preloadBuf = nullptr;
    }
    preloadCap = 0U;
}

bool DmdzPreloadSession::open(
    const char* serverUrl,
    const char* streamPath,
    const char* clientId,
    const char* profile,
    size_t maxPreloadBytes,
    std::atomic<size_t>& outBytesDownloaded,
    std::atomic<bool>& outDownloadComplete,
    StreamStopReason (*checkStop)(void*),
    void* userCtx,
    const char* logTag
) {
    releasePreloadBuffer();
    outBytesDownloaded.store(0U, std::memory_order_release);
    outDownloadComplete.store(false, std::memory_order_release);
    contentLength = 0U;
    scanOffset = 16U;
    framesBuffered = 0U;
    hasContentLength = false;
    isChunked = false;
    headerParsed = false;
    tooLargeForPreload = false;
    streamEof = false;
    lastSockErrno = 0;
    header = core::drawing::DmdzStreamHeader{};
    chunkDec.reset();

#if defined(ESP32) && !defined(UNIT_TEST) && defined(BOARD_HAS_PSRAM)
    if (maxPreloadBytes < 1024U) {
        tooLargeForPreload = true;
        return false;
    }
    if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
        return false;
    }

    const char* tag = (logTag && logTag[0] != '\0') ? logTag : "Showcase";
    const char* rawPath = streamPath;
    struct sockaddr_in addr;
    if (!resolveShowcaseTargetAddr(
            serverUrl,
            streamPath,
            resolvedHost,
            sizeof(resolvedHost),
            resolvedPort,
            rawPath,
            addr)) {
        return false;
    }

    if (!formatDmdzHttpStreamPath(rawPath, clientId, false, false, profile, activePath, sizeof(activePath))) {
        return false;
    }

    char* hdrBuf = static_cast<char*>(heap_caps_malloc(1536U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!hdrBuf) {
        hdrBuf = static_cast<char*>(malloc(1024U));
    }
    if (!hdrBuf) {
        return false;
    }
    const size_t hdrCap = 1024U;
    struct HdrFreeGuard {
        char* p;
        ~HdrFreeGuard() {
            if (p) free(p);
        }
    } hdrGuard{hdrBuf};

    const int reqLen = snprintf(
        hdrBuf,
        hdrCap,
        "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
        activePath,
        resolvedHost,
        static_cast<unsigned>(resolvedPort)
    );
    if (reqLen <= 0 || static_cast<size_t>(reqLen) >= hdrCap) {
        return false;
    }

    size_t scratchUsed = 0U;
    size_t bodyStart = 0U;
    fd = connectAndReadHttpHeaders(
        addr,
        hdrBuf,
        hdrCap,
        static_cast<size_t>(reqLen),
        scratchUsed,
        bodyStart,
        lastSockErrno,
        checkStop,
        userCtx
    );
    if (fd < 0 ||
        (strncmp(hdrBuf, "HTTP/1.1 200", 12) != 0 && strncmp(hdrBuf, "HTTP/1.0 200", 12) != 0)) {
        closeSocket();
        return false;
    }

    ShowcaseDiscovery::touchVerifiedHost(resolvedHost, resolvedPort);
    hasContentLength = parseContentLengthHeader(hdrBuf, contentLength);
    isChunked = hasChunkedTransferEncoding(hdrBuf);

    if (hasContentLength && contentLength > maxPreloadBytes) {
        LOGI(
            tag,
            "PSRAM preload skipped (Content-Length=%u > cap=%u) -> falling back to WS SPSC ring",
            static_cast<unsigned>(contentLength),
            static_cast<unsigned>(maxPreloadBytes)
        );
        tooLargeForPreload = true;
        closeSocket();
        return false;
    }
    if (hasContentLength && contentLength < 18U) {
        closeSocket();
        return false;
    }

    const size_t allocBytes = (hasContentLength && contentLength > 0U) ? contentLength : maxPreloadBytes;
    preloadBuf = static_cast<uint8_t*>(heap_caps_malloc(allocBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!preloadBuf) {
        tooLargeForPreload = true;
        closeSocket();
        return false;
    }
    preloadCap = allocBytes;

    if (isChunked) {
        chunkScratch = static_cast<uint8_t*>(heap_caps_malloc(16384U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!chunkScratch) {
            releasePreloadBuffer();
            tooLargeForPreload = true;
            return false;
        }
    }

    size_t written = 0U;
    const size_t initialRawLen = (scratchUsed > bodyStart) ? (scratchUsed - bodyStart) : 0U;
    if (initialRawLen > 0U) {
        const uint8_t* initPtr = reinterpret_cast<const uint8_t*>(hdrBuf + bodyStart);
        if (isChunked) {
            size_t off = 0U;
            while (off < initialRawLen && !streamEof) {
                if (written >= preloadCap) {
                    tooLargeForPreload = true;
                    releasePreloadBuffer();
                    return false;
                }
                size_t consumed = 0U;
                const size_t prod = chunkDec.decode(
                    initPtr + off,
                    initialRawLen - off,
                    consumed,
                    preloadBuf + written,
                    preloadCap - written
                );
                off += consumed;
                written += prod;
                if (chunkDec.hasError()) {
                    releasePreloadBuffer();
                    return false;
                }
                if (chunkDec.isDone()) {
                    streamEof = true;
                    break;
                }
                if (consumed == 0U && prod == 0U) {
                    releasePreloadBuffer();
                    return false;
                }
            }
        } else {
            if (initialRawLen > preloadCap) {
                tooLargeForPreload = true;
                releasePreloadBuffer();
                return false;
            }
            memcpy(preloadBuf, initPtr, initialRawLen);
            written = initialRawLen;
        }
        outBytesDownloaded.store(written, std::memory_order_release);
    }

    const uint32_t hdrT0 = millis();
    while (!headerParsed) {
        if (outBytesDownloaded.load(std::memory_order_relaxed) >= 16U) {
            if (!core::drawing::DmdzStreamDecoder::parseHeader(preloadBuf, header)) {
                releasePreloadBuffer();
                return false;
            }
            headerParsed = true;
            scanOffset = 16U;
            (void)pump(outBytesDownloaded, outDownloadComplete, 0U, checkStop, userCtx);
            break;
        }
        if (streamEof || (millis() - hdrT0) >= 3000U) {
            releasePreloadBuffer();
            return false;
        }
        if (!pump(outBytesDownloaded, outDownloadComplete, 20U, checkStop, userCtx)) {
            releasePreloadBuffer();
            return false;
        }
    }
    return headerParsed;
#else
    (void)serverUrl;
    (void)streamPath;
    (void)clientId;
    (void)profile;
    (void)maxPreloadBytes;
    (void)checkStop;
    (void)userCtx;
    (void)logTag;
    return false;
#endif
}

bool DmdzPreloadSession::pump(
    std::atomic<size_t>& outBytesDownloaded,
    std::atomic<bool>& outDownloadComplete,
    uint32_t maxWaitMs,
    StreamStopReason (*checkStop)(void*),
    void* userCtx
) {
#if defined(ESP32) && !defined(UNIT_TEST) && defined(BOARD_HAS_PSRAM)
    if (!preloadBuf) {
        return false;
    }

    size_t written = outBytesDownloaded.load(std::memory_order_relaxed);

    auto scanBufferedFrames = [&]() {
        if (!headerParsed && written >= 16U) {
            if (core::drawing::DmdzStreamDecoder::parseHeader(preloadBuf, header)) {
                headerParsed = true;
                scanOffset = 16U;
            }
        }
        if (!headerParsed) {
            return;
        }
        if (header.isDmdz) {
            while (framesBuffered < header.totalFrames && (scanOffset + 2U) <= written) {
                const uint16_t pLen = static_cast<uint16_t>(preloadBuf[scanOffset]) |
                                      (static_cast<uint16_t>(preloadBuf[scanOffset + 1U]) << 8);
                if ((scanOffset + 2U + static_cast<size_t>(pLen)) > written) {
                    break;
                }
                scanOffset += (2U + static_cast<size_t>(pLen));
                ++framesBuffered;
            }
        } else {
            const size_t fBytes = static_cast<size_t>(header.width) * static_cast<size_t>(header.height) * 2U;
            if (fBytes > 0U && written >= 16U) {
                framesBuffered = static_cast<uint32_t>((written - 16U) / fBytes);
            }
        }
    };

    auto finalizeIfComplete = [&]() -> bool {
        scanBufferedFrames();
        const bool completeByLen = hasContentLength && (written >= contentLength);
        const bool completeByFrames = headerParsed && (framesBuffered >= header.totalFrames);
        const bool completeByEof = streamEof && headerParsed && (framesBuffered > 0U);
        if (completeByLen || completeByFrames || completeByEof) {
            streamEof = true;
            closeSocket();
            if (chunkScratch) {
                free(chunkScratch);
                chunkScratch = nullptr;
            }
            outDownloadComplete.store(true, std::memory_order_release);
            return true;
        }
        return false;
    };

    if (outDownloadComplete.load(std::memory_order_relaxed)) {
        return true;
    }
    if (finalizeIfComplete()) {
        return true;
    }
    if (fd < 0) {
        return outDownloadComplete.load(std::memory_order_relaxed);
    }

    auto drainReads = [&]() -> bool {
        for (int iter = 0; iter < 4 && !streamEof && fd >= 0; ++iter) {
            if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
                return false;
            }
            if (!isChunked) {
                if (written >= preloadCap) {
                    tooLargeForPreload = true;
                    return false;
                }
                const size_t remCap = preloadCap - written;
                const size_t toRead = (remCap < 16384U) ? remCap : 16384U;
                const int r = lwip_recv(fd, preloadBuf + written, toRead, MSG_DONTWAIT);
                if (r > 0) {
                    written += static_cast<size_t>(r);
                    outBytesDownloaded.store(written, std::memory_order_release);
                    if (finalizeIfComplete()) {
                        return true;
                    }
                    continue;
                }
                if (r == 0) {
                    streamEof = true;
                    if (finalizeIfComplete()) {
                        return true;
                    }
                    return false;
                }
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                lastSockErrno = errno;
                return false;
            } else {
                if (!chunkScratch) return false;
                const int r = lwip_recv(fd, chunkScratch, 16384U, MSG_DONTWAIT);
                if (r > 0) {
                    size_t off = 0U;
                    const size_t rLen = static_cast<size_t>(r);
                    while (off < rLen && !streamEof) {
                        if (written >= preloadCap) {
                            tooLargeForPreload = true;
                            return false;
                        }
                        size_t consumed = 0U;
                        const size_t prod = chunkDec.decode(
                            chunkScratch + off,
                            rLen - off,
                            consumed,
                            preloadBuf + written,
                            preloadCap - written
                        );
                        off += consumed;
                        if (prod > 0U) {
                            written += prod;
                            outBytesDownloaded.store(written, std::memory_order_release);
                        }
                        if (chunkDec.hasError()) return false;
                        if (chunkDec.isDone()) {
                            streamEof = true;
                            break;
                        }
                        if (consumed == 0U && prod == 0U) return false;
                    }
                    if (finalizeIfComplete()) {
                        return true;
                    }
                    continue;
                }
                if (r == 0) {
                    streamEof = true;
                    if (finalizeIfComplete()) {
                        return true;
                    }
                    return false;
                }
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                lastSockErrno = errno;
                return false;
            }
        }
        return true;
    };

    if (!drainReads()) {
        return false;
    }
    if (outDownloadComplete.load(std::memory_order_relaxed) || maxWaitMs == 0U || fd < 0) {
        return true;
    }
    if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
        return false;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(maxWaitMs / 1000U);
    tv.tv_usec = static_cast<long>((maxWaitMs % 1000U) * 1000U);
    const int sel = lwip_select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (sel < 0) {
        if (errno == EINTR) return true;
        lastSockErrno = errno;
        return false;
    }
    if (sel > 0 && FD_ISSET(fd, &rfds)) {
        return drainReads();
    }
    return true;
#else
    (void)outBytesDownloaded;
    (void)outDownloadComplete;
    (void)maxWaitMs;
    (void)checkStop;
    (void)userCtx;
    return false;
#endif
}

void DmdzStreamClient::notifyHttpStreamPurge(
    const char* serverUrl,
    const char* streamPath,
    const char* clientId,
    const char* profile
) {
#if defined(ESP32) && !defined(UNIT_TEST) && defined(BOARD_HAS_PSRAM)
    if (!serverUrl || !streamPath || streamPath[0] == '\0') {
        return;
    }
    char host[64] = "";
    uint16_t port = 8890U;
    const char* rawPath = streamPath;
    struct sockaddr_in addr;
    if (!resolveShowcaseTargetAddr(serverUrl, streamPath, host, sizeof(host), port, rawPath, addr)) {
        return;
    }
    char purgePath[288] = "";
    if (!formatDmdzHttpStreamPath(rawPath, clientId, true, false, profile, purgePath, sizeof(purgePath))) {
        return;
    }
    char buf[640];
    const int reqLen = snprintf(
        buf,
        sizeof(buf),
        "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
        purgePath,
        host,
        static_cast<unsigned>(port)
    );
    if (reqLen <= 0 || static_cast<size_t>(reqLen) >= sizeof(buf)) {
        return;
    }
    size_t used = 0U;
    size_t bodyStart = 0U;
    int err = 0;
    const int fd = connectAndReadHttpHeaders(addr, buf, sizeof(buf), static_cast<size_t>(reqLen), used, bodyStart, err, nullptr, nullptr);
    if (fd >= 0) {
        struct linger lin = {1, 0};
        lwip_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        lwip_close(fd);
    }
#else
    (void)serverUrl;
    (void)streamPath;
    (void)clientId;
    (void)profile;
#endif
}

} // namespace net
