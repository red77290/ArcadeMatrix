#include "DmdzStreamClient.h"

#include "../drawing/DmdzStreamDecoder.h"
#include "RawMqttSocket.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#if defined(ESP32)
#include <Arduino.h>
#include "../Logger.h"
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
    size_t n = 0U;
    while (n + 1U < cap && src[n] != '\0') {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

bool extractStreamBaseAndApiKey(
    const char* rawPath,
    const char*& outPathStart,
    size_t& outBaseLen,
    char* outApiKeyPair,
    size_t apiKeyCap
) {
    outPathStart = nullptr;
    outBaseLen = 0U;
    if (outApiKeyPair && apiKeyCap > 0U) {
        outApiKeyPair[0] = '\0';
    }
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
    if (baseLen == 0U || pathStart[0] != '/') {
        return false;
    }
    const char* streamSeg = strstr(pathStart, "/stream/");
    if (!streamSeg || streamSeg >= (pathStart + baseLen)) {
        return false;
    }

    if (outApiKeyPair && apiKeyCap > 0U && qMark && qMark[1] != '\0') {
        const char* ak = strstr(qMark + 1, "api_key=");
        if (ak != nullptr) {
            const char* akEnd = strchr(ak, '&');
            const size_t akLen = akEnd ? static_cast<size_t>(akEnd - ak) : strlen(ak);
            if (akLen > 8U && akLen + 1U < apiKeyCap) {
                memcpy(outApiKeyPair, ak, akLen);
                outApiKeyPair[akLen] = '\0';
            }
        }
    }

    outPathStart = pathStart;
    outBaseLen = baseLen;
    return true;
}

#if defined(ESP32) && !defined(UNIT_TEST)
inline size_t streamClientFree8BitHeap() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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

bool waitSocketReadableMs(int fd, uint32_t maxWaitMs, int& outErrno) {
    if (fd < 0 || maxWaitMs == 0U) {
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
            return false;
        }
        outErrno = errno;
        return false;
    }
    return (sel > 0) && FD_ISSET(fd, &rfds);
}
#endif // defined(ESP32) && !defined(UNIT_TEST)

} // namespace

bool formatDmdzHttpStreamPath(
    const char* rawPath,
    const char* clientId,
    bool purge,
    char* outPath,
    size_t outCap
) {
    if (!outPath || outCap == 0U) {
        return false;
    }
    outPath[0] = '\0';

    const char* pathStart = nullptr;
    size_t baseLen = 0U;
    char apiKeyPair[80] = "";
    if (!extractStreamBaseAndApiKey(rawPath, pathStart, baseLen, apiKeyPair, sizeof(apiKeyPair))) {
        return false;
    }
    if (baseLen >= outCap) {
        return false;
    }

    char clientParam[64] = "";
    if (clientId && clientId[0] != '\0') {
        const int cn = snprintf(clientParam, sizeof(clientParam), "&client_id=%s", clientId);
        if (cn <= 0 || static_cast<size_t>(cn) >= sizeof(clientParam)) {
            clientParam[0] = '\0';
        }
    }

    char apiKeyParam[84] = "";
    if (apiKeyPair[0] != '\0') {
        const int an = snprintf(apiKeyParam, sizeof(apiKeyParam), "&%s", apiKeyPair);
        if (an <= 0 || static_cast<size_t>(an) >= sizeof(apiKeyParam)) {
            apiKeyParam[0] = '\0';
        }
    }

    const int n = snprintf(
        outPath,
        outCap,
        "%.*s?purge=%s&codec=dmdz2%s%s",
        static_cast<int>(baseLen),
        pathStart,
        purge ? "true" : "false",
        clientParam,
        apiKeyParam
    );
    if (n <= 0 || static_cast<size_t>(n) >= outCap) {
        outPath[0] = '\0';
        return false;
    }
    return true;
}

