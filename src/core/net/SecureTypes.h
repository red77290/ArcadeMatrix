#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <vector>
#include <utility>

namespace net {

/**
 * @enum TransportError
 * @brief Categorized transport and admission failures.
 */
enum class TransportError : uint8_t {
    None = 0,
    TlsDenied,           ///< DRAM admission rejected by NetworkBudget (prevents mbedTLS alloc crash)
    LockContended,       ///< Another TLS handshake is currently in flight on Core 0
    ConnectFailed,       ///< Physical TCP/TLS handshake failed or timed out
    HttpError,           ///< HTTP response status >= 400
    Timeout,             ///< Network stream read/write timeout
    StreamError,         ///< Premature socket closure or truncation during stream read
    InvalidUrl,          ///< Malformed HTTP/HTTPS URL
    ResponsePending,     ///< New request attempted while previous response is still active
    Closed,              ///< Session explicitly closed
    Aborted              ///< Invariant N8: Session aborted via client-side cancellation
};

inline const char* transportErrorToString(TransportError err) {
    switch (err) {
        case TransportError::None:            return "OK";
        case TransportError::TlsDenied:       return "TLS_DENIED_BY_BUDGET";
        case TransportError::LockContended:   return "TLS_HANDSHAKE_LOCK_CONTENDED";
        case TransportError::ConnectFailed:   return "CONNECT_FAILED";
        case TransportError::HttpError:       return "HTTP_ERROR";
        case TransportError::Timeout:         return "TIMEOUT";
        case TransportError::StreamError:     return "STREAM_ERROR";
        case TransportError::InvalidUrl:      return "INVALID_URL";
        case TransportError::ResponsePending: return "RESPONSE_PENDING";
        case TransportError::Closed:          return "SESSION_CLOSED";
        case TransportError::Aborted:         return "ABORTED";
        default:                              return "UNKNOWN_ERROR";
    }
}

/**
 * @enum SessionState
 * @brief Strict state machine tracking the TLS connection and active HTTP response.
 */
enum class SessionState : uint8_t {
    Disconnected = 0,    ///< Initial or closed state; no TCP/TLS socket
    Connecting,          ///< Handshake lock acquired, connect() in progress
    Connected,           ///< TLS connection established, idle and ready for requests
    ResponseActive,      ///< Request sent, response stream currently open and consumable
    Consumed,            ///< Response consumed/closed, socket ready for next request
    Closed               ///< Explicitly closed or unrecoverably severed
};

inline const char* sessionStateToString(SessionState state) {
    switch (state) {
        case SessionState::Disconnected:   return "Disconnected";
        case SessionState::Connecting:     return "Connecting";
        case SessionState::Connected:      return "Connected";
        case SessionState::ResponseActive: return "ResponseActive";
        case SessionState::Consumed:       return "Consumed";
        case SessionState::Closed:         return "Closed";
        default:                           return "Unknown";
    }
}

/**
 * @enum HttpOwnerId
 * @brief Well-known owner IDs for scoped network session abortion (Invariant N8).
 */
enum HttpOwnerId : uint16_t {
    OWNER_ANY = 0,
    OWNER_WEATHER = 100,
    OWNER_DASHBOARD = 200,
    OWNER_SPOTIFY = 300,
    OWNER_CRYPTO = 400,
    OWNER_STOCK = 500,
    OWNER_GNEWS = 600,
    OWNER_ARTWORK = 700
};

/**
 * @struct SecureHttpOptions
 * @brief Configuration parameters for TLS sessions and HTTP requests.
 */
struct SecureHttpOptions {
    uint32_t requestTimeoutMs = 6000;
    uint32_t handshakeTimeoutSec = 8;
    const char* userAgent = "Mozilla/5.0 (compatible; ArcadeMatrix/4.0)";
    bool keepAlive = true;
    bool followRedirects = false;
    uint16_t ownerId = OWNER_ANY; ///< Owner ID (engine or subsystem ID) for scoped abort
    String authUser;
    String authPassword;
    std::vector<std::pair<String, String>> customHeaders;
};

/**
 * @struct ParsedUrl
 * @brief Canonical URL decomposition helper.
 */
struct ParsedUrl {
    String protocol;
    String host;
    uint16_t port = 443;
    String path = "/";
    bool isHttps = true;

    static bool parse(const String& url, ParsedUrl& out) {
        if (url.length() == 0) return false;
        
        int protoEnd = url.indexOf("://");
        if (protoEnd < 0) return false;
        
        out.protocol = url.substring(0, protoEnd);
        out.protocol.toLowerCase();
        
        if (out.protocol == "https") {
            out.isHttps = true;
            out.port = 443;
        } else if (out.protocol == "http") {
            out.isHttps = false;
            out.port = 80;
        } else {
            return false;
        }
        
        int hostStart = protoEnd + 3;
        int pathStart = url.indexOf('/', hostStart);
        
        String hostPort;
        if (pathStart >= 0) {
            hostPort = url.substring(hostStart, pathStart);
            out.path = url.substring(pathStart);
        } else {
            hostPort = url.substring(hostStart);
            out.path = "/";
        }
        
        if (hostPort.length() == 0) return false;
        
        int colonIdx = hostPort.indexOf(':');
        if (colonIdx >= 0) {
            out.host = hostPort.substring(0, colonIdx);
            int p = hostPort.substring(colonIdx + 1).toInt();
            if (p > 0 && p <= 65535) {
                out.port = (uint16_t)p;
            } else {
                return false;
            }
        } else {
            out.host = hostPort;
        }
        
        return (out.host.length() > 0);
    }
};

} // namespace net
