#include "SecureHttpClient.h"

namespace net {

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