bool formatDmdzHttpDeletePath(
    const char* rawPath,
    const char* clientId,
    char* outPath,
    size_t outCap
) {
    if (!outPath || outCap == 0U) {
        return false;
    }
    outPath[0] = '\0';

    const char* pathStart = nullptr;
    size_t baseLen = 0U;
    char apiKeyPair[80] = "";
    if (!extractStreamBaseAndApiKey(rawPath, pathStart, baseLen, apiKeyPair, sizeof(apiKeyPair))) {
        return false;
    }
    if (baseLen >= outCap) {
        return false;
    }

    const bool hasClient = (clientId != nullptr && clientId[0] != '\0');
    const bool hasApiKey = (apiKeyPair[0] != '\0');
    int n = 0;
    if (hasClient && hasApiKey) {
        n = snprintf(
            outPath,
            outCap,
            "%.*s?client_id=%s&%s",
            static_cast<int>(baseLen),
            pathStart,
            clientId,
            apiKeyPair
        );
    } else if (hasClient) {
        n = snprintf(
            outPath,
            outCap,
            "%.*s?client_id=%s",
            static_cast<int>(baseLen),
            pathStart,
            clientId
        );
    } else if (hasApiKey) {
        n = snprintf(
            outPath,
            outCap,
            "%.*s?%s",
            static_cast<int>(baseLen),
            pathStart,
            apiKeyPair
        );
    } else {
        n = snprintf(outPath, outCap, "%.*s", static_cast<int>(baseLen), pathStart);
    }

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

void DmdzHttpStreamSession::close() {
#if defined(ESP32) && !defined(UNIT_TEST)
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

bool DmdzHttpStreamSession::open(
    const char* serverUrl,
    const char* streamPath,
    const char* clientId,
    bool purge,
    StreamStopReason (*checkStop)(void*),
    void* userCtx,
    const char* logTag
) {
    close();
    rawOff = 0U;
    rawLen = 0U;
    isChunked = false;
    hasContentLength = false;
    streamEof = false;
    headerParsed = false;
    sockError = false;
    lastSockErrno = 0;
    contentLength = 0U;
    bodyBytesDelivered = 0U;
    header = core::drawing::DmdzStreamHeader{};
    chunkDec.reset();
    memset(rawHeader16, 0, sizeof(rawHeader16));

    if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
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

    if (!formatDmdzHttpStreamPath(rawPath, clientId, purge, activePath, sizeof(activePath))) {
        return false;
    }

    char* hdrScratch = reinterpret_cast<char*>(rawBuf);
    const int reqLen = snprintf(
        hdrScratch,
        sizeof(rawBuf),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Connection: close\r\n\r\n",
        activePath,
        resolvedHost,
        static_cast<unsigned>(resolvedPort)
    );
    if (reqLen <= 0 || static_cast<size_t>(reqLen) >= sizeof(rawBuf)) {
        return false;
    }

    size_t scratchUsed = 0U;
    size_t bodyStart = 0U;
    fd = connectAndReadHttpHeaders(
        addr,
        hdrScratch,
        sizeof(rawBuf),
        static_cast<size_t>(reqLen),
        scratchUsed,
        bodyStart,
        lastSockErrno,
        checkStop,
        userCtx
    );
    if (fd < 0) {
        return false;
    }

    int statusCode = 0;
    if (strncmp(hdrScratch, "HTTP/1.", 7) == 0) {
        const char* sp = strchr(hdrScratch, ' ');
        if (sp) {
            statusCode = atoi(sp + 1);
        }
    }
    if (statusCode != 200) {
        LOGW(tag, "HTTP stream open rejected (status=%d, path=%s)", statusCode, activePath);
        close();
        return false;
    }

    hasContentLength = parseContentLengthHeader(hdrScratch, contentLength);
    isChunked = !hasContentLength && hasChunkedTransferEncoding(hdrScratch);
    if (hasContentLength && contentLength < 16U) {
        close();
        return false;
    }

    const size_t leftover = (scratchUsed > bodyStart) ? (scratchUsed - bodyStart) : 0U;
    if (leftover > 0U) {
        memmove(rawBuf, rawBuf + bodyStart, leftover);
        rawOff = 0U;
        rawLen = static_cast<uint16_t>(leftover);
    }

    size_t hdrBytesGot = 0U;
    const uint32_t hdrT0 = millis();
    while (hdrBytesGot < 16U && (millis() - hdrT0) < 2500U) {
        if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
            close();
            return false;
        }
        size_t stepGot = 0U;
        if (!readSome(rawHeader16 + hdrBytesGot, 16U - hdrBytesGot, stepGot, 40U, checkStop, userCtx)) {
            close();
            return false;
        }
        hdrBytesGot += stepGot;
        if (hdrBytesGot < 16U && streamEof) {
            break;
        }
    }
    if (hdrBytesGot < 16U || !core::drawing::DmdzStreamDecoder::parseHeader(rawHeader16, header)) {
        LOGW(tag, "Invalid or truncated DMDZ v2 stream header (%u bytes)", static_cast<unsigned>(hdrBytesGot));
        close();
        return false;
    }

    headerParsed = true;
    ShowcaseDiscovery::touchVerifiedHost(resolvedHost, resolvedPort);
    return true;
#else
    (void)serverUrl;
    (void)streamPath;
    (void)clientId;
    (void)purge;
    (void)logTag;
    return false;
#endif
}

bool DmdzHttpStreamSession::readSome(
    uint8_t* dst,
    size_t dstCap,
    size_t& outBytesRead,
    uint32_t maxWaitMs,
    StreamStopReason (*checkStop)(void*),
    void* userCtx
) {
    outBytesRead = 0U;
    if (!dst || dstCap == 0U) {
        return true;
    }
    if (sockError) {
        return false;
    }
    if (streamEof) {
        return true;
    }
    if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
        return false;
    }

#if defined(ESP32) && !defined(UNIT_TEST)
    if (!isChunked) {
        if (hasContentLength && bodyBytesDelivered >= contentLength) {
            streamEof = true;
            return true;
        }
        const size_t maxDeliver = hasContentLength
            ? std::min<size_t>(dstCap, contentLength - bodyBytesDelivered)
            : dstCap;

        if (rawOff < rawLen) {
            const size_t buffered = static_cast<size_t>(rawLen - rawOff);
            const size_t take = std::min<size_t>(maxDeliver, buffered);
            memcpy(dst, rawBuf + rawOff, take);
            rawOff = static_cast<uint16_t>(rawOff + take);
            if (rawOff >= rawLen) {
                rawOff = 0U;
                rawLen = 0U;
            }
            outBytesRead += take;
            bodyBytesDelivered += take;
            if (hasContentLength && bodyBytesDelivered >= contentLength) {
                streamEof = true;
                return true;
            }
            if (outBytesRead >= maxDeliver) {
                return true;
            }
        }

        auto drainNonBlocking = [&]() -> bool {
            while (outBytesRead < maxDeliver && fd >= 0 && !streamEof) {
                if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
                    return false;
                }
                const size_t rem = maxDeliver - outBytesRead;
                const int r = lwip_recv(fd, dst + outBytesRead, rem, MSG_DONTWAIT);
                if (r > 0) {
                    const size_t got = static_cast<size_t>(r);
                    outBytesRead += got;
                    bodyBytesDelivered += got;
                    if (hasContentLength && bodyBytesDelivered >= contentLength) {
                        streamEof = true;
                        return true;
                    }
                    continue;
                }
                if (r == 0) {
                    streamEof = true;
                    if (hasContentLength && bodyBytesDelivered < contentLength) {
                        sockError = true;
                        return false;
                    }
                    return true;
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                lastSockErrno = errno;
                sockError = true;
                return false;
            }
            return true;
        };

        if (!drainNonBlocking()) {
            return false;
        }
        if (outBytesRead == 0U && !streamEof && maxWaitMs > 0U && fd >= 0) {
            if (waitSocketReadableMs(fd, maxWaitMs, lastSockErrno)) {
                return drainNonBlocking();
            }
            if (lastSockErrno != 0) {
                sockError = true;
                return false;
            }
        }
        return true;
    }

    // `Transfer-Encoding: chunked` streaming path
    bool waitedOnce = false;
    while (outBytesRead < dstCap && !streamEof) {
        if (checkStop && checkStop(userCtx) != StreamStopReason::Continue) {
            return false;
        }
        if (rawOff < rawLen) {
            size_t consumed = 0U;
            const size_t produced = chunkDec.decode(
                rawBuf + rawOff,
                static_cast<size_t>(rawLen - rawOff),
                consumed,
                dst + outBytesRead,
                dstCap - outBytesRead
            );
            rawOff = static_cast<uint16_t>(rawOff + consumed);
            if (rawOff >= rawLen) {
                rawOff = 0U;
                rawLen = 0U;
            }
            outBytesRead += produced;
            bodyBytesDelivered += produced;
            if (chunkDec.hasError()) {
                sockError = true;
                return false;
            }
            if (chunkDec.isDone()) {
                streamEof = true;
                return true;
            }
            if (consumed == 0U && produced == 0U) {
                sockError = true;
                return false;
            }
            continue;
        }

        if (fd < 0) {
            streamEof = true;
            sockError = !chunkDec.isDone();
            return !sockError;
        }
        const int r = lwip_recv(fd, rawBuf, sizeof(rawBuf), MSG_DONTWAIT);
        if (r > 0) {
            rawOff = 0U;
            rawLen = static_cast<uint16_t>(r);
            continue;
        }
        if (r == 0) {
            streamEof = true;
            sockError = !chunkDec.isDone();
            return !sockError;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (outBytesRead > 0U || waitedOnce || maxWaitMs == 0U) {
                break;
            }
            waitedOnce = true;
            if (!waitSocketReadableMs(fd, maxWaitMs, lastSockErrno)) {
                if (lastSockErrno != 0) {
                    sockError = true;
                    return false;
                }
                break;
            }
            continue;
        }
        lastSockErrno = errno;
        sockError = true;
        return false;
    }
    return true;
#else
    (void)maxWaitMs;
    return false;
#endif
}

