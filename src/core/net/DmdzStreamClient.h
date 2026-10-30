#pragma once

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
    size_t ringCap
) {
    return (pendingWsCredits > 0U) &&
           (ringFree >= effectiveWsCreditMinFreeBytes(ringCap) || ringCount == 0U) &&
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
    const char* logTag = "Showcase";
    uint16_t ownerId = 0U;
    uint8_t wsCredits = 2U;
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
 * @class DmdzStreamClient
 * @brief Reusable zero-heap ZeDMD-style Credit-Acknowledged WebSocket (`DMDZ` v1) + Paced HTTP
 *        (`DMDZ`/`DMDS`) stream client with `SO_LINGER {1, 0}` zero-`TIME_WAIT` socket reclamation.
 */
class DmdzStreamClient {
public:
    static bool stream(const DmdzStreamParams& params);
};

} // namespace net
