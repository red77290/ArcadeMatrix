#pragma once

#include "../drawing/DmdzStreamDecoder.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace net {

/**
 * @struct StreamByteRing
 * @brief Zero-heap circular byte ring buffer backed by caller-owned scratch memory (`ioScratch`).
 *        Stores already-demuxed stream payload bytes (`DMDZ` / `DMDS`) so Core 0 continuously drains
 *        Wi-Fi / lwIP RX packets during `dueMs` frame pacing and decodes frames from RAM in `< 0.5 ms`.
 */
struct StreamByteRing {
    uint8_t* buf = nullptr;
    size_t cap = 0U;
    size_t head = 0U;
    size_t tail = 0U;
    size_t count = 0U;

    void init(uint8_t* storage, size_t capacity) {
        buf = storage;
        cap = storage ? capacity : 0U;
        head = 0U;
        tail = 0U;
        count = 0U;
    }

    void initFromSlice(uint8_t* storage, size_t capacity, size_t offset, size_t len) {
        init(storage, capacity);
        if (!buf || cap == 0U || offset >= cap || len == 0U) {
            return;
        }
        const size_t clampedLen = (len <= (cap - offset)) ? len : (cap - offset);
        head = offset;
        count = clampedLen;
        tail = (offset + clampedLen) % cap;
    }

    size_t size() const { return count; }
    size_t capacity() const { return cap; }
    size_t freeBytes() const { return (cap > count) ? (cap - count) : 0U; }
    bool empty() const { return count == 0U; }
    bool full() const { return cap == 0U || count >= cap; }

    uint8_t* writePtr() { return buf ? (buf + tail) : nullptr; }
    const uint8_t* readPtr() const { return buf ? (buf + head) : nullptr; }

    size_t contiguousReadCap() const {
        if (!buf || count == 0U || cap == 0U) {
            return 0U;
        }
        const size_t toEnd = cap - head;
        return (count < toEnd) ? count : toEnd;
    }

    void consumeRead(size_t n) {
        if (!buf || cap == 0U || n == 0U || count == 0U) {
            return;
        }
        const size_t adv = (n <= count) ? n : count;
        head = (head + adv) % cap;
        count -= adv;
    }

    size_t contiguousWriteCap() const {
        if (!buf || count >= cap) {
            return 0U;
        }
        const size_t space = cap - count;
        const size_t toEnd = cap - tail;
        return (space < toEnd) ? space : toEnd;
    }

    void commitWrite(size_t n) {
        if (!buf || cap == 0U || n == 0U) {
            return;
        }
        const size_t space = freeBytes();
        const size_t adv = (n <= space) ? n : space;
        tail = (tail + adv) % cap;
        count += adv;
    }

    size_t push(const uint8_t* src, size_t len) {
        if (!buf || !src || cap == 0U || len == 0U || count >= cap) {
            return 0U;
        }
        const size_t space = cap - count;
        const size_t take = (len <= space) ? len : space;
        const size_t first = ((cap - tail) < take) ? (cap - tail) : take;
        memcpy(buf + tail, src, first);
        if (take > first) {
            memcpy(buf, src + first, take - first);
        }
        tail = (tail + take) % cap;
        count += take;
        return take;
    }

    size_t pop(uint8_t* dst, size_t len) {
        if (!buf || cap == 0U || len == 0U || count == 0U) {
            return 0U;
        }
        const size_t take = (len <= count) ? len : count;
        const size_t first = ((cap - head) < take) ? (cap - head) : take;
        if (dst) {
            memcpy(dst, buf + head, first);
            if (take > first) {
                memcpy(dst + first, buf, take - first);
            }
        }
        head = (head + take) % cap;
        count -= take;
        return take;
    }

    size_t peek(uint8_t* dst, size_t len, size_t offset = 0U) const {
        if (!buf || !dst || cap == 0U || len == 0U || offset >= count) {
            return 0U;
        }
        const size_t avail = count - offset;
        const size_t take = (len <= avail) ? len : avail;
        const size_t start = (head + offset) % cap;
        const size_t first = ((cap - start) < take) ? (cap - start) : take;
        memcpy(dst, buf + start, first);
        if (take > first) {
            memcpy(dst + first, buf, take - first);
        }
        return take;
    }
};

