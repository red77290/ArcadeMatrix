#include "ShowcaseDiscovery.h"
#include "../Logger.h"
#include "../NetworkBudget.h"
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>

#if defined(ESP32) && !defined(UNIT_TEST)
#include "../ConfigLoader.h"
#include "../../hal/HardwareHAL.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <fcntl.h>
#include <errno.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
extern ConfigLoader config;
#endif

namespace net {

namespace {

void copySafe(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0U) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = 0U;
    while (n + 1U < cap && src[n] != '\0') {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

bool isValidCandidateIpv4(const char* s) {
    if (!s || s[0] == '\0') return false;
    if (strcmp(s, "0.0.0.0") == 0 ||
        strcmp(s, "255.255.255.255") == 0 ||
        strncmp(s, "127.", 4) == 0 ||
        strcasecmp(s, "localhost") == 0) {
        return false;
    }
    int dots = 0;
    int partVal = 0;
    int partDigits = 0;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p == '.') {
            if (partDigits == 0 || partVal > 255) return false;
            dots++;
            partVal = 0;
            partDigits = 0;
        } else if (*p >= '0' && *p <= '9') {
            partVal = partVal * 10 + (*p - '0');
            partDigits++;
            if (partDigits > 3 || partVal > 255) return false;
        } else {
            return false;
        }
    }
    return (dots == 3 && partDigits > 0 && partVal <= 255);
}

struct CachedDiscoveryState {
    std::atomic<uint32_t> seq{0};
    char ip[24] = "";
    char version[24] = "";
    uint16_t port = 0;
    uint32_t resolvedAtMs = 0;
    bool valid = false;
};

struct CandidateRing {
    std::atomic<uint32_t> seq{0};
    char ips[6][20] = {{0}};
    uint8_t count = 0;
};

static constexpr uint32_t kPositiveCacheTtlMs = 120000U;
#if defined(ESP32) && !defined(UNIT_TEST)
static constexpr uint32_t kNegativeScanBackoffMs = 5000U;
static constexpr size_t kMinDiscoveryHeapFloorBytes = 6144U;
#endif

CachedDiscoveryState s_cache;
CandidateRing s_candidates;

bool readCache(uint16_t expectedPort, uint32_t maxAgeMs, char* outIp, size_t ipCap, char* outVer, size_t verCap) {
    for (int i = 0; i < 4; ++i) {
        uint32_t s1 = s_cache.seq.load(std::memory_order_acquire);
        if ((s1 & 1U) != 0U) continue;
        bool valid = s_cache.valid;
        uint16_t port = s_cache.port;
        uint32_t ts = s_cache.resolvedAtMs;
        char localIp[24];
        char localVer[24];
        copySafe(localIp, sizeof(localIp), s_cache.ip);
        copySafe(localVer, sizeof(localVer), s_cache.version);
        uint32_t s2 = s_cache.seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            if (!valid || port != expectedPort || localIp[0] == '\0') return false;
            if ((millis() - ts) > maxAgeMs) return false;
            copySafe(outIp, ipCap, localIp);
            if (outVer && verCap > 0) copySafe(outVer, verCap, localVer);
            return true;
        }
    }
    return false;
}

void writeCache(const char* ip, uint16_t port, const char* version) {
    s_cache.seq.fetch_add(1, std::memory_order_acq_rel);
    copySafe(s_cache.ip, sizeof(s_cache.ip), ip);
    copySafe(s_cache.version, sizeof(s_cache.version), (version && version[0] != '\0') ? version : "1.1.4");
    s_cache.port = port;
    s_cache.resolvedAtMs = millis();
    s_cache.valid = (ip != nullptr && ip[0] != '\0');
    s_cache.seq.fetch_add(1, std::memory_order_release);
    if (ip != nullptr && ip[0] != '\0') {
        ShowcaseDiscovery::recordCandidateIpStr(ip);
    }
}

#if defined(ESP32) && !defined(UNIT_TEST)
std::atomic<bool> s_bgScanBusy{false};
std::atomic<bool> s_scanMutexFlag{false};
std::atomic<bool> s_mdnsBusy{false};
std::atomic<uint32_t> s_lastSubnetScanMs{0};
char s_bgProbeUrl[96] = "";
bool s_bgProbeForce = false;

void snapshotCandidates(char outIps[6][20], uint8_t& outCount) {
    outCount = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t s1 = s_candidates.seq.load(std::memory_order_acquire);
        if ((s1 & 1U) != 0U) continue;
        uint8_t cnt = s_candidates.count;
        if (cnt > 6) cnt = 6;
        char tmp[6][20];
        for (uint8_t idx = 0; idx < cnt; ++idx) {
            copySafe(tmp[idx], sizeof(tmp[idx]), s_candidates.ips[idx]);
        }
        uint32_t s2 = s_candidates.seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            for (uint8_t idx = 0; idx < cnt; ++idx) {
                copySafe(outIps[idx], sizeof(outIps[idx]), tmp[idx]);
            }
            outCount = cnt;
            return;
        }
    }
}

