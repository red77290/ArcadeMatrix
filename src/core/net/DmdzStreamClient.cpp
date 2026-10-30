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
#endif

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
    if (!params.ioScratch || params.ioScratchCap < 512U) {
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
    char* ioScratch = params.ioScratch;
    const size_t scratchCap = params.ioScratchCap;

    char resolvedHost[64] = "";
    uint16_t resolvedPort = 8890U;
    const char* rawPath = params.streamPath;

    if (strncasecmp(params.streamPath, "http://", 7) == 0 ||
        strncasecmp(params.streamPath, "https://", 8) == 0) {
        if (!parseEndpointHostAndPort(params.streamPath, 8890U, resolvedHost, sizeof(resolvedHost), resolvedPort)) {
            return false;
        }
        if (!ShowcaseDiscovery::resolveHostInPlace(resolvedHost, sizeof(resolvedHost), resolvedPort, true)) {
            return false;
        }
        const char* afterScheme = strstr(params.streamPath, "://");
        const char* slash = afterScheme ? strchr(afterScheme + 3, '/') : nullptr;
        rawPath = slash ? slash : "/";
    } else {
        char baseServerUrl[112] = "";
        bool resolvedOnline = false;
        if (!ShowcaseDiscovery::resolveUrl(
                params.serverUrl,
                baseServerUrl,
                sizeof(baseServerUrl),
                resolvedHost,
                sizeof(resolvedHost),
                resolvedPort,
                resolvedOnline,
                nullptr,
                0U,
                true,
                false) ||
            ShowcaseDiscovery::isAutoDiscoverableHost(resolvedHost)) {
            return false;
        }
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(resolvedPort);
    if (inet_aton(resolvedHost, &addr.sin_addr) == 0) {
        IPAddress resolvedIp;
        if (streamClientFree8BitHeap() < 5120U || !WiFi.hostByName(resolvedHost, resolvedIp)) {
            return false;
        }
        addr.sin_addr.s_addr = static_cast<uint32_t>(resolvedIp);
    }

    int fd = -1;
    int lastSockErrno = 0;
    auto closeStreamSocket = [&](bool abortWithRst) {
        if (fd >= 0) {
            if (abortWithRst) {
                struct linger lin = {1, 0};
                lwip_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
            }
            lwip_close(fd);
            fd = -1;
        }
    };
    struct SocketAbortGuard {
        int& fdRef;
        ~SocketAbortGuard() {
            if (fdRef >= 0) {
                struct linger lin = {1, 0};
                lwip_setsockopt(fdRef, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
                lwip_close(fdRef);
                fdRef = -1;
            }
        }
    } sockGuard{fd};

    auto sendAllBytes = [&](const void* rawBuf, size_t totalLen, uint32_t maxSendMs = 1500U) -> bool {
        const uint8_t* ptr = static_cast<const uint8_t*>(rawBuf);
        size_t sent = 0U;
        const uint32_t t0 = millis();
        while (sent < totalLen) {
            if (checkStop() != StreamStopReason::Continue) {
                return false;
            }
            int w = lwip_send(fd, ptr + sent, totalLen - sent, 0);
            if (w > 0) {
                sent += static_cast<size_t>(w);
            } else if (w == 0) {
                return false;
            } else {
                lastSockErrno = errno;
                if (lastSockErrno != EAGAIN && lastSockErrno != EWOULDBLOCK && lastSockErrno != EINTR) {
                    return false;
                }
                if ((millis() - t0) >= maxSendMs) {
                    return false;
                }
            }
        }
        return true;
    };

    auto openAndReadResponseHeaders = [&](size_t reqBytes, size_t& outScratchUsed, size_t& outBodyStart) -> bool {
        outScratchUsed = 0U;
        outBodyStart = 0U;
        closeStreamSocket(true);

        fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) {
            return false;
        }
        int nodelay = 1;
        lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        int flags = lwip_fcntl(fd, F_GETFL, 0);
        lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int rc = lwip_connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        if (rc < 0 && errno != EINPROGRESS) {
            closeStreamSocket(true);
            return false;
        }
        if (rc != 0) {
            uint32_t connT0 = millis();
            bool connected = false;
            while ((millis() - connT0) < 1500U && checkStop() == StreamStopReason::Continue) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(fd, &wfds);
                struct timeval tv = {0, 40000};
                int sel = lwip_select(fd + 1, nullptr, &wfds, nullptr, &tv);
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
                closeStreamSocket(true);
                return false;
            }
        }

        lwip_fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        struct timeval rcvTv = {0, 40000};  // 40 ms slice keeps rotation shutdown & WebUI preemption fast
        struct timeval sndTv = {0, 120000}; // 120 ms per send slice inside sendAllBytes
        lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvTv, sizeof(rcvTv));
        lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sndTv, sizeof(sndTv));

        if (reqBytes == 0U || reqBytes >= scratchCap || !sendAllBytes(ioScratch, reqBytes, 1500U)) {
            closeStreamSocket(true);
            return false;
        }
        ioScratch[0] = '\0';

        bool foundHeaderEnd = false;
        uint32_t hdrT0 = millis();
        while (!foundHeaderEnd &&
               (millis() - hdrT0) < 3500U &&
               checkStop() == StreamStopReason::Continue) {
            if (outScratchUsed >= scratchCap - 1U) {
                break;
            }
            int r = lwip_recv(fd, ioScratch + outScratchUsed, scratchCap - 1U - outScratchUsed, 0);
            if (r > 0) {
                outScratchUsed += static_cast<size_t>(r);
                ioScratch[outScratchUsed] = '\0';
                char* sep = strstr(ioScratch, "\r\n\r\n");
                if (sep != nullptr) {
                    foundHeaderEnd = true;
                    outBodyStart = static_cast<size_t>((sep - ioScratch) + 4);
                    *sep = '\0';
                }
            } else if (r == 0) {
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }
        return foundHeaderEnd;
    };

    char activePath[288] = "";
    size_t scratchUsed = 0U;
    size_t bodyStart = 0U;
    bool isWebSocket = false;
    bool serverPaced = false;
    bool isChunked = false;
    core::drawing::DmdzWsDemuxer wsDemux;
    HttpChunkedDecoder chunkDec;

    if (core::drawing::buildDmdzWebSocketPath(
            rawPath,
            params.clientId,
            activePath,
            sizeof(activePath),
            params.wsCredits)) {
        char wsKey[25];
        formatRandomWsKeyBase64(wsKey);
        int wsReqLen = snprintf(
            ioScratch,
            scratchCap,
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
        if (wsReqLen > 0 &&
            static_cast<size_t>(wsReqLen) < scratchCap &&
            openAndReadResponseHeaders(static_cast<size_t>(wsReqLen), scratchUsed, bodyStart) &&
            strncmp(ioScratch, "HTTP/1.1 101", 12) == 0) {
            isWebSocket = true;
            serverPaced = true;
            wsDemux.reset();
        } else {
            closeStreamSocket(true);
            if (checkStop() != StreamStopReason::Continue) {
                return false;
            }
        }
    }

    if (!isWebSocket) {
        const bool hasQ = (strchr(rawPath, '?') != nullptr);
        const bool hasPaced = (strstr(rawPath, "paced=") != nullptr);
        const bool hasCodec = (strstr(rawPath, "codec=") != nullptr);
        if (hasPaced && hasCodec) {
            copySafeLocalStr(activePath, sizeof(activePath), rawPath);
        } else if (hasPaced && !hasCodec) {
            snprintf(activePath, sizeof(activePath), "%s%scodec=dmdz1", rawPath, hasQ ? "&" : "?");
        } else if (!hasPaced && hasCodec) {
            snprintf(activePath, sizeof(activePath), "%s%spaced=1", rawPath, hasQ ? "&" : "?");
        } else {
            snprintf(activePath, sizeof(activePath), "%s%spaced=1&codec=dmdz1", rawPath, hasQ ? "&" : "?");
        }

        int reqLen = snprintf(
            ioScratch,
            scratchCap,
            "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
            activePath,
            resolvedHost,
            static_cast<unsigned>(resolvedPort)
        );
        if (reqLen <= 0 ||
            static_cast<size_t>(reqLen) >= scratchCap ||
            !openAndReadResponseHeaders(static_cast<size_t>(reqLen), scratchUsed, bodyStart) ||
            (strncmp(ioScratch, "HTTP/1.1 200", 12) != 0 && strncmp(ioScratch, "HTTP/1.0 200", 12) != 0)) {
            LOGW(logTag, "TCP stream HTTP rejected: path=%s status=%.40s", activePath, ioScratch);
            closeStreamSocket(true);
            return false;
        }

        for (const char* p = ioScratch; *p != '\0'; ++p) {
            if ((*p == 'X' || *p == 'x') && strncasecmp(p, "x-dmd-paced:", 12) == 0) {
                const char* v = p + 12;
                while (*v == ' ' || *v == '\t') ++v;
                if (*v == '1') {
                    serverPaced = true;
                }
                break;
            }
        }
        isChunked = hasChunkedTransferEncoding(ioScratch);
    }

    ShowcaseDiscovery::touchVerifiedHost(resolvedHost, resolvedPort);

    const size_t initialRawLen = (scratchUsed > bodyStart) ? (scratchUsed - bodyStart) : 0U;
    const char* lastReadFailReason = "idle_body";
    uint16_t pendingWsCredits = 0U;
    const uint8_t clampedWsCredits = clampWsCredits(params.wsCredits);
    const uint8_t ackBatch = computeWsAckBatch(clampedWsCredits);

    StreamByteRing rxRing;
    bool streamEof = false;
    uint8_t rawRxBuf[520];

    auto flushWsControlAndCredits = [&](bool forceAllCredits) -> bool {
        if (!isWebSocket) {
            return true;
        }
        while (wsDemux.hasPendingPong()) {
            uint8_t pongFrame[131];
            const size_t pongLen = wsDemux.takePendingPongFrame(pongFrame);
            if (pongLen > 0U && !sendAllBytes(pongFrame, pongLen, 1000U)) {
                lastReadFailReason = "sock_err";
                return false;
            }
        }
        while (canFlushWsCredits(
                   pendingWsCredits,
                   ackBatch,
                   forceAllCredits,
                   rxRing.size(),
                   rxRing.freeBytes(),
                   rxRing.capacity())) {
            const uint8_t batch = (pendingWsCredits > 16U) ? 16U : static_cast<uint8_t>(pendingWsCredits);
            uint8_t ackFrame[22];
            const size_t ackLen = core::drawing::DmdzWsDemuxer::buildMaskedCreditAckFrame(batch, ackFrame);
            if (ackLen > 0U && !sendAllBytes(ackFrame, ackLen, 1000U)) {
                lastReadFailReason = "sock_err";
                return false;
            }
            pendingWsCredits = static_cast<uint16_t>(pendingWsCredits - batch);
        }
        return true;
    };

    auto feedRawBytesToRing = [&](const uint8_t* rawSrc, size_t rawLen) -> bool {
        if (isWebSocket) {
            size_t off = 0U;
            while (off < rawLen && !streamEof) {
                const size_t contig = rxRing.contiguousWriteCap();
                if (contig == 0U) {
                    lastReadFailReason = "decode_err";
                    return false;
                }
                size_t consumed = 0U;
                uint8_t creditsToAck = 0U;
                const size_t produced = wsDemux.decode(
                    rawSrc + off,
                    rawLen - off,
                    consumed,
                    rxRing.writePtr(),
                    contig,
                    creditsToAck
                );
                off += consumed;
                if (produced > 0U) {
                    rxRing.commitWrite(produced);
                }
                pendingWsCredits = static_cast<uint16_t>(pendingWsCredits + creditsToAck);
                if (wsDemux.hasError()) {
                    lastReadFailReason = "decode_err";
                    return false;
                }
                if (wsDemux.hasPendingPong() && !flushWsControlAndCredits(false)) {
                    return false;
                }
                if (wsDemux.isClosed()) {
                    streamEof = true;
                    break;
                }
                if (consumed == 0U && produced == 0U) {
                    lastReadFailReason = "decode_err";
                    return false;
                }
            }
            return flushWsControlAndCredits(false);
        }

        if (isChunked) {
            size_t off = 0U;
            while (off < rawLen && !streamEof) {
                const size_t contig = rxRing.contiguousWriteCap();
                if (contig == 0U) {
                    lastReadFailReason = "decode_err";
                    return false;
                }
                size_t consumed = 0U;
                const size_t produced = chunkDec.decode(
                    rawSrc + off,
                    rawLen - off,
                    consumed,
                    rxRing.writePtr(),
                    contig
                );
                off += consumed;
                if (produced > 0U) {
                    rxRing.commitWrite(produced);
                }
                if (chunkDec.hasError()) {
                    lastReadFailReason = "decode_err";
                    return false;
                }
                if (chunkDec.isDone()) {
                    streamEof = true;
                    break;
                }
                if (consumed == 0U && produced == 0U) {
                    lastReadFailReason = "decode_err";
                    return false;
                }
            }
            return true;
        }

        rxRing.push(rawSrc, rawLen);
        return true;
    };

    // Transition `ioScratch` from Phase 1 HTTP/101 headers into Phase 2 circular `StreamByteRing`
    // holding already-demuxed stream payload bytes.
    if (!isWebSocket && !isChunked) {
        rxRing.initFromSlice(reinterpret_cast<uint8_t*>(ioScratch), scratchCap, bodyStart, initialRawLen);
    } else {
        rxRing.init(reinterpret_cast<uint8_t*>(ioScratch), scratchCap);
        if (initialRawLen > 0U) {
            const size_t rawStart = scratchCap - initialRawLen;
            memmove(ioScratch + rawStart, ioScratch + bodyStart, initialRawLen);
            size_t rawOff = 0U;
            while (rawOff < initialRawLen && !streamEof) {
                const size_t rem = initialRawLen - rawOff;
                const size_t step = (rem < sizeof(rawRxBuf)) ? rem : sizeof(rawRxBuf);
                memcpy(rawRxBuf, ioScratch + rawStart + rawOff, step);
                rawOff += step;
                if (!feedRawBytesToRing(rawRxBuf, step)) {
                    closeStreamSocket(true);
                    return false;
                }
            }
        }
    }

    auto drainNonBlockingToRing = [&]() -> bool {
        while (!streamEof && !rxRing.full()) {
            const StreamStopReason sr = checkStop();
            if (sr != StreamStopReason::Continue) {
                lastReadFailReason = stopReasonLabel(sr);
                return false;
            }
            if (!isWebSocket && !isChunked) {
                const size_t contig = rxRing.contiguousWriteCap();
                const size_t toRead = (contig < 1024U) ? contig : 1024U;
                int r = lwip_recv(fd, rxRing.writePtr(), toRead, MSG_DONTWAIT);
                if (r > 0) {
                    rxRing.commitWrite(static_cast<size_t>(r));
                    continue;
                }
                if (r == 0) {
                    streamEof = true;
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                lastSockErrno = errno;
                lastReadFailReason = "sock_err";
                return false;
            }

            const size_t space = rxRing.freeBytes();
            const size_t toRead = (space < sizeof(rawRxBuf)) ? space : sizeof(rawRxBuf);
            int r = lwip_recv(fd, rawRxBuf, toRead, MSG_DONTWAIT);
            if (r > 0) {
                if (!feedRawBytesToRing(rawRxBuf, static_cast<size_t>(r))) {
                    return false;
                }
                continue;
            }
            if (r == 0) {
                streamEof = true;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            lastSockErrno = errno;
            lastReadFailReason = "sock_err";
            return false;
        }
        return true;
    };

    auto pumpSocketToRing = [&](uint32_t maxWaitMs) -> bool {
        if (!drainNonBlockingToRing()) {
            return false;
        }
        if (maxWaitMs == 0U || streamEof || rxRing.full()) {
            return true;
        }
        const StreamStopReason sr = checkStop();
        if (sr != StreamStopReason::Continue) {
            lastReadFailReason = stopReasonLabel(sr);
            return false;
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv;
        tv.tv_sec = static_cast<long>(maxWaitMs / 1000U);
        tv.tv_usec = static_cast<long>((maxWaitMs % 1000U) * 1000U);
        int sel = lwip_select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (errno == EINTR) {
                return true;
            }
            lastSockErrno = errno;
            lastReadFailReason = "sock_err";
            return false;
        }
        if (sel > 0 && FD_ISSET(fd, &rfds)) {
            return drainNonBlockingToRing();
        }
        return true;
    };

    auto ensureRingBytes = [&](size_t minBytes, uint32_t maxIdleMs) -> bool {
        if (minBytes > rxRing.capacity()) {
            minBytes = rxRing.capacity();
        }
        if (rxRing.size() >= minBytes) {
            return true;
        }
        uint32_t lastProg = millis();
        while (rxRing.size() < minBytes) {
            const StreamStopReason sr = checkStop();
            if (sr != StreamStopReason::Continue) {
                lastReadFailReason = stopReasonLabel(sr);
                return false;
            }
            const size_t before = rxRing.size();
            if (!pumpSocketToRing(0U)) {
                return false;
            }
            if (rxRing.size() > before) {
                lastProg = millis();
                if (rxRing.size() >= minBytes) {
                    return true;
                }
                continue;
            }
            if (streamEof) {
                lastReadFailReason = "eof_early";
                return false;
            }
            if (isWebSocket && (pendingWsCredits > 0U || wsDemux.hasPendingPong())) {
                if (!flushWsControlAndCredits(true)) {
                    return false;
                }
            }
            const uint32_t elapsed = millis() - lastProg;
            if (elapsed >= maxIdleMs) {
                lastReadFailReason = "idle_body";
                return false;
            }
            const uint32_t waitMs = std::min<uint32_t>(20U, maxIdleMs - elapsed);
            if (!pumpSocketToRing(waitMs)) {
                return false;
            }
            if (rxRing.size() > before) {
                lastProg = millis();
            } else if (streamEof && rxRing.size() < minBytes) {
                lastReadFailReason = "eof_early";
                return false;
            }
        }
        return true;
    };

    auto readExactStreamBody = [&](uint8_t* dst, size_t wantLen, uint32_t maxIdleMs) -> bool {
        size_t got = 0U;
        if (!rxRing.empty() && wantLen > 0U) {
            got += rxRing.pop(dst, wantLen);
        }
        if (got >= wantLen) {
            return true;
        }
        uint32_t lastProg = millis();
        while (got < wantLen) {
            const StreamStopReason sr = checkStop();
            if (sr != StreamStopReason::Continue) {
                lastReadFailReason = stopReasonLabel(sr);
                return false;
            }
            if (!pumpSocketToRing(0U)) {
                return false;
            }
            if (!rxRing.empty()) {
                got += rxRing.pop(dst + got, wantLen - got);
                lastProg = millis();
                if (got >= wantLen) {
                    return true;
                }
                continue;
            }
            if (streamEof) {
                lastReadFailReason = "eof_early";
                return false;
            }
            if (isWebSocket && (pendingWsCredits > 0U || wsDemux.hasPendingPong())) {
                if (!flushWsControlAndCredits(true)) {
                    return false;
                }
            }
            const uint32_t elapsed = millis() - lastProg;
            if (elapsed >= maxIdleMs) {
                lastReadFailReason = "idle_body";
                return false;
            }
            const uint32_t waitMs = std::min<uint32_t>(20U, maxIdleMs - elapsed);
            if (!pumpSocketToRing(waitMs)) {
                return false;
            }
            if (!rxRing.empty()) {
                got += rxRing.pop(dst + got, wantLen - got);
                lastProg = millis();
            } else if (streamEof) {
                lastReadFailReason = "eof_early";
                return false;
            }
        }
        return true;
    };

    uint8_t hdr[16] = {0};
    core::drawing::DmdzStreamHeader parsedHdr;
    if (!readExactStreamBody(hdr, sizeof(hdr), 3500U) ||
        !core::drawing::DmdzStreamDecoder::parseHeader(hdr, parsedHdr)) {
        LOGW(
            logTag,
            "TCP stream bad header magic (proto=%s chunked=%u): %02x %02x %02x %02x %02x %02x",
            isWebSocket ? "ws" : "http",
            isChunked ? 1U : 0U,
            hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5]
        );
        closeStreamSocket(true);
        return false;
    }

    const uint8_t fps = parsedHdr.fps;
    const uint16_t w = parsedHdr.width;
    const uint16_t h = parsedHdr.height;
    const uint16_t totalFrames = parsedHdr.totalFrames;
    const size_t framePixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t frameBytes = framePixels * sizeof(uint16_t);
    if (frameBytes == 0U || framePixels > params.maxFramePixels || totalFrames == 0U) {
        LOGW(
            logTag,
            "TCP stream geometry mismatch: %ux%u (%u frames) vs panel %ux%u",
            static_cast<unsigned>(w),
            static_cast<unsigned>(h),
            static_cast<unsigned>(totalFrames),
            static_cast<unsigned>(params.panelWidth),
            static_cast<unsigned>(params.panelHeight)
        );
        closeStreamSocket(true);
        return false;
    }

    core::drawing::DmdzStreamDecoder dmdzDec;
    if (parsedHdr.isDmdz) {
        dmdzDec.init(w, h);
    }

    const bool singleBuffer = (params.frameBuf1 == nullptr);
    const uint32_t safeFps = std::max<uint32_t>(1U, static_cast<uint32_t>(fps));
    const uint32_t headerExpectedMs = (static_cast<uint32_t>(totalFrames) * 1000U) / safeFps;
    const uint32_t maxAllowedMs = std::min<uint32_t>(
        300000U,
        std::max<uint32_t>(params.configuredStreamMs + 4000U, (headerExpectedMs * 2U) + 4500U)
    );
    const uint32_t streamStartMs = millis();
    const size_t minFreeForCredit = effectiveWsCreditMinFreeBytes(rxRing.capacity());
    const size_t safePrebufCap = (rxRing.capacity() > minFreeForCredit)
        ? (rxRing.capacity() - minFreeForCredit)
        : rxRing.capacity();

    uint32_t scheduleT0 = 0U;
    uint32_t publishedOnClock = 0U;
    const char* endReason = "eof";
    size_t minFree8 = streamClientFree8BitHeap();
    uint32_t lateFrames = 0U;

    auto dueMsFor = [&](uint32_t k) -> uint32_t {
        return scheduleT0 + static_cast<uint32_t>((static_cast<uint64_t>(k) * 1000ULL) / safeFps);
    };

    uint16_t framesRead = 0U;
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

        // 1. For frames after Frame 0, pace on the monotonic presentation clock BEFORE locking
        //    `dstBuf`, continuously pumping incoming Wi-Fi packets into `rxRing` so `esp_wifi` +
        //    lwIP RX buffers stay drained and Frame `f` is ready in RAM at `due`.
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
                if (!streamEof && !rxRing.full()) {
                    const uint32_t sliceMs = (remMs > 10) ? 10U : static_cast<uint32_t>(remMs);
                    if (!pumpSocketToRing(sliceMs)) {
                        endReason = lastReadFailReason;
                        abortPacing = true;
                        break;
                    }
                } else {
                    const uint32_t sleepMs = (remMs > 10) ? 10U : static_cast<uint32_t>(remMs);
                    vTaskDelay(pdMS_TO_TICKS(sleepMs > 1U ? (sleepMs - 1U) : 1U));
                }
            }
            if (abortPacing) {
                break;
            }
        }

        // 2. Ensure Frame `f`'s compressed bytes (up to `safePrebufCap`) are already buffered in
        //    `rxRing` BEFORE locking `dstBuf` ODD, keeping the Core 1 seqlock ODD window < 0.5 ms.
        if (parsedHdr.isDmdz) {
            if (!ensureRingBytes(2U, 5000U)) {
                endReason = (strcmp(lastReadFailReason, "idle_body") == 0) ? "idle_wait" : lastReadFailReason;
                break;
            }
            uint8_t peekLen[2] = {0U, 0U};
            rxRing.peek(peekLen, 2U, 0U);
            const uint16_t nextPayloadLen = static_cast<uint16_t>(peekLen[0]) |
                                            (static_cast<uint16_t>(peekLen[1]) << 8);
            const size_t wantPrebuf = std::min<size_t>(
                static_cast<size_t>(2U) + static_cast<size_t>(nextPayloadLen),
                safePrebufCap
            );
            if (!ensureRingBytes(wantPrebuf, 5000U)) {
                endReason = (strcmp(lastReadFailReason, "idle_body") == 0) ? "idle_len" : lastReadFailReason;
                break;
            }
        } else {
            const size_t wantPrebuf = std::min<size_t>(frameBytes, safePrebufCap);
            if (!ensureRingBytes(wantPrebuf, 5000U)) {
                endReason = (strcmp(lastReadFailReason, "idle_body") == 0) ? "idle_wait" : lastReadFailReason;
                break;
            }
        }

        if (!streamEof && !rxRing.full()) {
            if (!pumpSocketToRing(0U)) {
                endReason = lastReadFailReason;
                break;
            }
        }

        uint16_t* dstBuf = singleBuffer
            ? params.frameBuf0
            : (((f & 1U) == 0U) ? params.frameBuf0 : params.frameBuf1);

        // 3. Caller pre-write hook (Core 1 handoff check + ODD seqlock barrier when single-buffered).
        if (params.onBeginFrameWrite) {
            params.onBeginFrameWrite(params.userCtx, f, dstBuf, singleBuffer, safeFps);
        }

        // 4. Decode Frame `f` from `rxRing` into `dstBuf` (pure RAM-to-RAM for frames <= `safePrebufCap`;
        //    oversized frames seamlessly stream remaining bytes via `readExactStreamBody`).
        bool okFrame = false;
        const char* frameFailReason = "idle_body";
        if (parsedHdr.isDmdz) {
            if (!singleBuffer && f > 0U) {
                const uint16_t* prevBuf = ((f & 1U) == 0U) ? params.frameBuf1 : params.frameBuf0;
                memcpy(dstBuf, prevBuf, frameBytes);
            }
            uint8_t lenBytes[2] = {0U, 0U};
            if (!readExactStreamBody(lenBytes, 2U, 5000U)) {
                frameFailReason = (strcmp(lastReadFailReason, "idle_body") == 0) ? "idle_len" : lastReadFailReason;
            } else {
                const uint16_t payloadLen = static_cast<uint16_t>(lenBytes[0]) |
                                            (static_cast<uint16_t>(lenBytes[1]) << 8);
                if (!dmdzDec.beginFrame(payloadLen)) {
                    frameFailReason = "decode_err";
                } else {
                    size_t rem = payloadLen;
                    okFrame = true;
                    while (rem > 0U) {
                        if (rxRing.empty() && !ensureRingBytes(1U, 5000U)) {
                            frameFailReason = lastReadFailReason;
                            okFrame = false;
                            break;
                        }
                        const size_t contig = rxRing.contiguousReadCap();
                        const size_t take = (rem < contig) ? rem : contig;
                        if (take == 0U) {
                            frameFailReason = "idle_body";
                            okFrame = false;
                            break;
                        }
                        if (!dmdzDec.feed(rxRing.readPtr(), take, dstBuf)) {
                            frameFailReason = "decode_err";
                            okFrame = false;
                            break;
                        }
                        rxRing.consumeRead(take);
                        rem -= take;
                        if (rem > 0U && rxRing.empty() && isWebSocket &&
                            (pendingWsCredits > 0U || wsDemux.hasPendingPong())) {
                            if (!flushWsControlAndCredits(true)) {
                                frameFailReason = lastReadFailReason;
                                okFrame = false;
                                break;
                            }
                        }
                    }
                    if (okFrame && !dmdzDec.finishFrame()) {
                        frameFailReason = "decode_err";
                        okFrame = false;
                    }
                }
            }
        } else {
            okFrame = readExactStreamBody(reinterpret_cast<uint8_t*>(dstBuf), frameBytes, 5000U);
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
                    const int32_t lagMs = static_cast<int32_t>(now - due);
                    if (lagMs > static_cast<int32_t>(1000U / safeFps)) {
                        scheduleT0 = now - static_cast<uint32_t>(
                            (static_cast<uint64_t>(publishedOnClock) * 1000ULL) / safeFps
                        );
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

        // 5. Immediately publish EVEN seqlock with zero post-decode delay!
        if (params.onFinishFrameWrite) {
            params.onFinishFrameWrite(params.userCtx, f, dstBuf, w, h, okFrame, singleBuffer);
        }
        if (!okFrame) {
            const StreamStopReason sr = checkStop();
            endReason = (sr != StreamStopReason::Continue) ? stopReasonLabel(sr) : frameFailReason;
            break;
        }

        if (isWebSocket && (static_cast<uint32_t>(f) + 1U < static_cast<uint32_t>(totalFrames))) {
            if (!flushWsControlAndCredits(false)) {
                endReason = lastReadFailReason;
                break;
            }
        }

        ++framesRead;
        ++publishedOnClock;
        const size_t free8 = streamClientFree8BitHeap();
        if (free8 < minFree8) minFree8 = free8;
        if ((f & 0x07U) == 0U) {
            taskYIELD();
        }
    }

    if (isWebSocket && framesRead == totalFrames && fd >= 0) {
        (void)sendAllBytes(
            core::drawing::DmdzWsDemuxer::kMaskedCloseFrame,
            sizeof(core::drawing::DmdzWsDemuxer::kMaskedCloseFrame),
            250U
        );
    }
    closeStreamSocket(true);
    if (framesRead > 0U) {
        ShowcaseDiscovery::touchVerifiedHost(resolvedHost, resolvedPort);
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
    {
        const uint32_t durMs = std::max<uint32_t>(1U, millis() - streamStartMs);
        const uint32_t pres = params.getPresentedFrames
            ? params.getPresentedFrames(params.userCtx)
            : static_cast<uint32_t>(framesRead);
        LOGI(
            logTag,
            "TCP stream done (%s, errno=%d): proto=%s codec=%s path=%s rxFrames=%u/%u presented=%u late=%u durMs=%u effFps=%.1f paced=%u minFree8=%u stackFree=%u",
            endReason,
            lastSockErrno,
            isWebSocket ? "ws" : "http",
            parsedHdr.isDmdz ? "dmdz1" : "dmds",
            activePath,
            static_cast<unsigned>(framesRead),
            static_cast<unsigned>(totalFrames),
            static_cast<unsigned>(pres),
            static_cast<unsigned>(lateFrames),
            static_cast<unsigned>(durMs),
            (static_cast<float>(pres) * 1000.0f) / static_cast<float>(durMs),
            serverPaced ? 1U : 0U,
            static_cast<unsigned>(minFree8),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr))
        );
    }
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
        dmdzDec.init(w, h);
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

} // namespace net