/**
 * @struct DmdzSpscByteRing
 * @brief Lock-free Single-Producer (Core 0) / Single-Consumer (Core 1) circular byte ring.
 *
 * Uses `[0, 2 * cap)` Lamport indices (`std::atomic<size_t> head`, `std::atomic<size_t> tail`)
 * with `acquire`/`release` memory ordering:
 * - Zero mutexes, zero integer divisions on Core 1, zero 32-bit monotonic wrap discontinuities.
 * - Full 100% capacity utilization (`0 .. cap` bytes) for arbitrary non-power-of-two sizes
 *   (`6,144 B`, `8,192 B`, `10,240 B`, `256 KB`, `768 KB`).
 */
struct DmdzSpscByteRing {
    uint8_t* buf = nullptr;
    size_t cap = 0U;
    std::atomic<size_t> head{0U};
    std::atomic<size_t> tail{0U};

    inline size_t offsetOf(size_t idx) const {
        return (idx >= cap) ? (idx - cap) : idx;
    }

    inline size_t advanceIdx(size_t idx, size_t n) const {
        const size_t twoCap = cap * 2U;
        if (twoCap == 0U) {
            return 0U;
        }
        size_t next = idx + n;
        if (next >= twoCap) {
            next -= twoCap;
        }
        return next;
    }

    inline size_t usedFrom(size_t h, size_t t) const {
        if (cap == 0U) {
            return 0U;
        }
        return (t >= h) ? (t - h) : ((cap * 2U) - h + t);
    }

    void init(uint8_t* storage, size_t capacity) {
        buf = storage;
        cap = (storage && capacity > 0U) ? capacity : 0U;
        head.store(0U, std::memory_order_release);
        tail.store(0U, std::memory_order_release);
    }

    void reset() {
        head.store(0U, std::memory_order_release);
        tail.store(0U, std::memory_order_release);
    }

    size_t capacity() const { return cap; }

    size_t size() const {
        const size_t h = head.load(std::memory_order_acquire);
        const size_t t = tail.load(std::memory_order_acquire);
        return usedFrom(h, t);
    }

    size_t freeBytes() const {
        const size_t used = size();
        return (cap > used) ? (cap - used) : 0U;
    }

    size_t producerSize() const {
        const size_t t = tail.load(std::memory_order_relaxed);
        const size_t h = head.load(std::memory_order_acquire);
        return usedFrom(h, t);
    }

    size_t producerFreeBytes() const {
        const size_t used = producerSize();
        return (cap > used) ? (cap - used) : 0U;
    }

    size_t consumerSize() const {
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        return usedFrom(h, t);
    }

    bool empty() const { return size() == 0U; }
    bool full() const { return cap == 0U || size() >= cap; }

    uint8_t* writePtr() {
        if (!buf || cap == 0U) {
            return nullptr;
        }
        return buf + offsetOf(tail.load(std::memory_order_relaxed));
    }

    size_t contiguousWriteCap() const {
        if (!buf || cap == 0U) {
            return 0U;
        }
        const size_t t = tail.load(std::memory_order_relaxed);
        const size_t h = head.load(std::memory_order_acquire);
        const size_t used = usedFrom(h, t);
        if (used >= cap) {
            return 0U;
        }
        const size_t space = cap - used;
        const size_t off = offsetOf(t);
        const size_t toEnd = cap - off;
        return (space < toEnd) ? space : toEnd;
    }

    void commitWrite(size_t n) {
        if (!buf || cap == 0U || n == 0U) {
            return;
        }
        const size_t t = tail.load(std::memory_order_relaxed);
        const size_t h = head.load(std::memory_order_acquire);
        const size_t space = cap - usedFrom(h, t);
        const size_t adv = (n <= space) ? n : space;
        tail.store(advanceIdx(t, adv), std::memory_order_release);
    }

