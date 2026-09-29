#include "SecureHttpSession.h"
#include "../NetworkBudget.h"
#include <string.h>

#if defined(ARDUINO)
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>
#endif

namespace net {

struct SessionTransport {
    virtual ~SessionTransport() = default;
    virtual bool connect(const char* host, uint16_t port, uint32_t timeoutSec) = 0;
    virtual bool connected() = 0;
    virtual void stop() = 0;
    virtual bool begin(const String& host, uint16_t port, const String& path, bool https, const SecureHttpOptions& options) = 0;
    virtual void addHeader(const String& name, const String& value) = 0;
    virtual void setAuthorization(const char* user, const char* password) = 0;
    virtual int sendRequest(const char* method, const char* body = nullptr, const char* contentType = nullptr) = 0;
    virtual Stream* getStream() = 0;
    virtual int getSize() = 0;
    virtual void end() = 0;
};

#if defined(ARDUINO)
class EspSessionTransport : public SessionTransport {
public:
    EspSessionTransport() {
        _client.setInsecure();
    }

    bool connect(const char* host, uint16_t port, uint32_t timeoutSec) override {
        _client.setHandshakeTimeout(timeoutSec);
        return _client.connect(host, port);
    }

    bool connected() override {
        return _client.connected();
    }

    void stop() override {
        _client.stop();
    }

    bool begin(const String& host, uint16_t port, const String& path, bool https, const SecureHttpOptions& options) override {
        _http.setTimeout(options.requestTimeoutMs);
        _http.setUserAgent(options.userAgent);
        _http.setReuse(options.keepAlive);
        if (options.followRedirects) {
            _http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        }
        bool ok = _http.begin(_client, host, port, path, https);
        if (ok) {
            if (!options.authUser.isEmpty() && !options.authPassword.isEmpty()) {
                _http.setAuthorization(options.authUser.c_str(), options.authPassword.c_str());
            }
            for (const auto& h : options.customHeaders) {
                _http.addHeader(h.first, h.second);
            }
        }
        return ok;
    }

    void addHeader(const String& name, const String& value) override {
        _http.addHeader(name, value);
    }

    void setAuthorization(const char* user, const char* password) override {
        if (user && password) {
            _http.setAuthorization(user, password);
        }
    }

    int sendRequest(const char* method, const char* body = nullptr, const char* contentType = nullptr) override {
        if (strcmp(method, "POST") == 0) {
            if (contentType) _http.addHeader("Content-Type", contentType);
            return _http.POST(body ? body : "");
        }
        return _http.GET();
    }

    Stream* getStream() override {
        return _http.getStreamPtr();
    }

    int getSize() override {
        return _http.getSize();
    }