void extractJsonVersionField(const char* body, char* outVer, size_t verCap) {
    if (!outVer || verCap == 0) return;
    outVer[0] = '\0';
    if (!body) return;
    const char* key = strstr(body, "\"version\"");
    if (!key) return;
    const char* colon = strchr(key + 9, ':');
    if (!colon) return;
    const char* q1 = strchr(colon + 1, '"');
    if (!q1) return;
    const char* q2 = strchr(q1 + 1, '"');
    if (!q2 || q2 <= q1 + 1) return;
    size_t len = static_cast<size_t>(q2 - (q1 + 1));
    if (len >= verCap) len = verCap - 1;
    memcpy(outVer, q1 + 1, len);
    outVer[len] = '\0';
}

bool isUsableIpv4(const IPAddress& addr) {
    uint32_t raw = static_cast<uint32_t>(addr);
    return raw != 0U && raw != 0xFFFFFFFFU;
}

bool isUnreachableDockerBridgeIp(const IPAddress& cand) {
    if (cand[0] != 172 || cand[1] < 16 || cand[1] > 31) {
        return false;
    }
    IPAddress selfIp = WiFi.localIP();
    return !(selfIp[0] == 172 && selfIp[1] >= 16 && selfIp[1] <= 31);
}

bool verifyShowcaseHealthHttp(
    const char* host,
    uint16_t port,
    uint32_t connectTimeoutMs,
    char* outVer,
    size_t verCap
) {
    if (!host || host[0] == '\0' || port == 0) return false;
    if (ESP.getFreeHeap() < 6144U) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_aton(host, &addr.sin_addr) == 0) {
        if (ShowcaseDiscovery::isAutoDiscoverableHost(host) || ESP.getFreeHeap() < 10240U) {
            return false;
        }
        IPAddress resolved;
        if (!WiFi.hostByName(host, resolved) || !isUsableIpv4(resolved) || isUnreachableDockerBridgeIp(resolved)) {
            return false;
        }
        addr.sin_addr.s_addr = static_cast<uint32_t>(resolved);
    }

    int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return false;
    }

    auto closeAbort = [&]() {
        struct linger lin = {1, 0};
        lwip_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        lwip_close(fd);
    };

    int flags = lwip_fcntl(fd, F_GETFL, 0);
    lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = lwip_connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        closeAbort();
        return false;
    }
    if (rc != 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv;
        uint32_t boundedMs = connectTimeoutMs > 0 ? connectTimeoutMs : 350U;
        tv.tv_sec = static_cast<time_t>(boundedMs / 1000U);
        tv.tv_usec = static_cast<suseconds_t>((boundedMs % 1000U) * 1000U);
        int sel = lwip_select(fd + 1, nullptr, &wfds, nullptr, &tv);
        if (sel <= 0 || !FD_ISSET(fd, &wfds)) {
            closeAbort();
            return false;
        }
        int soErr = -1;
        socklen_t errLen = sizeof(soErr);
        if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &errLen) != 0 || soErr != 0) {
            closeAbort();
            return false;
        }
    }

    lwip_fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval rtv = {0, 500000};
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &rtv, sizeof(rtv));

    char reqBuf[128];
    int reqLen = snprintf(
        reqBuf,
        sizeof(reqBuf),
        "GET /api/v1/health HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
        host,
        static_cast<unsigned>(port)
    );
    if (reqLen <= 0 || lwip_send(fd, reqBuf, static_cast<size_t>(reqLen), 0) <= 0) {
        closeAbort();
        return false;
    }

    char rxBuf[384];
    size_t total = 0;
    uint32_t t0 = millis();
    while ((millis() - t0) < 600U && total < sizeof(rxBuf) - 1U) {
        int r = lwip_recv(fd, rxBuf + total, sizeof(rxBuf) - 1U - total, 0);
        if (r > 0) {
            total += static_cast<size_t>(r);
            rxBuf[total] = '\0';
            if (strstr(rxBuf, "dmd-media-showcase") != nullptr && strstr(rxBuf, "\"version\"") != nullptr) {
                break;
            }
        } else {
            break;
        }
    }
    closeAbort();
    rxBuf[total] = '\0';

    bool isShowcase = (strstr(rxBuf, "dmd-media-showcase") != nullptr) ||
                      (strstr(rxBuf, "\"mqtt_port\"") != nullptr && strstr(rxBuf, "\"ok\"") != nullptr);
    if (!isShowcase) {
        return false;
    }
    if (outVer && verCap > 0) {
        extractJsonVersionField(rxBuf, outVer, verCap);
        if (outVer[0] == '\0') {
            copySafe(outVer, verCap, "1.1.4");
        }
    }
    return true;
}

