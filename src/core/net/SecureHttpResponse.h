#pragma once

#include "SecureTypes.h"
#include <Stream.h>
#include <memory>

namespace net {

class SecureHttpSession;

/**
 * @class NullStream
 * @brief Fallback empty stream returned when response is invalid or already consumed.
 */
class NullStream : public Stream {
public:
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }
};

/**
 * @class SecureHttpResponse
 * @brief Non-owning active transaction view over an HTTP response stream.
 *
 * Implements Move-Only RAII semantics:
 * - Maintains reference to the active transaction on the owning SecureHttpSession.
 * - Destructor automatically closes/consumes any unconsumed stream data, ensuring
 *   the underlying HTTP connection is cleanly recycled or finalized.
 * - When created via one-shot SecureHttpClient::get(), retains ownership of a
 *   standalone session instance until destruction.
 */
class SecureHttpResponse {
public:
    SecureHttpResponse();
    
    // Internal constructor called by SecureHttpSession
    SecureHttpResponse(SecureHttpSession* owner, int statusCode, Stream* stream,
                       size_t contentLength, uint32_t durationMs, TransportError error);

    // Destructor guarantees response finalization
    ~SecureHttpResponse();

    // Move-only semantics
    SecureHttpResponse(SecureHttpResponse&& other) noexcept;
    SecureHttpResponse& operator=(SecureHttpResponse&& other) noexcept;
    SecureHttpResponse(const SecureHttpResponse&) = delete;
    SecureHttpResponse& operator=(const SecureHttpResponse&) = delete;

    /**
     * @brief Checks if response returned a 2xx HTTP status and no transport error.
     */
    bool ok() const {
        return (_statusCode >= 200 && _statusCode < 300 && _error == TransportError::None);
    }

    int statusCode() const { return _statusCode; }
    TransportError error() const { return _error; }
    const char* errorMessage() const { return transportErrorToString(_error); }
    size_t contentLength() const { return _contentLength; }
    uint32_t durationMs() const { return _durationMs; }
    bool isConsumed() const { return _consumed; }

    /**
     * @brief Direct response payload stream.
     * Guaranteed safe: returns NullStream if response failed, is null, or was consumed.
     * Can be passed directly to ArduinoJson: deserializeJson(doc, response.stream()).
     */
    Stream& stream();

    /**
     * @brief Reads and decodes the entire response body into a String.
     * Automatically handles HTTP chunked transfer encoding (Transfer-Encoding: chunked).
     * Consumes and finalizes the response.
     */
    String body();
    String getString() { return body(); }

    /**
     * @brief Explicitly consumes and drains the remaining response body,
     * resetting the underlying session for subsequent requests.
     */
    void consume();

    /**
     * @brief Alias for consume().
     */
    void close() { consume(); }

    /**
     * @brief Attaches a standalone session instance for self-contained one-shot requests.
     */
    void attachStandaloneSession(std::unique_ptr<SecureHttpSession> session);

private:
    SecureHttpSession* _owner = nullptr;
    Stream* _stream = nullptr;
    int _statusCode = -1;
    size_t _contentLength = 0;
    uint32_t _durationMs = 0;
    TransportError _error = TransportError::None;
    bool _consumed = false;
    std::unique_ptr<SecureHttpSession> _standaloneSession;

    static NullStream s_nullStream;
};

} // namespace net
