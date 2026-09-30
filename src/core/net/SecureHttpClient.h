#pragma once

#include "SecureTypes.h"
#include "SecureHttpSession.h"
#include "SecureHttpResponse.h"

namespace net {

/**
 * @class SecureHttpClient
 * @brief Factory for persistent multi-request sessions and one-shot HTTPS requests.
 *
 * Provides a unified entry point:
 * - Multi-request keepalive: SecureHttpClient::session("api.binance.com")
 * - One-shot self-contained: SecureHttpClient::get("https://gnews.io/api/...")
 * Both paths use the exact same underlying SecureHttpSession and transport invariants.
 */
class SecureHttpClient {
public:
    /**
     * @brief Factory creating a scoped keepalive session bound to a specific host.
     */
    static SecureHttpSession session(const char* host, uint16_t port = 443,
                                     const SecureHttpOptions& options = {});

    /**
     * @brief Executes a self-contained one-shot HTTP GET request.
     * Parses the full URL, creates an internal session, and preserves session ownership
     * inside the returned SecureHttpResponse until the response is consumed or destroyed.
     */
    static SecureHttpResponse get(const String& url,
                                  const SecureHttpOptions& options = {},
                                  const std::vector<std::pair<String, String>>& headers = {});

    /**
     * @brief Executes a self-contained one-shot HTTP POST request.
     */
    static SecureHttpResponse post(const String& url,
                                   const String& contentType,
                                   const String& body,
                                   const SecureHttpOptions& options = {},
                                   const std::vector<std::pair<String, String>>& headers = {});

    /**
     * @brief Invariant N8: Immediately aborts all active sessions owned by ownerId.
     */
    static void abortSessionsOwnedBy(uint16_t ownerId);

    /**
     * @brief Invariant N8: Emergency abort of all active HTTP/TLS sessions system-wide.
     */
    static void abortAllActiveSessions();

    /**
     * @brief Internal registration hooks for active sessions tracking.
     */
    static void registerSession(SecureHttpSession* session);
    static void unregisterSession(SecureHttpSession* session);
};

} // namespace net
