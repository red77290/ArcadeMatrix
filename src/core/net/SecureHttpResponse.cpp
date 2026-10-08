#include "SecureHttpResponse.h"
#include "SecureHttpSession.h"

namespace net {

NullStream SecureHttpResponse::s_nullStream;

SecureHttpResponse::SecureHttpResponse()
    : _owner(nullptr), _stream(&s_nullStream), _statusCode(-1),
      _contentLength(0), _durationMs(0), _error(TransportError::None), _consumed(true) {}

SecureHttpResponse::SecureHttpResponse(SecureHttpSession* owner, int statusCode, Stream* stream,
                                       size_t contentLength, uint32_t durationMs, TransportError error)
    : _owner(owner),
      _stream(stream ? stream : &s_nullStream),
      _statusCode(statusCode),
      _contentLength(contentLength),
      _durationMs(durationMs),
      _error(error),
      _consumed(false) {}

SecureHttpResponse::~SecureHttpResponse() {
    close();
}

SecureHttpResponse::SecureHttpResponse(SecureHttpResponse&& other) noexcept
    : _owner(other._owner),
      _stream(other._stream),
      _statusCode(other._statusCode),
      _contentLength(other._contentLength),
      _durationMs(other._durationMs),
      _error(other._error),
      _consumed(other._consumed),
      _standaloneSession(std::move(other._standaloneSession)) {
    // Invalidate other to prevent double-consumption or dangling callback
    other._owner = nullptr;
    other._stream = &s_nullStream;
    other._statusCode = -1;
    other._consumed = true;
}

SecureHttpResponse& SecureHttpResponse::operator=(SecureHttpResponse&& other) noexcept {
    if (this != &other) {
        close();
        _owner = other._owner;
        _stream = other._stream;
        _statusCode = other._statusCode;
        _contentLength = other._contentLength;
        _durationMs = other._durationMs;
        _error = other._error;
        _consumed = other._consumed;
        _standaloneSession = std::move(other._standaloneSession);

        other._owner = nullptr;
        other._stream = &s_nullStream;
        other._statusCode = -1;
        other._consumed = true;
    }
    return *this;
}

Stream& SecureHttpResponse::stream() {
    if (_consumed || !_stream || (_owner && _owner->isAborted())) {
        return s_nullStream;
    }
    return *_stream;
}

String SecureHttpResponse::body() {
    if (_consumed || (_owner && _owner->isAborted())) {
        return "";
    }
    _consumed = true;
    String res;
    if (_owner) {
        res = _owner->readResponseBodyString();
    } else if (_stream) {
        res = _stream->readString();
    }
    _stream = &s_nullStream;
    return res;
}

void SecureHttpResponse::consume() {
    if (_consumed) return;
    _consumed = true;
    if (_owner) {
        _owner->finalizeCurrentResponse();
    }
    _stream = &s_nullStream;
}

void SecureHttpResponse::attachStandaloneSession(std::unique_ptr<SecureHttpSession> session) {
    _standaloneSession = std::move(session);
    if (_standaloneSession) {
        _owner = _standaloneSession.get();
    }
}

} // namespace net
