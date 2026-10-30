#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

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