bool tryMdnsLookup(const char* rawHost, uint16_t port, char* outIp, size_t ipCap, char* outVer, size_t verCap) {
    if (WiFi.status() != WL_CONNECTED) return false;
    bool expectedBusy = false;
    if (!s_mdnsBusy.compare_exchange_strong(expectedBusy, true, std::memory_order_acq_rel)) {
        return false;
    }
    struct MdnsGuard {
        ~MdnsGuard() { s_mdnsBusy.store(false, std::memory_order_release); }
    } mdnsGuard;

    char mdnsBase[64] = "dmdmediashowcase";
    if (rawHost && rawHost[0] != '\0') {
        size_t len = strlen(rawHost);
        if (len > 6 && strcasecmp(rawHost + len - 6, ".local") == 0) {
            size_t bLen = len - 6;
            if (bLen >= sizeof(mdnsBase)) bLen = sizeof(mdnsBase) - 1;
            memcpy(mdnsBase, rawHost, bLen);
            mdnsBase[bLen] = '\0';
        } else if (strcasecmp(rawHost, "auto") != 0) {
            copySafe(mdnsBase, sizeof(mdnsBase), rawHost);
        }
    }

    IPAddress ip = MDNS.queryHost(mdnsBase, 350);
    if (isUsableIpv4(ip) && !isUnreachableDockerBridgeIp(ip)) {
        char cand[20];
        snprintf(cand, sizeof(cand), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        if (verifyShowcaseHealthHttp(cand, port, 400, outVer, verCap)) {
            copySafe(outIp, ipCap, cand);
            writeCache(cand, port, outVer);
            ShowcaseDiscovery::recordCandidateIpStr(cand);
            return true;
        }
    }

    int n = MDNS.queryService("dmd-showcase", "tcp");
    for (int i = 0; i < n; ++i) {
        IPAddress sip = MDNS.IP(i);
        if (isUsableIpv4(sip) && !isUnreachableDockerBridgeIp(sip)) {
            char cand[20];
            snprintf(cand, sizeof(cand), "%u.%u.%u.%u", sip[0], sip[1], sip[2], sip[3]);
            uint16_t sPort = MDNS.port(i) > 0 ? MDNS.port(i) : port;
            if (sPort == port && verifyShowcaseHealthHttp(cand, port, 400, outVer, verCap)) {
                copySafe(outIp, ipCap, cand);
                writeCache(cand, port, outVer);
                ShowcaseDiscovery::recordCandidateIpStr(cand);
                return true;
            }
        }
    }
    return false;
}

bool tryCandidatePeers(uint16_t port, char* outIp, size_t ipCap, char* outVer, size_t verCap) {
    if (WiFi.status() != WL_CONNECTED) return false;

    // Seed configured MQTT broker IP if valid IPv4
    {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const auto& snap = guard.get();
        if (snap.mqtt.broker.length() > 0 && isValidCandidateIpv4(snap.mqtt.broker.c_str())) {
            ShowcaseDiscovery::recordCandidateIpStr(snap.mqtt.broker.c_str());
        }
    }

    char cands[6][20];
    uint8_t count = 0;
    snapshotCandidates(cands, count);
    for (uint8_t i = 0; i < count; ++i) {
        if (cands[i][0] == '\0') continue;
        if (verifyShowcaseHealthHttp(cands[i], port, 280, outVer, verCap)) {
            copySafe(outIp, ipCap, cands[i]);
            writeCache(cands[i], port, outVer);
            LOGI("ShowcaseDisc", "Discovered DMD Media Showcase via peer candidate at %s:%u", cands[i], port);
            return true;
        }
    }
    return false;
}

bool scanLocalSubnet24(uint16_t port, char* outIp, size_t ipCap, char* outVer, size_t verCap) {
    if (WiFi.status() != WL_CONNECTED) return false;
    if (ESP.getFreeHeap() < kMinDiscoveryHeapFloorBytes) return false;

    // Rate-limit negative full /24 sweeps to at most once every 5 seconds (1:1 RPi parity)
    uint32_t lastScan = s_lastSubnetScanMs.load(std::memory_order_acquire);
    if (lastScan != 0 && (millis() - lastScan) < kNegativeScanBackoffMs) {
        return false;
    }

    bool expected = false;
    if (!s_scanMutexFlag.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    IPAddress selfIp = WiFi.localIP();
    IPAddress gwIp = WiFi.gatewayIP();
    if (!isUsableIpv4(selfIp)) {
        s_scanMutexFlag.store(false, std::memory_order_release);
        return false;
    }

    s_lastSubnetScanMs.store(millis() == 0 ? 1U : millis(), std::memory_order_release);

    // Build ordered probe list starting outward from ESP32's own last octet (typical DHCP neighborhood)
    uint8_t order[254];
    size_t orderLen = 0;
    int center = static_cast<int>(selfIp[3]);
    int gwOctet = (gwIp[0] == selfIp[0] && gwIp[1] == selfIp[1] && gwIp[2] == selfIp[2])
        ? static_cast<int>(gwIp[3])
        : -1;

    for (int delta = 1; delta <= 254 && orderLen < 254; ++delta) {
        int lo = center - delta;
        if (lo >= 2 && lo <= 254 && lo != gwOctet) {
            order[orderLen++] = static_cast<uint8_t>(lo);
        }
        int hi = center + delta;
        if (hi >= 2 && hi <= 254 && hi != gwOctet) {
            order[orderLen++] = static_cast<uint8_t>(hi);
        }
    }

    // Strictly bounded 3-socket non-blocking connect batches (`CONFIG_LWIP_MAX_SOCKETS = 10` safe)
    constexpr size_t BATCH_SOCKETS = 3;
    bool found = false;

    for (size_t base = 0; base < orderLen && !found; base += BATCH_SOCKETS) {
        if (WiFi.status() != WL_CONNECTED || ESP.getFreeHeap() < kMinDiscoveryHeapFloorBytes) break;

        int fds[BATCH_SOCKETS] = {-1, -1, -1};
        uint8_t octets[BATCH_SOCKETS] = {0, 0, 0};
        int maxFd = -1;
        fd_set wfds;
        FD_ZERO(&wfds);

        for (size_t b = 0; b < BATCH_SOCKETS && (base + b) < orderLen; ++b) {
            uint8_t oct = order[base + b];
            int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (fd < 0) continue;

            int flags = lwip_fcntl(fd, F_GETFL, 0);
            lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            uint32_t ipv4Net = (static_cast<uint32_t>(selfIp[0])) |
                               (static_cast<uint32_t>(selfIp[1]) << 8) |
                               (static_cast<uint32_t>(selfIp[2]) << 16) |
                               (static_cast<uint32_t>(oct) << 24);
            addr.sin_addr.s_addr = ipv4Net;

            int rc = lwip_connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
            if (rc == 0 || errno == EINPROGRESS) {
                fds[b] = fd;
                octets[b] = oct;
                FD_SET(fd, &wfds);
                if (fd > maxFd) maxFd = fd;
            } else {
                lwip_close(fd);
            }
        }

        uint8_t openOctets[BATCH_SOCKETS] = {0, 0, 0};
        size_t openCount = 0;

        if (maxFd >= 0) {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 32000; // 32 ms per 3-IP batch (~2.5s worst-case full /24, typically <200ms)
            int sel = lwip_select(maxFd + 1, nullptr, &wfds, nullptr, &tv);
            if (sel > 0) {
                for (size_t b = 0; b < BATCH_SOCKETS; ++b) {
                    if (fds[b] >= 0 && FD_ISSET(fds[b], &wfds)) {
                        int soErr = -1;
                        socklen_t errLen = sizeof(soErr);
                        if (lwip_getsockopt(fds[b], SOL_SOCKET, SO_ERROR, &soErr, &errLen) == 0 && soErr == 0) {
                            openOctets[openCount++] = octets[b];
                        }
                    }
                }
            }
        }

        // Always close all batch sockets BEFORE opening a verification HTTP socket
        for (size_t b = 0; b < BATCH_SOCKETS; ++b) {
            if (fds[b] >= 0) {
                lwip_close(fds[b]);
                fds[b] = -1;
            }
        }

        for (size_t k = 0; k < openCount; ++k) {
            char cand[20];
            snprintf(cand, sizeof(cand), "%u.%u.%u.%u", selfIp[0], selfIp[1], selfIp[2], openOctets[k]);
            if (verifyShowcaseHealthHttp(cand, port, 350, outVer, verCap)) {
                copySafe(outIp, ipCap, cand);
                writeCache(cand, port, outVer);
                ShowcaseDiscovery::recordCandidateIpStr(cand);
                LOGI("ShowcaseDisc", "Auto-discovered DMD Media Showcase on LAN at %s:%u", cand, port);
                found = true;
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }

    s_scanMutexFlag.store(false, std::memory_order_release);
    return found;
}

void bgScanTaskEntry(void* arg) {
    uint16_t port = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(arg));
    if (port == 0) port = 8890;
    char probeUrl[96] = "";
    copySafe(probeUrl, sizeof(probeUrl), s_bgProbeUrl);
    bool force = s_bgProbeForce;
    s_bgProbeUrl[0] = '\0';
    s_bgProbeForce = false;

    if (probeUrl[0] != '\0') {
        char resolvedUrl[112] = "";
        char resolvedHost[64] = "";
        char ver[24] = "";
        uint16_t resolvedPort = port;
        bool online = false;
        ShowcaseDiscovery::resolveUrl(
            probeUrl,
            resolvedUrl,
            sizeof(resolvedUrl),
            resolvedHost,
            sizeof(resolvedHost),
            resolvedPort,
            online,
            ver,
            sizeof(ver),
            true,
            force
        );
    } else {
        char ip[24] = "";
        char ver[24] = "";
        if (!tryCandidatePeers(port, ip, sizeof(ip), ver, sizeof(ver))) {
            if (!tryMdnsLookup("dmdmediashowcase.local", port, ip, sizeof(ip), ver, sizeof(ver))) {
                scanLocalSubnet24(port, ip, sizeof(ip), ver, sizeof(ver));
            }
        }
    }
    s_bgScanBusy.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}
#endif

} // namespace

void ShowcaseDiscovery::hardwareShortId(char* out, size_t cap) {
    if (!out || cap == 0) return;
#if defined(ESP32) && !defined(UNIT_TEST)
    static char s_cachedHwId[8] = "";
    if (s_cachedHwId[0] == '\0') {
        uint64_t mac = ESP.getEfuseMac();
        // Mix all 48 EFUSE MAC bits into a 24-bit non-zero identifier (6 lowercase hex chars)
        uint32_t mixed = (static_cast<uint32_t>(mac >> 24) ^ static_cast<uint32_t>(mac)) & 0xFFFFFFU;
        if (mixed == 0U) mixed = 0x000001U;
        snprintf(s_cachedHwId, sizeof(s_cachedHwId), "%06x", static_cast<unsigned>(mixed));
    }
    copySafe(out, cap, s_cachedHwId);
#else
    copySafe(out, cap, "ut0001");
#endif
}

void ShowcaseDiscovery::formatClientId(
    const char* domainSlug,
    uint16_t width,
    uint16_t height,
    bool syncSuffix,
    char* out,
    size_t cap
) {
    if (!out || cap == 0) return;
    char hwId[8] = "";
    hardwareShortId(hwId, sizeof(hwId));
    const char* dom = (domainSlug && domainSlug[0] != '\0') ? domainSlug : "games";
    uint16_t w = width > 0 ? width : 128;
    uint16_t h = height > 0 ? height : 32;
    if (syncSuffix) {
        snprintf(out, cap, "esp32_%s_%s_%ux%u_sync", hwId, dom, static_cast<unsigned>(w), static_cast<unsigned>(h));
    } else {
        snprintf(out, cap, "esp32_%s_%s_%ux%u", hwId, dom, static_cast<unsigned>(w), static_cast<unsigned>(h));
    }
}

void ShowcaseDiscovery::recordCandidateIpStr(const char* ipStr) {
    if (!isValidCandidateIpv4(ipStr)) return;
#if defined(ESP32) && !defined(UNIT_TEST)
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress selfIp = WiFi.localIP();
        char selfStr[20];
        snprintf(selfStr, sizeof(selfStr), "%u.%u.%u.%u", selfIp[0], selfIp[1], selfIp[2], selfIp[3]);
        if (strcmp(ipStr, selfStr) == 0) return;
    }
#endif
    s_candidates.seq.fetch_add(1, std::memory_order_acq_rel);
    // Move to front (MRU slot 0) so active WebUI browser / Docker host is always probed first
    int existingIdx = -1;
    for (uint8_t i = 0; i < s_candidates.count && i < 6; ++i) {
        if (strcmp(s_candidates.ips[i], ipStr) == 0) {
            existingIdx = static_cast<int>(i);
            break;
        }
    }
    if (existingIdx == 0) {
        s_candidates.seq.fetch_add(1, std::memory_order_release);
        return;
    }
    int shiftEnd = (existingIdx > 0)
        ? existingIdx
        : (s_candidates.count < 6 ? static_cast<int>(s_candidates.count++) : 5);
    for (int i = shiftEnd; i > 0; --i) {
        copySafe(s_candidates.ips[i], sizeof(s_candidates.ips[i]), s_candidates.ips[i - 1]);
    }
    copySafe(s_candidates.ips[0], sizeof(s_candidates.ips[0]), ipStr);
    s_candidates.seq.fetch_add(1, std::memory_order_release);
}

bool ShowcaseDiscovery::isAutoDiscoverableHost(const char* host) {
    if (!host || host[0] == '\0') return false;
    size_t len = strlen(host);
    if (len > 6 && strcasecmp(host + len - 6, ".local") == 0) {
        return true;
    }
    if (strcasecmp(host, "dmdmediashowcase") == 0 ||
        strcasecmp(host, "dmd-media-showcase") == 0 ||
        strcasecmp(host, "auto") == 0) {
        return true;
    }
    // Single-label LAN hostnames without dots (excluding localhost) require `.local` mDNS / LAN discovery
    if (strchr(host, '.') == nullptr && strchr(host, ':') == nullptr && strcasecmp(host, "localhost") != 0) {
        return true;
    }
    return false;
}

bool ShowcaseDiscovery::parseEndpoint(
    const char* rawUrl,
    uint16_t defaultPort,
    char* outScheme,
    size_t schemeCap,
    char* outHost,
    size_t hostCap,
    uint16_t& outPort
) {
    if (!rawUrl || !outHost || hostCap == 0) return false;
    while (*rawUrl == ' ' || *rawUrl == '\t') ++rawUrl;
    if (*rawUrl == '\0') return false;

    const char* p = rawUrl;
    const char* scheme = "http";
    if (strncasecmp(p, "https://", 8) == 0) {
        scheme = "https";
        p += 8;
    } else if (strncasecmp(p, "http://", 7) == 0) {
        scheme = "http";
        p += 7;
    } else if (strncasecmp(p, "mqtt://", 7) == 0) {
        scheme = "mqtt";
        p += 7;
    } else if (strncasecmp(p, "tcp://", 6) == 0) {
        scheme = "tcp";
        p += 6;
    }
    if (outScheme && schemeCap > 0) {
        copySafe(outScheme, schemeCap, scheme);
    }

    const char* slash = strchr(p, '/');
    size_t authLen = slash ? static_cast<size_t>(slash - p) : strlen(p);
    if (authLen == 0) return false;

    char auth[96];
    size_t copyLen = (authLen < sizeof(auth) - 1U) ? authLen : (sizeof(auth) - 1U);
    memcpy(auth, p, copyLen);
    auth[copyLen] = '\0';

    outPort = defaultPort;
    char* closeBracket = strrchr(auth, ']');
    char* colon = strrchr(auth, ':');
    if (colon && (!closeBracket || colon > closeBracket)) {
        *colon = '\0';
        int parsed = atoi(colon + 1);
        if (parsed > 0 && parsed <= 65535) {
            outPort = static_cast<uint16_t>(parsed);
        }
    }
    if (auth[0] == '\0') return false;
    // Normalize single-label LAN hostnames (e.g. `dmdmediashowcase` -> `dmdmediashowcase.local`)
    if (strchr(auth, '.') == nullptr &&
        strchr(auth, ':') == nullptr &&
        strcasecmp(auth, "localhost") != 0 &&
        strcasecmp(auth, "auto") != 0) {
        snprintf(outHost, hostCap, "%s.local", auth);
    } else {
        copySafe(outHost, hostCap, auth);
    }
    return true;
}

bool ShowcaseDiscovery::resolveHostInPlace(
    char* hostBuf,
    size_t hostCap,
    uint16_t showcaseHttpPort,
    bool allowSubnetScan
) {
    if (!hostBuf || hostCap == 0 || hostBuf[0] == '\0') return false;
    if (!isAutoDiscoverableHost(hostBuf)) {
        if (isValidCandidateIpv4(hostBuf)) {
            recordCandidateIpStr(hostBuf);
        }
        return true;
    }

    char cachedIp[24] = "";
    char ver[24] = "";
    uint16_t probePort = showcaseHttpPort > 0 ? showcaseHttpPort : 8890;
    if (readCache(probePort, kPositiveCacheTtlMs, cachedIp, sizeof(cachedIp), ver, sizeof(ver))) {
        copySafe(hostBuf, hostCap, cachedIp);
        return true;
    }

#if defined(ESP32) && !defined(UNIT_TEST)
    // Retain last-known-good IP if emergency heap floor is reached; otherwise ensure it is in `s_candidates`
    // and probe `s_candidates` FIRST (WebUI browser Docker/Rancher host + MQTT broker + last healthy IP).
    char prevIp[24] = "";
    if (readCache(probePort, 86400000U, prevIp, sizeof(prevIp), ver, sizeof(ver))) {
        if (ESP.getFreeHeap() < kMinDiscoveryHeapFloorBytes) {
            copySafe(hostBuf, hostCap, prevIp);
            return true;
        }
        recordCandidateIpStr(prevIp);
    }

    if (tryCandidatePeers(probePort, cachedIp, sizeof(cachedIp), ver, sizeof(ver)) ||
        tryMdnsLookup(hostBuf, probePort, cachedIp, sizeof(cachedIp), ver, sizeof(ver)) ||
        (allowSubnetScan && scanLocalSubnet24(probePort, cachedIp, sizeof(cachedIp), ver, sizeof(ver)))) {
        copySafe(hostBuf, hostCap, cachedIp);
        return true;
    }
#else
    (void)allowSubnetScan;
#endif
    return false;
}

bool ShowcaseDiscovery::resolveUrl(
    const char* rawUrl,
    char* outResolvedUrl,
    size_t urlCap,
    char* outHost,
    size_t hostCap,
    uint16_t& outPort,
    bool& outOnline,
    char* outVersion,
    size_t verCap,
    bool allowSubnetScan,
    bool forceRefresh
) {
    outOnline = false;
    if (outVersion && verCap > 0) outVersion[0] = '\0';
    if (outResolvedUrl && urlCap > 0) outResolvedUrl[0] = '\0';

    char scheme[12] = "http";
    if (!parseEndpoint(rawUrl, 8890, scheme, sizeof(scheme), outHost, hostCap, outPort)) {
        return false;
    }

    char verBuf[24] = "";
    if (!isAutoDiscoverableHost(outHost)) {
        if (isValidCandidateIpv4(outHost)) {
            recordCandidateIpStr(outHost);
        }
        if (outResolvedUrl && urlCap > 0) {
            snprintf(outResolvedUrl, urlCap, "%s://%s:%u", scheme, outHost, static_cast<unsigned>(outPort));
        }
#if defined(ESP32) && !defined(UNIT_TEST)
        if (!forceRefresh) {
            char cachedIp[24] = "";
            if (readCache(outPort, kPositiveCacheTtlMs, cachedIp, sizeof(cachedIp), verBuf, sizeof(verBuf)) &&
                strcmp(cachedIp, outHost) == 0) {
                outOnline = true;
                if (outVersion && verCap > 0) copySafe(outVersion, verCap, verBuf);
                return true;
            }
            if (outVersion == nullptr) {
                // Fast runtime URL formatting path for an explicit IP/host: skip opening a
                // redundant 450ms health-check TCP socket right before the caller's real request.
                outOnline = true;
                return true;
            }
        }
        if (verifyShowcaseHealthHttp(outHost, outPort, 450, verBuf, sizeof(verBuf))) {
            outOnline = true;
            writeCache(outHost, outPort, verBuf);
            if (outVersion && verCap > 0) copySafe(outVersion, verCap, verBuf);
        }
#else
        (void)forceRefresh;
        (void)allowSubnetScan;
#endif
        return true;
    }

    char discoveredIp[24] = "";
    if (!forceRefresh && readCache(outPort, kPositiveCacheTtlMs, discoveredIp, sizeof(discoveredIp), verBuf, sizeof(verBuf))) {
        copySafe(outHost, hostCap, discoveredIp);
        if (outResolvedUrl && urlCap > 0) {
            snprintf(outResolvedUrl, urlCap, "%s://%s:%u", scheme, outHost, static_cast<unsigned>(outPort));
        }
        outOnline = true;
        if (outVersion && verCap > 0) copySafe(outVersion, verCap, verBuf);
        return true;
    }

#if defined(ESP32) && !defined(UNIT_TEST)
    char prevIp[24] = "";
    if (readCache(outPort, 86400000U, prevIp, sizeof(prevIp), verBuf, sizeof(verBuf))) {
        if (!forceRefresh && ESP.getFreeHeap() < kMinDiscoveryHeapFloorBytes) {
            copySafe(outHost, hostCap, prevIp);
            if (outResolvedUrl && urlCap > 0) {
                snprintf(outResolvedUrl, urlCap, "%s://%s:%u", scheme, outHost, static_cast<unsigned>(outPort));
            }
            outOnline = true;
            if (outVersion && verCap > 0) copySafe(outVersion, verCap, verBuf);
            return true;
        }
        recordCandidateIpStr(prevIp);
    }
    if (forceRefresh) {
        s_lastSubnetScanMs.store(0, std::memory_order_release);
    }

    // Probe `s_candidates` FIRST (WebUI browser client IPs + MQTT broker + last healthy Showcase IP)
    if (tryCandidatePeers(outPort, discoveredIp, sizeof(discoveredIp), verBuf, sizeof(verBuf)) ||
        tryMdnsLookup(outHost, outPort, discoveredIp, sizeof(discoveredIp), verBuf, sizeof(verBuf)) ||
        (allowSubnetScan && scanLocalSubnet24(outPort, discoveredIp, sizeof(discoveredIp), verBuf, sizeof(verBuf)))) {
        copySafe(outHost, hostCap, discoveredIp);
        outOnline = true;
        if (outVersion && verCap > 0) copySafe(outVersion, verCap, verBuf);
    }
#endif

    if (outResolvedUrl && urlCap > 0) {
        snprintf(outResolvedUrl, urlCap, "%s://%s:%u", scheme, outHost, static_cast<unsigned>(outPort));
    }
    return true;
}

void ShowcaseDiscovery::touchVerifiedHost(const char* ipStr, uint16_t showcaseHttpPort) {
    if (!isValidCandidateIpv4(ipStr)) return;
    uint16_t port = showcaseHttpPort > 0 ? showcaseHttpPort : 8890;
    recordCandidateIpStr(ipStr);

    char existingIp[24] = "";
    char existingVer[24] = "";
    const char* verToKeep = "1.1.4";
    if (readCache(port, 86400000U, existingIp, sizeof(existingIp), existingVer, sizeof(existingVer)) &&
        strcmp(existingIp, ipStr) == 0 && existingVer[0] != '\0') {
        verToKeep = existingVer;
    }
    writeCache(ipStr, port, verToKeep);
}

bool ShowcaseDiscovery::peekCachedUrlStatus(
    const char* rawUrl,
    char* outResolvedUrl,
    size_t urlCap,
    char* outHost,
    size_t hostCap,
    uint16_t& outPort,
    bool& outOnline,
    char* outVersion,
    size_t verCap,
    uint32_t maxAgeMs
) {
    outOnline = false;
    if (outVersion && verCap > 0) outVersion[0] = '\0';
    if (outResolvedUrl && urlCap > 0) outResolvedUrl[0] = '\0';

    char scheme[12] = "http";
    if (!parseEndpoint(rawUrl, 8890, scheme, sizeof(scheme), outHost, hostCap, outPort)) {
        return false;
    }

    char cachedIp[24] = "";
    char verBuf[24] = "";
    if (!isAutoDiscoverableHost(outHost)) {
        if (isValidCandidateIpv4(outHost)) {
            recordCandidateIpStr(outHost);
        }
        if (outResolvedUrl && urlCap > 0) {
            snprintf(outResolvedUrl, urlCap, "%s://%s:%u", scheme, outHost, static_cast<unsigned>(outPort));
        }
        if (readCache(outPort, maxAgeMs, cachedIp, sizeof(cachedIp), verBuf, sizeof(verBuf)) &&
            strcmp(cachedIp, outHost) == 0) {
            outOnline = true;
            if (outVersion && verCap > 0) copySafe(outVersion, verCap, verBuf);
        }
        return true;
    }

    if (readCache(outPort, maxAgeMs, cachedIp, sizeof(cachedIp), verBuf, sizeof(verBuf))) {
        copySafe(outHost, hostCap, cachedIp);
        outOnline = true;
        if (outVersion && verCap > 0) copySafe(outVersion, verCap, verBuf);
    }
    if (outResolvedUrl && urlCap > 0) {
        snprintf(outResolvedUrl, urlCap, "%s://%s:%u", scheme, outHost, static_cast<unsigned>(outPort));
    }
    return true;
}

bool ShowcaseDiscovery::triggerBackgroundProbe(const char* rawUrl, bool forceRefresh) {
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!rawUrl || rawUrl[0] == '\0' || WiFi.status() != WL_CONNECTED) return false;
    char scheme[12] = "http";
    char host[64] = "";
    uint16_t port = 8890;
    if (!parseEndpoint(rawUrl, 8890, scheme, sizeof(scheme), host, sizeof(host), port)) {
        return false;
    }

    bool hasPsram = hardwareHAL.capabilities().hasPsram;
    if (!hasPsram) {
        auto act = NetworkBudget::getDmdNetActivity();
        if (NetworkBudget::isBulkTransferActive() || act == NetworkBudget::DmdNetActivity::WebUiSync) {
            return false;
        }
        if (!forceRefresh && act != NetworkBudget::DmdNetActivity::Idle) {
            return false;
        }
        if (ESP.getFreeHeap() < 10240U || ESP.getMaxAllocHeap() < 4608U) {
            return false;
        }
    } else if (ESP.getFreeHeap() < 16384U) {
        return false;
    }

    if (s_bgScanBusy.load(std::memory_order_acquire)) {
        return true;
    }
    bool expected = false;
    if (!s_bgScanBusy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }
    copySafe(s_bgProbeUrl, sizeof(s_bgProbeUrl), rawUrl);
    s_bgProbeForce = forceRefresh;
    uint32_t stackBytes = hasPsram ? 4096U : 3584U;
    BaseType_t ok = xTaskCreatePinnedToCore(
        bgScanTaskEntry,
        "ShowcaseDisc",
        stackBytes,
        reinterpret_cast<void*>(static_cast<uintptr_t>(port > 0 ? port : 8890)),
        1,
        nullptr,
        0 // Core 0 only
    );
    if (ok != pdPASS) {
        s_bgProbeUrl[0] = '\0';
        s_bgProbeForce = false;
        s_bgScanBusy.store(false, std::memory_order_release);
        return false;
    }
    return true;
#else
    (void)rawUrl;
    (void)forceRefresh;
    return false;
#endif
}

void ShowcaseDiscovery::resetCacheForTests() {
    s_cache.seq.fetch_add(1, std::memory_order_acq_rel);
    s_cache.ip[0] = '\0';
    s_cache.version[0] = '\0';
    s_cache.port = 0;
    s_cache.resolvedAtMs = 0;
    s_cache.valid = false;
    s_cache.seq.fetch_add(1, std::memory_order_release);

    s_candidates.seq.fetch_add(1, std::memory_order_acq_rel);
    for (uint8_t i = 0; i < 6; ++i) {
        s_candidates.ips[i][0] = '\0';
    }
    s_candidates.count = 0;
    s_candidates.seq.fetch_add(1, std::memory_order_release);
}

void ShowcaseDiscovery::triggerBackgroundScan(uint16_t showcaseHttpPort) {
    char defaultUrl[64];
    snprintf(defaultUrl, sizeof(defaultUrl), "http://dmdmediashowcase.local:%u", static_cast<unsigned>(showcaseHttpPort > 0 ? showcaseHttpPort : 8890));
    (void)triggerBackgroundProbe(defaultUrl, false);
}

} // namespace net