    size_t push(const uint8_t* src, size_t len) {
        if (!buf || !src || cap == 0U || len == 0U) {
            return 0U;
        }
        const size_t t = tail.load(std::memory_order_relaxed);
        const size_t h = head.load(std::memory_order_acquire);
        const size_t used = usedFrom(h, t);
        if (used >= cap) {
            return 0U;
        }
        const size_t space = cap - used;
        const size_t take = (len <= space) ? len : space;
        const size_t off = offsetOf(t);
        const size_t first = ((cap - off) < take) ? (cap - off) : take;
        memcpy(buf + off, src, first);
        if (take > first) {
            memcpy(buf, src + first, take - first);
        }
        tail.store(advanceIdx(t, take), std::memory_order_release);
        return take;
    }

    const uint8_t* readPtr() const {
        if (!buf || cap == 0U) {
            return nullptr;
        }
        return buf + offsetOf(head.load(std::memory_order_relaxed));
    }

    size_t contiguousReadCap() const {
        if (!buf || cap == 0U) {
            return 0U;
        }
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        const size_t avail = usedFrom(h, t);
        if (avail == 0U) {
            return 0U;
        }
        const size_t off = offsetOf(h);
        const size_t toEnd = cap - off;
        return (avail < toEnd) ? avail : toEnd;
    }

    void consumeRead(size_t n) {
        if (!buf || cap == 0U || n == 0U) {
            return;
        }
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        const size_t avail = usedFrom(h, t);
        const size_t adv = (n <= avail) ? n : avail;
        head.store(advanceIdx(h, adv), std::memory_order_release);
    }

    size_t pop(uint8_t* dst, size_t len) {
        if (!buf || cap == 0U || len == 0U) {
            return 0U;
        }
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        const size_t avail = usedFrom(h, t);
        if (avail == 0U) {
            return 0U;
        }
        const size_t take = (len <= avail) ? len : avail;
        const size_t off = offsetOf(h);
        const size_t first = ((cap - off) < take) ? (cap - off) : take;
        if (dst) {
            memcpy(dst, buf + off, first);
            if (take > first) {
                memcpy(dst + first, buf, take - first);
            }
        }
        head.store(advanceIdx(h, take), std::memory_order_release);
        return take;
    }

    size_t peek(uint8_t* dst, size_t len, size_t offset = 0U) const {
        if (!buf || !dst || cap == 0U || len == 0U) {
            return 0U;
        }
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        const size_t avail = usedFrom(h, t);
        if (offset >= avail) {
            return 0U;
        }
        const size_t rem = avail - offset;
        const size_t take = (len <= rem) ? len : rem;
        const size_t startOff = offsetOf(advanceIdx(h, offset));
        const size_t first = ((cap - startOff) < take) ? (cap - startOff) : take;
        memcpy(dst, buf + startOff, first);
        if (take > first) {
            memcpy(dst + first, buf, take - first);
        }
        return take;
    }

    bool peekFrameLength(uint16_t& outPayloadLen) const {
        if (!buf || cap == 0U) {
            return false;
        }
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        if (usedFrom(h, t) < 2U) {
            return false;
        }
        const size_t off0 = offsetOf(h);
        const size_t off1 = (off0 + 1U == cap) ? 0U : (off0 + 1U);
        outPayloadLen = static_cast<uint16_t>(buf[off0]) |
                        (static_cast<uint16_t>(buf[off1]) << 8);
        return true;
    }

    bool hasCompleteFrame(uint16_t& outPayloadLen) const {
        if (!buf || cap == 0U) {
            return false;
        }
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        const size_t avail = usedFrom(h, t);
        if (avail < 2U) {
            return false;
        }
        const size_t off0 = offsetOf(h);
        const size_t off1 = (off0 + 1U == cap) ? 0U : (off0 + 1U);
        outPayloadLen = static_cast<uint16_t>(buf[off0]) |
                        (static_cast<uint16_t>(buf[off1]) << 8);
        return avail >= (static_cast<size_t>(2U) + static_cast<size_t>(outPayloadLen));
    }

