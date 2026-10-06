#include "SecureHttpClient.h"
#include <mutex>
#include <vector>
#include <algorithm>

namespace net {

static std::vector<SecureHttpSession*> s_activeSessions;
static std::mutex s_sessionMutex;

void SecureHttpClient::registerSession(SecureHttpSession* session) {
    if (!session) return;
    std::lock_guard<std::mutex> lock(s_sessionMutex);
    s_activeSessions.push_back(session);
}

void SecureHttpClient::unregisterSession(SecureHttpSession* session) {
    if (!session) return;
    std::lock_guard<std::mutex> lock(s_sessionMutex);
    auto it = std::find(s_activeSessions.begin(), s_activeSessions.end(), session);
    if (it != s_activeSessions.end()) {
        s_activeSessions.erase(it);
    }
}

void SecureHttpClient::abortSessionsOwnedBy(uint16_t ownerId) {
    std::vector<SecureHttpSession*> toAbort;
    {
        std::lock_guard<std::mutex> lock(s_sessionMutex);
        for (auto* s : s_activeSessions) {
            if (s && (ownerId == 0 || s->ownerId() == ownerId)) {
                toAbort.push_back(s);
            }
        }
    }
    // Abort outside of mutex lock to prevent lock inversion with transport callbacks
    for (auto* s : toAbort) {
        s->abort();
    }
}

void SecureHttpClient::abortAllActiveSessions() {
    abortSessionsOwnedBy(0);
}

SecureHttpSession SecureHttpClient::session(const char* host, uint16_t port,
                                           const SecureHttpOptions& options) {
    return SecureHttpSession(host ? host : "", port, options);
}

SecureHttpResponse SecureHttpClient::get(const String& url,
                                         const SecureHttpOptions& options,
                                         const std::vector<std::pair<String, String>>& headers) {
    ParsedUrl parsed;
    if (!ParsedUrl::parse(url, parsed)) {
        return SecureHttpResponse(nullptr, -1, nullptr, 0, 0, TransportError::InvalidUrl);
    }

    std::unique_ptr<SecureHttpSession> sess(new SecureHttpSession(parsed.host, parsed.port, options));
    auto response = sess->get(parsed.path, headers);
    response.attachStandaloneSession(std::move(sess));
    return response;
}

SecureHttpResponse SecureHttpClient::post(const String& url,
                                          const String& contentType,
                                          const String& body,
                                          const SecureHttpOptions& options,
                                          const std::vector<std::pair<String, String>>& headers) {
    ParsedUrl parsed;
    if (!ParsedUrl::parse(url, parsed)) {
        return SecureHttpResponse(nullptr, -1, nullptr, 0, 0, TransportError::InvalidUrl);
    }

    std::unique_ptr<SecureHttpSession> sess(new SecureHttpSession(parsed.host, parsed.port, options));
    auto response = sess->post(parsed.path, contentType, body, headers);
    response.attachStandaloneSession(std::move(sess));
    return response;
}

} // namespace net