    void end() override {
        _http.end();
    }

private:
    WiFiClientSecure _client;
    HTTPClient _http;
};
#endif

SecureHttpSession::SecureHttpSession(const String& host, uint16_t port, const SecureHttpOptions& options)
    : _host(host), _port(port), _options(options) {
#if defined(ARDUINO)
    _transport.reset(new EspSessionTransport());
#endif
}

SecureHttpSession::~SecureHttpSession() {
    close();
}

SecureHttpSession::SecureHttpSession(SecureHttpSession&& other) noexcept
    : _host(std::move(other._host)),
      _port(other._port),
      _options(other._options),
      _state(other._state),
      _lastError(other._lastError),
      _hasActiveResponse(other._hasActiveResponse),
      _transport(std::move(other._transport)) {
    other._state = SessionState::Closed;
    other._hasActiveResponse = false;
}

SecureHttpSession& SecureHttpSession::operator=(SecureHttpSession&& other) noexcept {
    if (this != &other) {
        close();
        _host = std::move(other._host);
        _port = other._port;
        _options = other._options;
        _state = other._state;
        _lastError = other._lastError;
        _hasActiveResponse = other._hasActiveResponse;
        _transport = std::move(other._transport);

        other._state = SessionState::Closed;
        other._hasActiveResponse = false;
    }
    return *this;
}

void SecureHttpSession::setTransport(std::unique_ptr<SessionTransport> transport) {
    close();
    _transport = std::move(transport);
}

bool SecureHttpSession::isConnected() const {
    return _transport && _transport->connected();
}

bool SecureHttpSession::ensureConnected() {
    if (!_transport) {
        _lastError = TransportError::ConnectFailed;
        return false;
    }

    // Keepalive optimization: if active connection is healthy, reuse without new handshake
    if (_transport->connected()) {
        _state = SessionState::Connected;
        return true;
    }

    _state = SessionState::Connecting;

    // Invariant N1: TLS admission check before handshake
    if (!NetworkBudget::canStartTlsSession()) {
        _lastError = TransportError::TlsDenied;
        _state = SessionState::Disconnected;
        return false;
    }

    // Invariant N2: Handshake lock strictly limited to connect()
    {
        NetworkBudget::ScopedTlsHandshakeLock lock;
        if (!lock) {
            _lastError = lock.isDeniedByBudget() ? TransportError::TlsDenied
                                                : TransportError::LockContended;
            _state = SessionState::Disconnected;
            return false;
        }

#if defined(ARDUINO)
        esp_task_wdt_reset();
#endif
        bool connected = _transport->connect(_host.c_str(), _port, _options.handshakeTimeoutSec);
        if (!connected) {
            _lastError = TransportError::ConnectFailed;
            _state = SessionState::Disconnected;
            return false;
        }
        // 👉 lock destructor runs here: released immediately upon connect() success!
    }

#if defined(ARDUINO)
    esp_task_wdt_reset();
#endif
    _state = SessionState::Connected;
    _lastError = TransportError::None;
    return true;
}

SecureHttpResponse SecureHttpSession::get(const String& path,
                                         const std::vector<std::pair<String, String>>& headers) {
    uint32_t startMs = millis();

    // If a previous response stream is still unconsumed, finalize it first
    if (_hasActiveResponse) {
        finalizeCurrentResponse();
    }

    // Connect or reuse keepalive connection
    if (!ensureConnected()) {
        return SecureHttpResponse(this, -1, nullptr, 0, millis() - startMs, _lastError);
    }

#if defined(ARDUINO)
    esp_task_wdt_reset();
#endif

    bool beginOk = _transport->begin(_host, _port, path, (_port == 443), _options);
    if (!beginOk) {
        _lastError = TransportError::ConnectFailed;
        return SecureHttpResponse(this, -1, nullptr, 0, millis() - startMs, _lastError);
    }

    for (const auto& h : headers) {
        _transport->addHeader(h.first, h.second);
    }

    int statusCode = _transport->sendRequest("GET");
    uint32_t durationMs = millis() - startMs;
#if defined(ARDUINO)
    esp_task_wdt_reset();
#endif

    if (statusCode <= 0) {
        _lastError = TransportError::Timeout;
        _transport->end();
        return SecureHttpResponse(this, statusCode, nullptr, 0, durationMs, _lastError);
    }

    TransportError err = (statusCode >= 200 && statusCode < 300)
                         ? TransportError::None
                         : TransportError::HttpError;
    _lastError = err;
    _state = SessionState::ResponseActive;
    _hasActiveResponse = true;

    Stream* streamPtr = _transport->getStream();
    size_t len = (size_t)max(0, _transport->getSize());

    // Invariant N3: NO intermediate response-body buffer.
    return SecureHttpResponse(this, statusCode, streamPtr, len, durationMs, err);
}

SecureHttpResponse SecureHttpSession::post(const String& path, const String& contentType, const String& body,
                                           const std::vector<std::pair<String, String>>& headers) {
    uint32_t startMs = millis();

    if (_hasActiveResponse) {
        finalizeCurrentResponse();
    }

    if (!ensureConnected()) {
        return SecureHttpResponse(this, -1, nullptr, 0, millis() - startMs, _lastError);
    }

#if defined(ARDUINO)
    esp_task_wdt_reset();
#endif

    bool beginOk = _transport->begin(_host, _port, path, (_port == 443), _options);
    if (!beginOk) {
        _lastError = TransportError::ConnectFailed;
        return SecureHttpResponse(this, -1, nullptr, 0, millis() - startMs, _lastError);
    }

    for (const auto& h : headers) {
        _transport->addHeader(h.first, h.second);
    }

    int statusCode = _transport->sendRequest("POST", body.c_str(), contentType.c_str());
    uint32_t durationMs = millis() - startMs;
#if defined(ARDUINO)
    esp_task_wdt_reset();
#endif

    if (statusCode <= 0) {
        _lastError = TransportError::Timeout;
        _transport->end();
        return SecureHttpResponse(this, statusCode, nullptr, 0, durationMs, _lastError);
    }

    TransportError err = (statusCode >= 200 && statusCode < 300)
                         ? TransportError::None
                         : TransportError::HttpError;
    _lastError = err;
    _state = SessionState::ResponseActive;
    _hasActiveResponse = true;

    Stream* streamPtr = _transport->getStream();
    size_t len = (size_t)max(0, _transport->getSize());

    return SecureHttpResponse(this, statusCode, streamPtr, len, durationMs, err);
}

void SecureHttpSession::finalizeCurrentResponse() {
    if (!_hasActiveResponse || !_transport) return;

    // Drain remaining unread stream bytes to keep TCP pipe clean for keepalive
    Stream* streamPtr = _transport->getStream();
    if (streamPtr && _transport->connected()) {
        while (streamPtr->available() > 0) {
            streamPtr->read();
        }
    }

    _transport->end();
    _hasActiveResponse = false;
    _state = _transport->connected() ? SessionState::Connected : SessionState::Disconnected;
}

void SecureHttpSession::close() {
    finalizeCurrentResponse();
    if (_transport) {
        _transport->stop();
    }
    _state = SessionState::Closed;
}

} // namespace net