    void getConsumerSlices(
        size_t offset,
        size_t len,
        const uint8_t*& outPtr1,
        size_t& outLen1,
        const uint8_t*& outPtr2,
        size_t& outLen2
    ) const {
        outPtr1 = nullptr;
        outLen1 = 0U;
        outPtr2 = nullptr;
        outLen2 = 0U;
        if (!buf || cap == 0U || len == 0U) {
            return;
        }
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        const size_t avail = usedFrom(h, t);
        if (offset >= avail) {
            return;
        }
        const size_t rem = avail - offset;
        const size_t take = (len <= rem) ? len : rem;
        const size_t startOff = offsetOf(advanceIdx(h, offset));
        const size_t first = ((cap - startOff) < take) ? (cap - startOff) : take;
        outPtr1 = buf + startOff;
        outLen1 = first;
        if (take > first) {
            outPtr2 = buf;
            outLen2 = take - first;
        }
    }
};

/**
 * @brief Selects the 3-tier Showcase adaptive stream profile (`"sram"`, `"psram"`, `"psram_hd"`).
 */
inline constexpr const char* selectDmdzStreamProfile(bool hasPsram, uint16_t width, uint16_t height) {
    return !hasPsram
        ? "sram"
        : (((static_cast<uint32_t>(width) * static_cast<uint32_t>(height)) <= 8192U) ? "psram" : "psram_hd");
}

/**
 * @brief Minimum free space (`1,536 B`) required in `DmdzSpscByteRing` before granting additional
 *        `'A'` WebSocket credits when unread compressed frames are already buffered.
 */
static constexpr size_t kSpscWsCreditMinRingFreeBytes = 1536U;

/**
 * @brief Minimum free space required in `StreamByteRing` (`>= 1 WS chunk` + headroom) before
 *        granting additional `'A'` WebSocket credits when unread frames are already buffered.
 */
static constexpr size_t kWsCreditMinRingFreeBytes = 1152U;

inline constexpr uint8_t clampWsCredits(uint8_t credits) {
    return (credits < 1U) ? 1U : ((credits > 16U) ? 16U : credits);
}

/**
 * @brief Computes WebSocket `'A'` credit ACK batch size:
 *        - `wsCredits = 2` (`!hasPsram` `esp32dev`) -> `ackBatch = 1`
 *        - `wsCredits = 8` (`hasPsram` `esp32s3`)   -> `ackBatch = 2`
 */
inline constexpr uint8_t computeWsAckBatch(uint8_t clampedCredits) {
    return (clampedCredits >= 4U) ? 2U : 1U;
}

inline constexpr size_t effectiveWsCreditMinFreeBytes(size_t ringCap) {
    return (ringCap >= 2048U) ? kWsCreditMinRingFreeBytes : (ringCap / 2U);
}

/**
 * @brief Deadlock-free WebSocket credit backpressure predicate:
 *        - Withholds `'A'` credits when `ringFree < minFree` AND `ringCount > 0` so at most 0-1
 *          packets can ever sit in `esp_wifi` / lwIP while `StreamByteRing` holds unread frames.
 *        - Always permits flushing when `ringCount == 0` so oversized keyframes (`> ringCap`)
 *          actively consumed by Core 0 stream through with zero deadlock.
 */
inline constexpr bool canFlushWsCredits(
    uint16_t pendingWsCredits,
    uint8_t ackBatch,
    bool forceAll,
    size_t ringCount,
    size_t ringFree,
    size_t ringCap,
    size_t minFreeOverride = 0U
) {
    return (pendingWsCredits > 0U) &&
           (ringFree >= ((minFreeOverride > 0U) ? minFreeOverride : effectiveWsCreditMinFreeBytes(ringCap)) ||
            ringCount == 0U) &&
           (pendingWsCredits >= (forceAll ? 1U : ((ackBatch > 0U) ? static_cast<uint16_t>(ackBatch) : 1U)));
}

/**
 * @struct HttpChunkedDecoder
 * @brief Zero-heap streaming RFC 9112 HTTP/1.1 `Transfer-Encoding: chunked` decoder.
 *        Strips `<hex>[;ext]\r\n` headers and `\r\n` trailers in bulk `memcpy` slices
 *        when a server or reverse proxy omits `Content-Length`.
 */