bool DmdzHttpStreamSession::pumpToRing(
    DmdzSpscByteRing& spscRing,
    uint32_t maxWaitMs,
    StreamStopReason (*checkStop)(void*),
    void* userCtx
) {
    if (sockError) {
        return false;
    }
    if (streamEof) {
        return true;
    }

    bool waitedOnce = false;
    while (!streamEof) {
        const size_t contig = spscRing.contiguousWriteCap();
        if (contig == 0U) {
            return true;
        }
        const size_t want = std::min<size_t>(contig, 16384U);
        size_t got = 0U;
        const bool ok = readSome(
            spscRing.writePtr(),
            want,
            got,
            waitedOnce ? 0U : maxWaitMs,
            checkStop,
            userCtx
        );
        waitedOnce = true;
        if (got > 0U) {
            spscRing.commitWrite(got);
        }
        if (!ok) {
            return false;
        }
        if (got == 0U) {
            break;
        }
    }
    return true;
}

void DmdzStreamClient::notifyHttpStreamPurge(
    const char* serverUrl,
    const char* streamPath,
    const char* clientId
) {
#if defined(ESP32) && !defined(UNIT_TEST)
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
    char deletePath[256] = "";
    if (!formatDmdzHttpDeletePath(rawPath, clientId, deletePath, sizeof(deletePath))) {
        return;
    }
    sendHttpDeleteOneShotAbort(host, port, deletePath, 800U, 300U);
#else
    (void)serverUrl;
    (void)streamPath;
    (void)clientId;
#endif
}

} // namespace net
