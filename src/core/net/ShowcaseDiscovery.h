#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

namespace net {

/**
 * @class ShowcaseDiscovery
 * @brief Docker-aware LAN auto-discovery and unique hardware identity helper for DMD Media Showcase.
 *
 * Why this exists:
 * - When `dmd-media-showcase` runs inside Docker Desktop / Rancher Desktop / OrbStack on macOS or Windows,
 *   the hypervisor NAT isolates container UDP `:5353` multicast from the physical LAN, so `MDNS.queryHost("dmdmediashowcase")`
 *   returns `0.0.0.0` even though TCP `:8890` and `:1883` are forwarded on the host LAN IP.
 * - Multiple ArcadeMatrix ESP32 boards of identical geometry (e.g. two `128x32` panels) must never share
 *   the same MQTT `clientId` or `dmd_showcase/clients/<id>/...` topic namespace.
 *
 * Concurrency & Memory Guarantees:
 * - Zero Core 1 execution: all network probes and optional `/24` scans run exclusively on Core 0 workers.
 * - `AsyncWebServer` (`allowSubnetScan == false`) performs instant cache / candidate lookup and schedules
 *   an asynchronous Core 0 scan task on cache miss without blocking `async_tcp`.
 * - Bounded 3-socket non-blocking `select()` batches preserve LwIP socket headroom (`CONFIG_LWIP_MAX_SOCKETS = 10`).
 */
class ShowcaseDiscovery {
public:
    /**
     * @brief Writes a deterministic 6-hex-character hardware identifier derived from the 48-bit EFUSE MAC.
     */
    static void hardwareShortId(char* out, size_t cap);

    /**
     * @brief Formats a globally unique per-board Showcase client/topic ID:
     *        `esp32_<hwid>_<domain>_<W>x<H>` (or `esp32_<hwid>_<domain>_<W>x<H>_sync` if `syncSuffix` is true).
     */
    static void formatClientId(
        const char* domainSlug,
        uint16_t width,
        uint16_t height,
        bool syncSuffix,
        char* out,
        size_t cap
    );

    /**
     * @brief Records a non-loopback LAN peer IPv4 address (e.g. WebUI browser host or configured MQTT broker)
     *        as a high-priority candidate for Showcase discovery before falling back to a `/24` sweep.
     */
    static void recordCandidateIpStr(const char* ipStr);

    /**
     * @brief Returns true if `host` requires auto-discovery (`.local`, `dmdmediashowcase`, or `auto`).
     */
    static bool isAutoDiscoverableHost(const char* host);

    /**
     * @brief Parses `[scheme://]host[:port][/...]` into scheme, host, and port.
     */
    static bool parseEndpoint(
        const char* rawUrl,
        uint16_t defaultPort,
        char* outScheme,
        size_t schemeCap,
        char* outHost,
        size_t hostCap,
        uint16_t& outPort
    );

    /**
     * @brief Resolves a Showcase server URL (supporting `.local` mDNS + candidate peer probing + bounded `/24` LAN scan)
     *        and optionally verifies `GET /api/v1/health` (`service == "dmd-media-showcase"`).
     *
     * @param rawUrl Configured Showcase URL (e.g. `http://dmdmediashowcase.local:8890` or `http://192.168.1.22:8890`).
     * @param outResolvedUrl Receives `<scheme>://<resolved_host>:<port>`.
     * @param urlCap Capacity of `outResolvedUrl`.
     * @param outHost Receives `<resolved_host>`.
     * @param hostCap Capacity of `outHost`.
     * @param outPort Receives resolved TCP port.
     * @param outOnline Receives true if Showcase responded healthy on `<resolved_host>:<port>`.
     * @param outVersion Optional buffer receiving Showcase `version` string from `/api/v1/health`.
     * @param verCap Capacity of `outVersion`.
     * @param allowSubnetScan True on Core 0 engine workers; false on `AsyncWebServer` handlers.
     * @param forceRefresh Bypass positive cache entry and re-verify health immediately.
     * @return true if URL syntax is valid (`outHost` / `outResolvedUrl` populated).
     */
    static bool resolveUrl(
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
        bool forceRefresh = false
    );

    /**
     * @brief Resolves a standalone hostname in-place (e.g. `dmdmediashowcase.local` -> `192.168.1.22`)
     *        using the shared discovery cache, mDNS, candidate IPs, and optional `/24` scan.
     */
    static bool resolveHostInPlace(
        char* hostBuf,
        size_t hostCap,
        uint16_t showcaseHttpPort = 8890,
        bool allowSubnetScan = true
    );

    /**
     * @brief Spawns a single-flight, short-lived Core 0 task (`ShowcaseDisc`) to sweep the local `/24`
     *        for `dmd-media-showcase` on `showcaseHttpPort` without blocking the HTTP server thread.
     */
    static void triggerBackgroundScan(uint16_t showcaseHttpPort = 8890);

    /**
     * @brief Renews the positive discovery cache lease (`resolvedAtMs = millis()`) whenever an active
     *        Showcase MQTT or TCP stream session succeeds against `ipStr`, preventing cache TTL expiry
     *        during continuous playback without extra `/api/v1/health` sockets.
     */
    static void touchVerifiedHost(const char* ipStr, uint16_t showcaseHttpPort = 8890);

    /**
     * @brief 100% non-blocking (`0 ms`, `0 sockets`, `0 mDNS`) cache inspection for `AsyncWebServer`
     *        handlers (`async_tcp`). Populates `outResolvedUrl` / `outHost` / `outPort` and sets
     *        `outOnline = true` if a fresh verified cache entry exists within `maxAgeMs`.
     */
    static bool peekCachedUrlStatus(
        const char* rawUrl,
        char* outResolvedUrl,
        size_t urlCap,
        char* outHost,
        size_t hostCap,
        uint16_t& outPort,
        bool& outOnline,
        char* outVersion,
        size_t verCap,
        uint32_t maxAgeMs = 180000U
    );

    /**
     * @brief Schedules a single-flight Core 0 health/discovery probe for `rawUrl` without blocking
     *        `async_tcp`. Respects single-activity network budget on non-PSRAM `esp32dev`.
     * @return true if a background probe task is running or was just scheduled.
     */
    static bool triggerBackgroundProbe(const char* rawUrl, bool forceRefresh = false);

    /**
     * @brief Clears cached discovery and candidate peer state (intended for deterministic unit tests).
     */
    static void resetCacheForTests();
};

} // namespace net

