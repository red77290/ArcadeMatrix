#pragma once

#include "SecureTypes.h"
#include "SecureHttpResponse.h"
#include <memory>

namespace net {

struct SessionTransport;

/**
 * @class SecureHttpSession
 * @brief Manages a persistent or single-session HTTPS/TLS connection with keepalive support.
 *
 * Enforces Architectural Invariants:
 * - Invariant N1: TLS DRAM admission (NetworkBudget::canStartTlsSession) before any handshake.
 * - Invariant N2: ScopedTlsHandshakeLock held ONLY during connect(), released before request.
 * - Invariant N3: Zero intermediate HTTP response-body buffer (stream-only access).
 * - Invariant N4: Single active TLS session per object.
 * - Invariant N5: Full RAII lifecycle across all return/failure paths.
 * - Invariant N6: Explicit timeouts (WDT resets assist but do not replace timeouts).
 * - Invariant N7: Centralized network allocation (no direct WiFiClientSecure/HTTPClient in engines).
 */
class SecureHttpSession {
public:
    explicit SecureHttpSession(const String& host, uint16_t port = 443,
                               const SecureHttpOptions& options = {});
    ~SecureHttpSession();

    // Move-only semantics
    SecureHttpSession(SecureHttpSession&& other) noexcept;
    SecureHttpSession& operator=(SecureHttpSession&& other) noexcept;
    SecureHttpSession(const SecureHttpSession&) = delete;
    SecureHttpSession& operator=(const SecureHttpSession&) = delete;

    /**
     * @brief Current target host.
     */
    const String& host() const { return _host; }

    /**
     * @brief Current target port.
     */
    uint16_t port() const { return _port; }

    /**
     * @brief Current state of the session state machine.
     */
    SessionState state() const { return _state; }

    /**
     * @brief Last transport or admission error encountered.
     */
    TransportError lastError() const { return _lastError; }

    /**
     * @brief Checks if the underlying TLS socket is currently connected.
     */
    bool isConnected() const;

    /**
     * @brief Guarantees connection to the host.
     * Keepalive is an optimization, not a guarantee: reuses the active socket if healthy,
     * or performs a clean re-admission and handshake if disconnected.
     *
     * @return true if connected and ready for HTTP requests.
     */
    bool ensureConnected();

    /**
     * @brief Sends an HTTP GET request on the session.
     * If a previous response stream was not consumed, it is finalized automatically.
     *
     * @param path URL path and query string (e.g. "/api/v3/ticker/24hr?symbol=BTCUSDT")
     * @return SecureHttpResponse non-owning stream view.
     */
    SecureHttpResponse get(const String& path,
                           const std::vector<std::pair<String, String>>& headers = {});

    /**
     * @brief Sends an HTTP POST request on the session.
     */
    SecureHttpResponse post(const String& path, const String& contentType, const String& body,
                            const std::vector<std::pair<String, String>>& headers = {});

    /**
     * @brief Finalizes the active response transaction, draining unread bytes and
     * recycling the connection for subsequent requests.
     */
    void finalizeCurrentResponse();

    /**
     * @brief Closes the HTTP request and stops the TLS client.
     */
    void close();

    /**
     * @brief Testing hook to inject a mock transport for host native unit tests.
     */
    void setTransport(std::unique_ptr<SessionTransport> transport);

private:
    String _host;
    uint16_t _port;
    SecureHttpOptions _options;
    SessionState _state = SessionState::Disconnected;
    TransportError _lastError = TransportError::None;
    bool _hasActiveResponse = false;

    std::unique_ptr<SessionTransport> _transport;
};

} // namespace net