struct HttpChunkedDecoder {
    enum class State : uint8_t {
        SizeHex = 0,
        SizeExt = 1,
        SizeLf = 2,
        Payload = 3,
        PayloadCr = 4,
        PayloadLf = 5,
        Done = 6,
        Error = 7
    };

    State state = State::SizeHex;
    size_t chunkRem = 0;
    uint8_t hexDigits = 0;

    void reset() {
        state = State::SizeHex;
        chunkRem = 0;
        hexDigits = 0;
    }

    bool isDone() const { return state == State::Done; }
    bool hasError() const { return state == State::Error; }

    size_t decode(
        const uint8_t* src,
        size_t srcLen,
        size_t& outSrcConsumed,
        uint8_t* dst,
        size_t dstCap
    );
};

/**
 * @brief Case-insensitive check for `Transfer-Encoding: ...chunked...` in raw HTTP headers.
 */
bool hasChunkedTransferEncoding(const char* rawHeaders);

/**
 * @brief Extracts host, port, and scheme flag from an `http://`, `https://`, `mqtt://`, or `tcp://` URL
 *        with zero heap allocations.
 */
bool parseEndpointHostAndPort(
    const char* rawUrl,
    uint16_t defaultPort,
    char* outHost,
    size_t hostCap,
    uint16_t& outPort,
    bool* outIsHttps = nullptr
);

enum class StreamStopReason : uint8_t {
    Continue = 0,
    Deactivated = 1,
    WebUiYield = 2,
    ConfigChanged = 3
};

/**
 * @struct DmdzStreamParams
 * @brief Zero-overhead POD configuration & callback table for `DmdzStreamClient::stream()`.
 *        Shared by `MediaShowcaseBase` (autonomous Showcase engines) and `FrontendSyncEngine`
 *        (late-preempting In-Game Showcase sequences).
 */
struct DmdzStreamParams {
    const char* serverUrl = nullptr;
    const char* streamPath = nullptr;
    const char* clientId = nullptr;
    const char* profile = nullptr;
    const char* logTag = "Showcase";
    uint16_t ownerId = 0U;
    uint8_t wsCredits = 2U;
    uint16_t wsChunkBytes = 0U;
    uint16_t panelWidth = 128U;
    uint16_t panelHeight = 32U;
    uint32_t configuredStreamMs = 12000U;
    size_t maxFramePixels = 0U;
    uint16_t* frameBuf0 = nullptr;
    uint16_t* frameBuf1 = nullptr;
    char* ioScratch = nullptr;
    size_t ioScratchCap = 0U;

    void* userCtx = nullptr;
    StreamStopReason (*checkStop)(void* ctx) = nullptr;
    void (*onBeginFrameWrite)(
        void* ctx,
        uint16_t frameIdx,
        uint16_t* dstBuf,
        bool singleBuffer,
        uint32_t safeFps
    ) = nullptr;
    void (*onFinishFrameWrite)(
        void* ctx,
        uint16_t frameIdx,
        const uint16_t* dstBuf,
        uint16_t width,
        uint16_t height,
        bool okFrame,
        bool singleBuffer
    ) = nullptr;
    uint32_t (*getPresentedFrames)(void* ctx) = nullptr;
    void (*onNextFrameDueMs)(void* ctx, uint32_t dueMs) = nullptr;
};

/**
 * @brief Formats a Showcase HTTP `.rgb565.stream` request path with explicit `purge`, `paced`,
 *        `codec=dmdz2`, optional `client_id`, preserved `api_key`, and adaptive `profile`.
 */
bool formatDmdzHttpStreamPath(
    const char* rawPath,
    const char* clientId,
    bool purge,
    bool paced,
    const char* profile,
    char* outPath,
    size_t outCap
);

/**
 * @struct DmdzSpscStreamSession
 * @brief Stateful Core 0 producer session pumping compressed `DMDZ` frames over credit-acked
 *        WebSocket (or paced HTTP fallback) directly into a lock-free `DmdzSpscByteRing` for
 *        Core 1 Zero-Copy `ScopedCanvasGuard` decoding.
 *
 * Supports pre-connecting ~300 ms before `Phase::Card` ends so 7-12 compressed frames are
 * pre-buffered in `DmdzSpscByteRing` before `Phase::LiveStream` begins.
 */
struct DmdzSpscStreamSession {
    int fd = -1;
    char resolvedHost[64] = "";
    char activePath[288] = "";
    uint16_t resolvedPort = 8890U;
    uint16_t pendingWsCredits = 0U;
    uint8_t ackBatch = 1U;
    bool isWebSocket = false;
    bool isChunked = false;
    bool serverPaced = false;
    bool streamEof = false;
    int lastSockErrno = 0;
    const char* lastFailReason = "eof";
    core::drawing::DmdzStreamHeader header{};
    core::drawing::DmdzWsDemuxer wsDemux{};
    HttpChunkedDecoder chunkDec{};
    uint8_t rawRxBuf[704] = {0};

    ~DmdzSpscStreamSession() {
        close(false);
    }

    bool open(
        const char* serverUrl,
        const char* streamPath,
        const char* clientId,
        const char* profile,
        uint8_t wsCredits,
        uint16_t wsChunkBytes,
        DmdzSpscByteRing& spscRing,
        StreamStopReason (*checkStop)(void*),
        void* userCtx,
        const char* logTag = "Showcase"
    );

    bool pump(
        DmdzSpscByteRing& spscRing,
        uint32_t maxWaitMs,
        StreamStopReason (*checkStop)(void*),
        void* userCtx
    );

    void close(bool sendGracefulWsClose);
};

/**
 * @struct DmdzPreloadSession
 * @brief Stateful Core 0 HTTP bulk preload / progressive download session (`?purge=false&paced=0&codec=dmdz2`)
 *        writing compressed `DMDZ` bytes into PSRAM (`<= 1.5 MB` Tier 2 full preload, `<= 3.5 MB` Tier 3
 *        progressive preload with `16 KB` socket reads).
 */
struct DmdzPreloadSession {
    int fd = -1;
    char resolvedHost[64] = "";
    char activePath[288] = "";
    uint16_t resolvedPort = 8890U;
    uint8_t* preloadBuf = nullptr;
    uint8_t* chunkScratch = nullptr;
    size_t preloadCap = 0U;
    size_t contentLength = 0U;
    size_t scanOffset = 16U;
    uint32_t framesBuffered = 0U;
    bool hasContentLength = false;
    bool isChunked = false;
    bool headerParsed = false;
    bool tooLargeForPreload = false;
    bool streamEof = false;
    int lastSockErrno = 0;
    core::drawing::DmdzStreamHeader header{};
    HttpChunkedDecoder chunkDec{};

    ~DmdzPreloadSession() {
        closeSocket();
        if (chunkScratch) {
            free(chunkScratch);
            chunkScratch = nullptr;
        }
    }

    bool open(
        const char* serverUrl,
        const char* streamPath,
        const char* clientId,
        const char* profile,
        size_t maxPreloadBytes,
        std::atomic<size_t>& outBytesDownloaded,
        std::atomic<bool>& outDownloadComplete,
        StreamStopReason (*checkStop)(void*),
        void* userCtx,
        const char* logTag = "Showcase"
    );

    bool pump(
        std::atomic<size_t>& outBytesDownloaded,
        std::atomic<bool>& outDownloadComplete,
        uint32_t maxWaitMs,
        StreamStopReason (*checkStop)(void*),
        void* userCtx
    );

    void closeSocket();
    void releasePreloadBuffer();
};

/**
 * @class DmdzStreamClient
 * @brief Reusable zero-heap ZeDMD-style Credit-Acknowledged WebSocket (`DMDZ` v1/v2) + Paced HTTP
 *        (`DMDZ`/`DMDS`) stream client with `SO_LINGER {1, 0}` zero-`TIME_WAIT` socket reclamation.
 */
class DmdzStreamClient {
public:
    static bool stream(const DmdzStreamParams& params);

    static void notifyHttpStreamPurge(
        const char* serverUrl,
        const char* streamPath,
        const char* clientId,
        const char* profile
    );
};

} // namespace net
