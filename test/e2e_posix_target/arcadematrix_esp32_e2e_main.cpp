/**
 * @file arcadematrix_esp32_e2e_main.cpp
 * @brief POSIX Linux/macOS E2E target for ArcadeMatrix (ESP32) FrontendSync + In-Game Showcase.
 *
 * Compiles and links the genuine ArcadeMatrix Core 1 & Core 0 C++ components without modifying `src/`:
 * - `DisplayArbiter` (`ROTATION` -> `MQTT` -> `INGAME_SHOWCASE` preemption & deterministic resume)
 * - `CanvasBufferedSurface` + `MockPresentationBackend` (`128x32` RGB565 SRAM surface)
 * - `ShowcaseStreamPlayer` + `DmdzStreamDecoder` + `DmdzSpscByteRing` (Zero-Copy `ScopedCanvasGuard`
 *   in-place `DMDZ` v2 `16-bit` RGB565 `RLE` / `LZ` / `Scroll2D` decoding)
 * - `ShowcaseDiscovery::formatClientId` (`esp32_<hwid>_games_128x32_sync` companion sync slot)
 * - `BuiltinSystemMaps.h` + `FrontendSyncEngine` system/folder normalization rules
 */

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <netdb.h>
#include <set>
#include <sstream>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "Arduino.h"
#include "core/DisplayArbiter.h"
#include "core/Logger.h"
#include "core/drawing/CanvasBufferedSurface.h"
#include "core/drawing/DmdzStreamDecoder.h"
#include "core/drawing/MockPresentationBackend.h"
#include "core/net/DmdzStreamClient.h"
#include "core/net/ShowcaseDiscovery.h"
#include "engines/BuiltinSystemMaps.h"
#include "engines/showcase/ShowcaseStreamPlayer.h"

HardwareSerial Serial;
LogLevel Logger::currentLevel = LOG_LEVEL_INFO;

namespace {

constexpr uint16_t kPanelW = 128;
constexpr uint16_t kPanelH = 32;
constexpr size_t kFramePixels = static_cast<size_t>(kPanelW) * static_cast<size_t>(kPanelH);
constexpr size_t kFrameBytes = kFramePixels * sizeof(uint16_t);

std::atomic<bool> g_running{true};

uint32_t crc32Bytes(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1U)));
        }
    }
    return ~crc;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c >= 0x20) out.push_back(static_cast<char>(c));
                break;
        }
    }
    return out;
}

std::string extractJsonString(const std::string& json, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    ++pos;
    std::string val;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            val.push_back(json[pos++]);
        } else if (c == '"') {
            break;
        } else {
            val.push_back(c);
        }
    }
    return val;
}

int extractJsonInt(const std::string& json, const char* key, int defVal) {
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return defVal;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return defVal;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '"')) ++pos;
    if (pos >= json.size()) return defVal;
    return std::atoi(json.c_str() + pos);
}

bool extractJsonBool(const std::string& json, const char* key, bool defVal) {
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return defVal;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return defVal;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return defVal;
}

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string trimAscii(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// Mirrors FrontendSyncEngine::cleanSystemName
std::string cleanSystemNameStd(const std::string& rawSystem) {
    std::string s = trimAscii(rawSystem);
    std::string sNorm = toLowerAscii(s);
    for (char& c : sNorm) {
        if (c == '_' || c == '-') c = ' ';
    }
    const char* prefixes[] = {
        "arcade manufacturer ",
        "arcade system ",
        "arcade genre ",
        "arcade collection ",
        "manufacturer ",
        "system ",
        "genre ",
        "collection ",
    };
    for (const char* p : prefixes) {
        size_t plen = std::strlen(p);
        if (sNorm.compare(0, plen, p) == 0 && plen <= s.size()) {
            std::string rem = s.substr(plen);
            if (!rem.empty() && (rem[0] == '_' || rem[0] == '-')) rem.erase(0, 1);
            return trimAscii(rem);
        }
    }
    return s;
}

// Mirrors FrontendSyncEngine::mapSystemToPixelcadeFolder
std::string mapSystemToPixelcadeFolderStd(const std::string& systemId) {
    std::string sLower = toLowerAscii(cleanSystemNameStd(systemId));
    struct Entry { const char* id; const char* folder; };
    static const Entry table[] = {
        {"arcade", "mame"}, {"mame", "mame"}, {"fba", "mame"}, {"fbneo", "mame"},
        {"cps1", "mame"}, {"cps2", "mame"}, {"cps3", "mame"},
        {"neogeo", "neogeo"}, {"neogeocd", "neogeocd"},
        {"nes", "nes"}, {"famicom", "nes"},
        {"snes", "snes"}, {"supernintendo", "snes"},
        {"n64", "n64"}, {"nintendo64", "n64"},
        {"gb", "gb"}, {"gameboy", "gb"},
        {"gba", "gba"}, {"gameboyadvance", "gba"},
        {"gbc", "gbc"}, {"gameboycolor", "gbc"},
        {"megadrive", "genesis"}, {"genesis", "genesis"},
        {"mastersystem", "mastersystem"}, {"gamegear", "gamegear"},
        {"psx", "psx"}, {"ps1", "psx"}, {"playstation", "psx"},
        {"dreamcast", "dreamcast"}, {"saturn", "saturn"},
        {"pcengine", "pcengine"}, {"tg16", "pcengine"},
        {"atari2600", "atari2600"}, {"atari5200", "atari5200"}, {"atari7800", "atari7800"},
    };
    for (const auto& e : table) {
        if (sLower == e.id) return e.folder;
    }
    return cleanSystemNameStd(systemId);
}

// Decodes 24-bit/32-bit uncompressed BMP into 128x32 RGB565
bool loadBmpTo565(const std::string& path, uint16_t out565[kFramePixels]) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') return false;
    uint32_t pixelOffset = data[10] | (uint32_t(data[11]) << 8) | (uint32_t(data[12]) << 16) | (uint32_t(data[13]) << 24);
    int32_t w = int32_t(data[18] | (uint32_t(data[19]) << 8) | (uint32_t(data[20]) << 16) | (uint32_t(data[21]) << 24));
    int32_t hSigned = int32_t(data[22] | (uint32_t(data[23]) << 8) | (uint32_t(data[24]) << 16) | (uint32_t(data[25]) << 24));
    uint16_t bpp = data[28] | (uint16_t(data[29]) << 8);
    if (w <= 0 || hSigned == 0 || (bpp != 24 && bpp != 32)) return false;
    bool topDown = (hSigned < 0);
    int32_t h = topDown ? -hSigned : hSigned;
    size_t rowStride = ((size_t(w) * (bpp / 8) + 3U) & ~3U);
    if (pixelOffset + rowStride * size_t(h) > data.size()) return false;

    std::memset(out565, 0, kFrameBytes);
    for (int y = 0; y < int(kPanelH); ++y) {
        int srcY = (y * h) / int(kPanelH);
        int bmpRow = topDown ? srcY : (h - 1 - srcY);
        const uint8_t* row = data.data() + pixelOffset + size_t(bmpRow) * rowStride;
        for (int x = 0; x < int(kPanelW); ++x) {
            int srcX = (x * w) / int(kPanelW);
            const uint8_t* px = row + size_t(srcX) * (bpp / 8);
            uint8_t b = px[0], g = px[1], r = px[2];
            out565[y * kPanelW + x] = IDrawingSurface::color565(r, g, b);
        }
    }
    return true;
}

// Decodes first frame of GIF87a/GIF89a (LZW) into 128x32 RGB565
bool loadGifFirstFrameTo565(const std::string& path, uint16_t out565[kFramePixels]) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 13 || std::memcmp(d.data(), "GIF8", 4) != 0) return false;

    uint16_t screenW = d[6] | (uint16_t(d[7]) << 8);
    uint16_t screenH = d[8] | (uint16_t(d[9]) << 8);
    uint8_t packed = d[10];
    size_t pos = 13;
    uint8_t gct[256][3] = {{0}};
    if (packed & 0x80U) {
        size_t gctEntries = 1ULL << ((packed & 0x07U) + 1U);
        if (pos + gctEntries * 3U > d.size()) return false;
        for (size_t i = 0; i < gctEntries; ++i) {
            gct[i][0] = d[pos++];
            gct[i][1] = d[pos++];
            gct[i][2] = d[pos++];
        }
    }

    int transparentIdx = -1;
    while (pos < d.size()) {
        uint8_t sep = d[pos++];
        if (sep == 0x3BU) break;
        if (sep == 0x21U) {
            if (pos >= d.size()) break;
            uint8_t label = d[pos++];
            if (label == 0xF9U && pos + 5 <= d.size() && d[pos] == 4) {
                uint8_t gceFlags = d[pos + 1];
                if (gceFlags & 0x01U) transparentIdx = d[pos + 4];
            }
            while (pos < d.size()) {
                uint8_t subLen = d[pos++];
                if (subLen == 0) break;
                pos += subLen;
            }
            continue;
        }
        if (sep != 0x2CU) continue;
        if (pos + 9 > d.size()) return false;
        uint16_t imgX = d[pos] | (uint16_t(d[pos + 1]) << 8);
        uint16_t imgY = d[pos + 2] | (uint16_t(d[pos + 3]) << 8);
        uint16_t imgW = d[pos + 4] | (uint16_t(d[pos + 5]) << 8);
        uint16_t imgH = d[pos + 6] | (uint16_t(d[pos + 7]) << 8);
        uint8_t imgPacked = d[pos + 8];
        pos += 9;

        uint8_t pal[256][3];
        std::memcpy(pal, gct, sizeof(pal));
        if (imgPacked & 0x80U) {
            size_t lctEntries = 1ULL << ((imgPacked & 0x07U) + 1U);
            if (pos + lctEntries * 3U > d.size()) return false;
            for (size_t i = 0; i < lctEntries; ++i) {
                pal[i][0] = d[pos++];
                pal[i][1] = d[pos++];
                pal[i][2] = d[pos++];
            }
        }
        if (pos >= d.size()) return false;
        uint8_t lzwMinCode = d[pos++];
        if (lzwMinCode < 2 || lzwMinCode > 8) return false;

        std::vector<uint8_t> subData;
        while (pos < d.size()) {
            uint8_t subLen = d[pos++];
            if (subLen == 0) break;
            if (pos + subLen > d.size()) return false;
            subData.insert(subData.end(), d.begin() + pos, d.begin() + pos + subLen);
            pos += subLen;
        }

        const int clearCode = 1 << lzwMinCode;
        const int endCode = clearCode + 1;
        int codeSize = lzwMinCode + 1;
        int nextCode = endCode + 1;
        std::vector<int> prefix(4096, -1);
        std::vector<uint8_t> suffix(4096, 0);
        for (int i = 0; i < clearCode; ++i) suffix[i] = static_cast<uint8_t>(i);

        std::vector<uint8_t> indices;
        indices.reserve(size_t(imgW) * size_t(imgH));
        size_t bitPos = 0;
        auto readCode = [&](int bits) -> int {
            if (bitPos + size_t(bits) > subData.size() * 8U) return -1;
            int val = 0;
            for (int b = 0; b < bits; ++b) {
                size_t byteIdx = (bitPos + b) >> 3;
                size_t bitIdx = (bitPos + b) & 7U;
                if (subData[byteIdx] & (1U << bitIdx)) val |= (1 << b);
            }
            bitPos += bits;
            return val;
        };

        int oldCode = -1;
        uint8_t firstChar = 0;
        std::vector<uint8_t> stack;
        while (indices.size() < size_t(imgW) * size_t(imgH)) {
            int code = readCode(codeSize);
            if (code < 0 || code == endCode) break;
            if (code == clearCode) {
                codeSize = lzwMinCode + 1;
                nextCode = endCode + 1;
                oldCode = -1;
                continue;
            }
            int inCode = code;
            stack.clear();
            if (code >= nextCode) {
                if (oldCode < 0) break;
                stack.push_back(firstChar);
                code = oldCode;
            }
            while (code >= clearCode && code < 4096) {
                stack.push_back(suffix[code]);
                code = prefix[code];
            }
            if (code < 0 || code >= clearCode) break;
            firstChar = suffix[code];
            stack.push_back(firstChar);
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                if (indices.size() < size_t(imgW) * size_t(imgH)) indices.push_back(*it);
            }
            if (oldCode >= 0 && nextCode < 4096) {
                prefix[nextCode] = oldCode;
                suffix[nextCode] = firstChar;
                ++nextCode;
                if (nextCode == (1 << codeSize) && codeSize < 12) ++codeSize;
            }
            oldCode = inCode;
        }

        if (indices.empty() || screenW == 0 || screenH == 0) return false;
        std::vector<uint16_t> canvas(size_t(screenW) * size_t(screenH), 0);
        for (uint16_t y = 0; y < imgH; ++y) {
            for (uint16_t x = 0; x < imgW; ++x) {
                size_t idxPos = size_t(y) * size_t(imgW) + size_t(x);
                if (idxPos >= indices.size()) break;
                uint8_t ci = indices[idxPos];
                if (int(ci) == transparentIdx) continue;
                uint16_t dx = imgX + x;
                uint16_t dy = imgY + y;
                if (dx < screenW && dy < screenH) {
                    canvas[size_t(dy) * size_t(screenW) + size_t(dx)] =
                        IDrawingSurface::color565(pal[ci][0], pal[ci][1], pal[ci][2]);
                }
            }
        }
        for (uint16_t y = 0; y < kPanelH; ++y) {
            uint16_t sy = (uint32_t(y) * screenH) / kPanelH;
            for (uint16_t x = 0; x < kPanelW; ++x) {
                uint16_t sx = (uint32_t(x) * screenW) / kPanelW;
                out565[y * kPanelW + x] = canvas[size_t(sy) * size_t(screenW) + size_t(sx)];
            }
        }
        return true;
    }
    return false;
}

bool loadMarqueeFileTo565(const std::string& path, uint16_t out565[kFramePixels]) {
    std::string lower = toLowerAscii(path);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bmp") {
        return loadBmpTo565(path, out565);
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".gif") {
        return loadGifFirstFrameTo565(path, out565);
    }
    if (lower.size() >= 7 && lower.substr(lower.size() - 7) == ".rgb565") {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        f.read(reinterpret_cast<char*>(out565), kFrameBytes);
        return f.gcount() == static_cast<std::streamsize>(kFrameBytes);
    }
    return false;
}

bool fileExists(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Minimal HTTP/1.1 client helper for POSIX target
struct SimpleHttpResponse {
    int status = 0;
    std::vector<uint8_t> body;
};

int connectTcpHostPort(const std::string& host, uint16_t port, int timeoutMs = 2500) {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%u", unsigned(port));
    if (::getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
        return -1;
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        return -1;
    }
    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        ::close(fd);
        ::freeaddrinfo(res);
        return -1;
    }
    ::freeaddrinfo(res);
    return fd;
}

SimpleHttpResponse httpRequest(
    const std::string& host,
    uint16_t port,
    const std::string& method,
    const std::string& path,
    const std::string& jsonBody = "",
    int timeoutMs = 4000
) {
    SimpleHttpResponse out;
    int fd = connectTcpHostPort(host, port, timeoutMs);
    if (fd < 0) return out;

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Connection: close\r\n";
    if (!jsonBody.empty()) {
        req << "Content-Type: application/json\r\n"
            << "Content-Length: " << jsonBody.size() << "\r\n";
    }
    req << "\r\n" << jsonBody;
    std::string wire = req.str();
    size_t sent = 0;
    while (sent < wire.size()) {
        ssize_t n = ::send(fd, wire.data() + sent, wire.size() - sent, 0);
        if (n <= 0) {
            ::close(fd);
            return out;
        }
        sent += size_t(n);
    }

    std::vector<uint8_t> raw;
    uint8_t buf[4096];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.insert(raw.end(), buf, buf + n);
    }
    ::close(fd);

    const std::string rawStr(reinterpret_cast<const char*>(raw.data()), raw.size());
    size_t hdrEnd = rawStr.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return out;
    size_t sp = rawStr.find(' ');
    if (sp != std::string::npos && sp + 3 <= hdrEnd) {
        out.status = std::atoi(rawStr.c_str() + sp + 1);
    }
    std::string headersLower = toLowerAscii(rawStr.substr(0, hdrEnd));
    size_t bodyStart = hdrEnd + 4;
    if (headersLower.find("transfer-encoding: chunked") != std::string::npos) {
        size_t p = bodyStart;
        while (p < raw.size()) {
            size_t lineEnd = rawStr.find("\r\n", p);
            if (lineEnd == std::string::npos) break;
            size_t chunkLen = std::strtoul(rawStr.substr(p, lineEnd - p).c_str(), nullptr, 16);
            p = lineEnd + 2;
            if (chunkLen == 0 || p + chunkLen > raw.size()) break;
            out.body.insert(out.body.end(), raw.begin() + p, raw.begin() + p + chunkLen);
            p += chunkLen + 2;
        }
    } else {
        out.body.assign(raw.begin() + bodyStart, raw.end());
    }
    return out;
}

// ============================================================================
// Runtime State for ArcadeMatrix ESP32 POSIX Target
// ============================================================================

struct AmRuntimeState {
    std::string sdRoot = "/sd";
    std::string showcaseHost = "dmd-showcase";
    uint16_t showcasePort = 8890;
    std::string localBrokerHost = "127.0.0.1";
    uint16_t localBrokerPort = 1883;
    uint16_t httpPort = 80;

    std::atomic<bool> mqttEnabled{true};
    std::atomic<bool> showcaseEnabled{true};
    std::atomic<bool> useShowcaseBroker{true};
    std::atomic<bool> ingameShowcaseEnabled{true};

    char syncClientId[64] = "";

    DisplayArbiter arbiter;
    MockPresentationBackend backend{kPanelW, kPanelH, 8, false};
    CanvasBufferedSurface surface{kPanelW, kPanelH, CanvasStorage::SRAM, &backend, true};
    engines::showcase::ShowcaseStreamPlayer streamPlayer;

    std::mutex stateMu;
    std::string resolvedBrokerHost = "";
    uint16_t resolvedBrokerPort = 1883;
    bool brokerFromShowcase = false;

    std::string activeSource = "ROTATION";
    uint8_t activePriority = 10;
    bool isGamePlaying = false;
    bool wantsInGameDisplay = false;
    std::string lastMqttTopic = "";
    std::string lastSystem = "";
    std::string lastGame = "";
    std::string lastTitle = "";
    std::string activeMarqueePath = "";
    bool sdSyncCacheEverStaged = false;

    uint16_t marqueeFrame565[kFramePixels] = {0};
    bool hasMarqueeFrame = false;

    std::mutex fbMu;
    uint16_t publishedFb565[kFramePixels] = {0};
    uint32_t frameCrc32 = 0;
    uint32_t nonzeroPixels = 0;
    std::set<uint32_t> distinctStreamCrcs;

    std::atomic<uint32_t> mqttEventsReceived{0};
    std::atomic<uint32_t> showcaseEnqueues{0};
    std::atomic<uint32_t> showcaseStreamsCompleted{0};
    std::atomic<uint32_t> showcaseQueueDeletes{0};
    std::atomic<uint32_t> playSessionToken{0};
    std::atomic<bool> mqttReadyHint{false};

    struct PendingJob {
        uint32_t token = 0;
        bool active = false;
        bool cancelRemote = false;
        std::string system;
        std::string game;
        std::string title;
    };
    std::mutex jobMu;
    PendingJob pendingJob;
};

AmRuntimeState g_am;

const char* sourceIdName(DisplaySourceId id) {
    switch (id) {
        case DisplaySourceId::ROTATION: return "ROTATION";
        case DisplaySourceId::GIF: return "GIF";
        case DisplaySourceId::MARQUEE: return "MARQUEE";
        case DisplaySourceId::MQTT: return "MQTT";
        case DisplaySourceId::INGAME_SHOWCASE: return "INGAME_SHOWCASE";
        case DisplaySourceId::VISUALIZER: return "VISUALIZER";
        case DisplaySourceId::ALERT: return "ALERT";
        case DisplaySourceId::WEBUI_SYNC: return "WEBUI_SYNC";
    }
    return "ROTATION";
}

std::string resolveLocalMarqueeOnSd(const std::string& systemId, const std::string& gameName, bool isSystemOnly) {
    const std::string cleanSys = cleanSystemNameStd(systemId);
    const std::string folder = mapSystemToPixelcadeFolderStd(cleanSys);
    const char* exts[] = {".gif", ".bmp", ".rgb565"};

    std::vector<std::string> baseCandidates;
    if (isSystemOnly) {
        baseCandidates.push_back(g_am.sdRoot + "/pixelcade/console/default-" + toLowerAscii(cleanSys));
        baseCandidates.push_back(g_am.sdRoot + "/pixelcade/console/" + toLowerAscii(cleanSys));
        baseCandidates.push_back(g_am.sdRoot + "/console/default-" + toLowerAscii(cleanSys));
        baseCandidates.push_back(g_am.sdRoot + "/gifs/console/default-" + toLowerAscii(cleanSys));
    } else {
        std::string gLower = toLowerAscii(gameName);
        baseCandidates.push_back(g_am.sdRoot + "/pixelcade/" + folder + "/" + gameName);
        baseCandidates.push_back(g_am.sdRoot + "/pixelcade/" + folder + "/" + gLower);
        baseCandidates.push_back(g_am.sdRoot + "/" + folder + "/" + gameName);
        baseCandidates.push_back(g_am.sdRoot + "/" + folder + "/" + gLower);
        baseCandidates.push_back(g_am.sdRoot + "/gifs/" + folder + "/" + gameName);
        baseCandidates.push_back(g_am.sdRoot + "/gifs/" + folder + "/" + gLower);
        baseCandidates.push_back(g_am.sdRoot + "/pixelcade/console/default-" + toLowerAscii(cleanSys));
        baseCandidates.push_back(g_am.sdRoot + "/gifs/console/default-" + toLowerAscii(cleanSys));
    }

    for (const auto& base : baseCandidates) {
        for (const char* ext : exts) {
            if (fileExists(base + ext)) {
                return base + ext;
            }
        }
    }
    return "";
}

void renderFallbackBannerTo565(const std::string& label, uint16_t out565[kFramePixels]) {
    MockPresentationBackend tmpBackend(kPanelW, kPanelH, 8, false);
    CanvasBufferedSurface tmpSurf(kPanelW, kPanelH, CanvasStorage::SRAM, &tmpBackend, true);
    tmpSurf.clear(0x0000);
    // Deterministic cyan banner bar + per-character pixel blocks when SD marquee is absent
    tmpSurf.fillRect(0, 0, kPanelW, 2, 0x07FFU);
    tmpSurf.fillRect(0, kPanelH - 2, kPanelW, 2, 0x07FFU);
    int x = 4;
    for (size_t i = 0; i < label.size() && x + 5 < int(kPanelW) - 4; ++i, x += 6) {
        if (label[i] != ' ') {
            uint16_t col = static_cast<uint16_t>(0x07E0U ^ (uint16_t(uint8_t(label[i])) * 37U));
            tmpSurf.fillRect(x, 11, 4, 10, col == 0 ? 0x07FFU : col);
        }
    }
    std::memcpy(out565, tmpSurf.getRawCanvasBuffer(), kFrameBytes);
}

void handleFrontendMqttMessage(const std::string& topic, const std::string& payload) {
    if (topic.find("dmd_showcase/clients/") == 0) {
        std::string action = extractJsonString(payload, "action");
        if (action == "play_now" || action == "play_card") {
            g_am.mqttReadyHint.store(true, std::memory_order_release);
        }
        return;
    }

    if (topic.find("system/playing/") != 0) {
        return;
    }

    g_am.mqttEventsReceived.fetch_add(1, std::memory_order_relaxed);
    std::string status = extractJsonString(payload, "status");
    std::string rawSystem = extractJsonString(payload, "system");
    std::string game = extractJsonString(payload, "game");
    std::string title = extractJsonString(payload, "title");
    std::string evType = extractJsonString(payload, "type");
    std::string cleanSys = cleanSystemNameStd(rawSystem);

    if (status == "stopped") {
        uint32_t tok = g_am.playSessionToken.fetch_add(1, std::memory_order_acq_rel) + 1U;
        {
            std::lock_guard<std::mutex> lk(g_am.jobMu);
            g_am.pendingJob = {tok, false, true, "", "", ""};
        }
        {
            std::lock_guard<std::mutex> lk(g_am.stateMu);
            g_am.isGamePlaying = false;
            g_am.wantsInGameDisplay = false;
            g_am.lastMqttTopic = topic;
            g_am.activeMarqueePath.clear();
            g_am.hasMarqueeFrame = false;
        }
        g_am.arbiter.cancelRequest(DisplaySourceId::INGAME_SHOWCASE);
        g_am.arbiter.cancelRequest(DisplaySourceId::MQTT);
        return;
    }

    const bool isSystemOnly = (evType == "system" || game.empty());
    uint16_t decoded565[kFramePixels] = {0};
    std::string resolvedPath = resolveLocalMarqueeOnSd(cleanSys, game, isSystemOnly);
    if (!resolvedPath.empty() && loadMarqueeFileTo565(resolvedPath, decoded565)) {
        // Loaded local Pixelcade marquee from SD
    } else {
        resolvedPath = "";
        renderFallbackBannerTo565(game.empty() ? cleanSys : (title.empty() ? game : title), decoded565);
    }

    uint32_t tok = g_am.playSessionToken.fetch_add(1, std::memory_order_acq_rel) + 1U;
    const bool playing = (status == "playing" && !isSystemOnly);
    {
        std::lock_guard<std::mutex> lk(g_am.stateMu);
        g_am.lastMqttTopic = topic;
        g_am.lastSystem = cleanSys;
        g_am.lastGame = game;
        g_am.lastTitle = title;
        g_am.isGamePlaying = playing;
        g_am.wantsInGameDisplay = false;
        g_am.activeMarqueePath = resolvedPath;
        std::memcpy(g_am.marqueeFrame565, decoded565, kFrameBytes);
        g_am.hasMarqueeFrame = true;
    }

    // Cancel any previous In-Game Showcase preemption and submit MQTT FrontendSync marquee
    g_am.arbiter.cancelRequest(DisplaySourceId::INGAME_SHOWCASE);
    DisplayRequest mqttReq(
        DisplaySourceId::MQTT,
        DisplayPriority::MQTT,
        RequestLifecycle::UNTIL_CANCELLED,
        true,
        tok,
        EngineHandle("frontend_sync", "mqtt_marquee"),
        0,
        millis(),
        false,
        false,
        false
    );
    g_am.arbiter.submitRequest(mqttReq, true);

    {
        std::lock_guard<std::mutex> lk(g_am.jobMu);
        if (playing && g_am.ingameShowcaseEnabled.load(std::memory_order_relaxed)) {
            g_am.pendingJob = {tok, true, false, cleanSys, game, title};
        } else {
            g_am.pendingJob = {tok, false, true, "", "", ""};
        }
    }
}

// ============================================================================
// Core 1 Render Loop (60 Hz — DisplayArbiter + CanvasBufferedSurface + ShowcaseStreamPlayer)
// ============================================================================

void core1RenderLoop() {
    // Persistent baseline rotation engine on DisplayArbiter
    DisplayRequest baseReq(
        DisplaySourceId::ROTATION,
        DisplayPriority::ROTATION,
        RequestLifecycle::PERSISTENT,
        false,
        1,
        EngineHandle("clock", "clock_base"),
        0,
        millis(),
        true,
        true,
        false
    );
    g_am.arbiter.submitRequest(baseReq, false);
    (void)g_am.streamPlayer.probeZeroCopyOnCore1(&g_am.surface, kPanelW, kPanelH);

    while (g_running.load(std::memory_order_relaxed)) {
        const uint32_t nowMs = millis();
        DisplayDecision dec = g_am.arbiter.evaluate();

        if (dec.sourceId == DisplaySourceId::INGAME_SHOWCASE && g_am.streamPlayer.isCore1Streaming()) {
            if (g_am.streamPlayer.shouldMarkCore1Dirty(nowMs)) {
                (void)g_am.streamPlayer.renderFrameCore1(&g_am.surface, kPanelW, kPanelH);
            }
        } else if (dec.sourceId == DisplaySourceId::MQTT) {
            uint16_t localMarquee[kFramePixels];
            bool hasM = false;
            {
                std::lock_guard<std::mutex> lk(g_am.stateMu);
                if (g_am.hasMarqueeFrame) {
                    std::memcpy(localMarquee, g_am.marqueeFrame565, kFrameBytes);
                    hasM = true;
                }
            }
            if (hasM) {
                g_am.surface.blit565(localMarquee, 0, 0, kPanelW, kPanelH);
            }
        } else {
            // Baseline idle screen: deterministic dark-blue clock/idle bar
            g_am.surface.clear(0x0000);
            g_am.surface.fillRect(2, 2, 24, 6, IDrawingSurface::color565(0, 40, 120));
        }

        if (g_am.surface.isDirty()) {
            (void)g_am.surface.present();
        }

        const uint16_t* rawCanvas = g_am.surface.getRawCanvasBuffer();
        if (rawCanvas) {
            uint32_t nz = 0;
            for (size_t i = 0; i < kFramePixels; ++i) {
                if (rawCanvas[i] != 0) ++nz;
            }
            uint32_t crc = crc32Bytes(reinterpret_cast<const uint8_t*>(rawCanvas), kFrameBytes);
            {
                std::lock_guard<std::mutex> lk(g_am.fbMu);
                std::memcpy(g_am.publishedFb565, rawCanvas, kFrameBytes);
                g_am.nonzeroPixels = nz;
                g_am.frameCrc32 = crc;
                if (dec.sourceId == DisplaySourceId::INGAME_SHOWCASE && nz > 0) {
                    g_am.distinctStreamCrcs.insert(crc);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_am.stateMu);
            g_am.activeSource = sourceIdName(dec.sourceId);
            g_am.activePriority = static_cast<uint8_t>(dec.priority);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

// ============================================================================
// Core 0 Showcase Worker (Broker Discovery + On-Demand Queue + SD DMDZ v2 Staging + SPSC Feed)
// ============================================================================

void refreshBrokerResolution() {
    if (g_am.showcaseEnabled.load() && g_am.useShowcaseBroker.load()) {
        auto h = httpRequest(g_am.showcaseHost, g_am.showcasePort, "GET", "/api/v1/health", "", 1500);
        if (h.status == 200) {
            std::string body(reinterpret_cast<const char*>(h.body.data()), h.body.size());
            std::string mHost = extractJsonString(body, "mqtt_host");
            int mPort = extractJsonInt(body, "mqtt_port", 1883);
            if (mHost.empty() || mHost == "127.0.0.1" || mHost == "0.0.0.0" || mHost == "localhost") {
                mHost = g_am.showcaseHost;
            }
            std::lock_guard<std::mutex> lk(g_am.stateMu);
            g_am.resolvedBrokerHost = mHost;
            g_am.resolvedBrokerPort = static_cast<uint16_t>(mPort > 0 ? mPort : 1883);
            g_am.brokerFromShowcase = true;
            return;
        }
    }
    std::lock_guard<std::mutex> lk(g_am.stateMu);
    g_am.resolvedBrokerHost = g_am.localBrokerHost;
    g_am.resolvedBrokerPort = g_am.localBrokerPort;
    g_am.brokerFromShowcase = false;
}

void deleteRemoteShowcaseQueue() {
    std::string path = std::string("/api/v1/clients/") + g_am.syncClientId + "/queue";
    auto r = httpRequest(g_am.showcaseHost, g_am.showcasePort, "DELETE", path, "", 2500);
    if (r.status >= 200 && r.status < 300) {
        g_am.showcaseQueueDeletes.fetch_add(1, std::memory_order_relaxed);
    }
}

void removeStagedSyncCache() {
    std::string cachePath = g_am.sdRoot + "/cache/showcase_sync.dmdz";
    ::unlink(cachePath.c_str());
}

void core0ShowcaseWorkerLoop() {
    uint32_t lastToken = 0;
    uint32_t lastBrokerProbeMs = 0;
    bool remoteQueueDirty = false;

    while (g_running.load(std::memory_order_relaxed)) {
        const uint32_t nowMs = millis();
        if (lastBrokerProbeMs == 0 || (nowMs - lastBrokerProbeMs) >= 5000U) {
            refreshBrokerResolution();
            lastBrokerProbeMs = nowMs == 0 ? 1U : nowMs;
        }

        AmRuntimeState::PendingJob job;
        {
            std::lock_guard<std::mutex> lk(g_am.jobMu);
            job = g_am.pendingJob;
        }

        if (job.token == lastToken) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }
        lastToken = job.token;

        if (remoteQueueDirty || job.cancelRemote) {
            deleteRemoteShowcaseQueue();
            removeStagedSyncCache();
            remoteQueueDirty = false;
        }

        if (!job.active || !g_am.showcaseEnabled.load() || !g_am.ingameShowcaseEnabled.load()) {
            continue;
        }

        // Build exact ArcadeMatrix FrontendSyncEngine queue/async JSON request
        std::ostringstream body;
        body << "{"
             << "\"domain\":\"games\","
             << "\"systems\":[\"" << jsonEscape(job.system) << "\"],"
             << "\"query\":\"" << jsonEscape(job.game) << "\",";
        if (!job.title.empty() && toLowerAscii(job.title) != toLowerAscii(job.game)) {
            body << "\"fallback_query\":\"" << jsonEscape(job.title) << "\",";
        }
        body << "\"mode\":\"play_now\","
             << "\"mark_seen\":false,"
             << "\"async_prepare\":true,"
             << "\"render_card_in_stream\":true,"
             << "\"include_card_intro\":true,"
             << "\"card_duration_s\":3,"
             << "\"jacket_panorama\":true,"
             << "\"marquee_panorama\":true,"
             << "\"include_video\":true,"
             << "\"video_max_duration_s\":4,"
             << "\"video_fps\":15,"
             << "\"width\":" << kPanelW << ","
             << "\"height\":" << kPanelH << ","
             << "\"lang\":\"en\""
             << "}";

        remoteQueueDirty = true;
        g_am.mqttReadyHint.store(false, std::memory_order_release);
        std::string postPath = std::string("/api/v1/clients/") + g_am.syncClientId + "/queue/async";
        auto qResp = httpRequest(g_am.showcaseHost, g_am.showcasePort, "POST", postPath, body.str(), 4000);
        if (qResp.status < 200 || qResp.status >= 300) {
            remoteQueueDirty = false;
            continue;
        }
        g_am.showcaseEnqueues.fetch_add(1, std::memory_order_relaxed);

        std::string qJson(reinterpret_cast<const char*>(qResp.body.data()), qResp.body.size());
        std::string queueId = extractJsonString(qJson, "queue_id");
        std::string qStatus = extractJsonString(qJson, "status");
        std::string readyItemId = (qStatus == "ready" || qStatus == "playing")
            ? extractJsonString(qJson, "item_id")
            : "";

        const uint32_t waitStart = millis();
        std::string pollPath = std::string("/api/v1/clients/") + g_am.syncClientId + "/queue";
        while (readyItemId.empty() &&
               g_running.load() &&
               g_am.playSessionToken.load(std::memory_order_acquire) == job.token &&
               (millis() - waitStart) < 45000U) {
            for (int i = 0; i < 6; ++i) {
                if (g_am.playSessionToken.load(std::memory_order_acquire) != job.token ||
                    g_am.mqttReadyHint.exchange(false, std::memory_order_acq_rel)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            if (g_am.playSessionToken.load(std::memory_order_acquire) != job.token) break;
            auto pResp = httpRequest(g_am.showcaseHost, g_am.showcasePort, "GET", pollPath, "", 2500);
            if (pResp.status == 200) {
                std::string pJson(reinterpret_cast<const char*>(pResp.body.data()), pResp.body.size());
                if (pJson.find("\"ready\"") != std::string::npos || pJson.find("\"playing\"") != std::string::npos) {
                    readyItemId = extractJsonString(pJson, "item_id");
                } else if (pJson.find("\"failed\"") != std::string::npos) {
                    break;
                }
            }
        }

        if (readyItemId.empty() || g_am.playSessionToken.load(std::memory_order_acquire) != job.token) {
            if (g_am.playSessionToken.load(std::memory_order_acquire) != job.token && remoteQueueDirty) {
                deleteRemoteShowcaseQueue();
                remoteQueueDirty = false;
            }
            continue;
        }

        // Download DMDZ v2 stream and stage on virtual SD (`/sd/cache/showcase_sync.dmdz`)
        std::string streamPath = "/api/v1/games/stream/" + readyItemId + ".rgb565.stream?purge=false&codec=dmdz2";
        auto sResp = httpRequest(g_am.showcaseHost, g_am.showcasePort, "GET", streamPath, "", 15000);
        if (sResp.status != 200 || sResp.body.size() <= 16 ||
            g_am.playSessionToken.load(std::memory_order_acquire) != job.token) {
            continue;
        }

        core::drawing::DmdzStreamHeader hdr{};
        if (!core::drawing::DmdzStreamDecoder::parseHeader(sResp.body.data(), hdr) ||
            hdr.width != kPanelW || hdr.height != kPanelH) {
            continue;
        }

        std::string cacheDir = g_am.sdRoot + "/cache";
        ::mkdir(g_am.sdRoot.c_str(), 0755);
        ::mkdir(cacheDir.c_str(), 0755);
        std::string cacheFile = cacheDir + "/showcase_sync.dmdz";
        {
            std::ofstream cf(cacheFile, std::ios::binary | std::ios::trunc);
            cf.write(reinterpret_cast<const char*>(sResp.body.data()), static_cast<std::streamsize>(sResp.body.size()));
        }
        {
            std::lock_guard<std::mutex> lk(g_am.stateMu);
            g_am.sdSyncCacheEverStaged = true;
            g_am.distinctStreamCrcs.clear();
        }

        // Feed the genuine `ShowcaseStreamPlayer` (`8,448 B` SPSC ring -> Core 1 `DmdzStreamDecoder`)
        g_am.streamPlayer.resetCore1StreamState();
        if (g_am.streamPlayer.ensureDecoderAllocated(false) &&
            g_am.streamPlayer.allocateSpscRing(
                engines::showcase::ShowcaseStreamPlayer::SRAM_SPSC_DEFAULT_RING_BYTES, false)) {
            const uint8_t* payloadPtr = sResp.body.data() + 16;
            const size_t payloadLen = sResp.body.size() - 16;
            size_t offset = 0;

            // Pre-buffer initial chunks BEFORE preempting the marquee (Late Preemption invariant)
            while (offset < payloadLen) {
                size_t pushed = g_am.streamPlayer.spscRing().push(payloadPtr + offset, payloadLen - offset);
                offset += pushed;
                if (pushed == 0) break;
            }
            const uint32_t fps = hdr.fps > 0 ? hdr.fps : 15U;
            g_am.streamPlayer.armSpscStreamForTest(hdr.width, hdr.height, hdr.version, hdr.isDmdz, hdr.totalFrames, fps);

            {
                std::lock_guard<std::mutex> lk(g_am.stateMu);
                g_am.wantsInGameDisplay = true;
            }
            DisplayRequest igReq(
                DisplaySourceId::INGAME_SHOWCASE,
                DisplayPriority::INGAME_SHOWCASE,
                RequestLifecycle::UNTIL_CANCELLED,
                true,
                job.token,
                EngineHandle("ingame_showcase", "sync_stream"),
                0,
                millis(),
                false,
                false,
                true
            );
            g_am.arbiter.submitRequest(igReq, true);

            while (g_running.load() &&
                   g_am.playSessionToken.load(std::memory_order_acquire) == job.token &&
                   !g_am.streamPlayer.isCore1StreamFinished()) {
                if (offset < payloadLen) {
                    size_t pushed = g_am.streamPlayer.spscRing().push(payloadPtr + offset, payloadLen - offset);
                    offset += pushed;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            const bool completedNormally = g_am.streamPlayer.isCore1StreamFinished() &&
                                           (g_am.playSessionToken.load(std::memory_order_acquire) == job.token);

            g_am.arbiter.cancelRequest(DisplaySourceId::INGAME_SHOWCASE);
            {
                std::lock_guard<std::mutex> lk(g_am.stateMu);
                g_am.wantsInGameDisplay = false;
            }
            g_am.streamPlayer.releaseAll();
            removeStagedSyncCache();

            // Purge consumed stream file on Showcase
            (void)httpRequest(
                g_am.showcaseHost,
                g_am.showcasePort,
                "DELETE",
                "/api/v1/games/stream/" + readyItemId,
                "",
                2500
            );

            if (completedNormally) {
                g_am.showcaseStreamsCompleted.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

// ============================================================================
// MQTT 3.1.1 Subscriber Loop (Subscribes to `system/playing/#` & Showcase command)
// ============================================================================

void encodeRemainingLen(std::vector<uint8_t>& pkt, size_t len) {
    do {
        uint8_t b = static_cast<uint8_t>(len % 128U);
        len /= 128U;
        if (len > 0) b |= 0x80U;
        pkt.push_back(b);
    } while (len > 0);
}

bool sendAllBytes(int fd, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}

bool sendMqttConnectAndSubscribe(int fd, const std::string& clientId, const std::vector<std::string>& topics) {
    std::vector<uint8_t> body = {0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02, 0x00, 0x3C};
    body.push_back(static_cast<uint8_t>((clientId.size() >> 8) & 0xFFU));
    body.push_back(static_cast<uint8_t>(clientId.size() & 0xFFU));
    body.insert(body.end(), clientId.begin(), clientId.end());

    std::vector<uint8_t> connPkt = {0x10};
    encodeRemainingLen(connPkt, body.size());
    connPkt.insert(connPkt.end(), body.begin(), body.end());
    if (!sendAllBytes(fd, connPkt.data(), connPkt.size())) return false;

    uint8_t connAck[4] = {0};
    if (::recv(fd, connAck, 4, 0) < 4 || (connAck[0] & 0xF0U) != 0x20U || connAck[3] != 0x00U) {
        return false;
    }

    uint16_t pktId = 1;
    for (const auto& t : topics) {
        std::vector<uint8_t> sBody;
        sBody.push_back(static_cast<uint8_t>((pktId >> 8) & 0xFFU));
        sBody.push_back(static_cast<uint8_t>(pktId & 0xFFU));
        ++pktId;
        sBody.push_back(static_cast<uint8_t>((t.size() >> 8) & 0xFFU));
        sBody.push_back(static_cast<uint8_t>(t.size() & 0xFFU));
        sBody.insert(sBody.end(), t.begin(), t.end());
        sBody.push_back(0x00);

        std::vector<uint8_t> sPkt = {0x82};
        encodeRemainingLen(sPkt, sBody.size());
        sPkt.insert(sPkt.end(), sBody.begin(), sBody.end());
        if (!sendAllBytes(fd, sPkt.data(), sPkt.size())) return false;
    }
    return true;
}

void mqttSubscriberLoop() {
    std::string activeHost;
    uint16_t activePort = 0;
    int fd = -1;
    std::vector<uint8_t> rxBuf;
    uint32_t lastPingMs = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        std::string wantHost;
        uint16_t wantPort = 1883;
        {
            std::lock_guard<std::mutex> lk(g_am.stateMu);
            wantHost = g_am.resolvedBrokerHost;
            wantPort = g_am.resolvedBrokerPort;
        }
        if (wantHost.empty()) {
            refreshBrokerResolution();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (fd >= 0 && (wantHost != activeHost || wantPort != activePort)) {
            ::close(fd);
            fd = -1;
        }

        if (fd < 0) {
            fd = connectTcpHostPort(wantHost, wantPort, 2000);
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }
            std::string cid = std::string(g_am.syncClientId) + "_sub";
            std::string cmdTopic = std::string("dmd_showcase/clients/") + g_am.syncClientId + "/command";
            if (!sendMqttConnectAndSubscribe(fd, cid, {"system/playing/#", cmdTopic})) {
                ::close(fd);
                fd = -1;
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }
            activeHost = wantHost;
            activePort = wantPort;
            rxBuf.clear();
            lastPingMs = millis();

            struct timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }

        uint8_t tmp[2048];
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            rxBuf.insert(rxBuf.end(), tmp, tmp + n);
            while (rxBuf.size() >= 2) {
                uint8_t hdr = rxBuf[0];
                size_t mult = 1;
                size_t remLen = 0;
                size_t idx = 1;
                bool completeLen = false;
                while (idx < rxBuf.size() && idx <= 4) {
                    uint8_t enc = rxBuf[idx++];
                    remLen += size_t(enc & 0x7FU) * mult;
                    mult *= 128U;
                    if ((enc & 0x80U) == 0) {
                        completeLen = true;
                        break;
                    }
                }
                if (!completeLen || rxBuf.size() < idx + remLen) break;

                if ((hdr & 0xF0U) == 0x30U && remLen >= 2) {
                    uint16_t tLen = (uint16_t(rxBuf[idx]) << 8) | rxBuf[idx + 1];
                    size_t pPos = idx + 2 + tLen;
                    if (((hdr >> 1) & 0x03U) > 0) pPos += 2;
                    if (idx + 2 + tLen <= idx + remLen && pPos <= idx + remLen) {
                        std::string topic(reinterpret_cast<const char*>(rxBuf.data() + idx + 2), tLen);
                        std::string payload(reinterpret_cast<const char*>(rxBuf.data() + pPos), (idx + remLen) - pPos);
                        handleFrontendMqttMessage(topic, payload);
                    }
                }
                rxBuf.erase(rxBuf.begin(), rxBuf.begin() + idx + remLen);
            }
        } else if (n == 0) {
            ::close(fd);
            fd = -1;
        }

        if (fd >= 0 && (millis() - lastPingMs) >= 15000U) {
            const uint8_t pingReq[2] = {0xC0, 0x00};
            if (!sendAllBytes(fd, pingReq, 2)) {
                ::close(fd);
                fd = -1;
            }
            lastPingMs = millis();
        }
    }
    if (fd >= 0) ::close(fd);
}

// ============================================================================
// Embedded Fallback MQTT 3.1.1 Broker (mirrors PicoMQTT::Server on port 1883)
// ============================================================================

void runEmbeddedBrokerLoop(uint16_t port) {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return;
    int opt = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(srv, 16) != 0) {
        ::close(srv);
        return;
    }

    struct timeval tv{0, 250000};
    ::setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (g_running.load(std::memory_order_relaxed)) {
        int cfd = ::accept(srv, nullptr, nullptr);
        if (cfd < 0) continue;
        std::thread([cfd]() {
            struct timeval ctv{2, 0};
            ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &ctv, sizeof(ctv));
            std::vector<uint8_t> rx;
            uint8_t buf[2048];
            while (g_running.load(std::memory_order_relaxed)) {
                ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
                if (n <= 0) break;
                rx.insert(rx.end(), buf, buf + n);
                while (rx.size() >= 2) {
                    uint8_t hdr = rx[0];
                    size_t mult = 1, rem = 0, i = 1;
                    bool ok = false;
                    while (i < rx.size() && i <= 4) {
                        uint8_t e = rx[i++];
                        rem += size_t(e & 0x7FU) * mult;
                        mult *= 128U;
                        if ((e & 0x80U) == 0) { ok = true; break; }
                    }
                    if (!ok || rx.size() < i + rem) break;
                    uint8_t ptype = hdr & 0xF0U;
                    if (ptype == 0x10U) {
                        const uint8_t ack[4] = {0x20, 0x02, 0x00, 0x00};
                        sendAllBytes(cfd, ack, 4);
                    } else if (ptype == 0x80U && rem >= 2) {
                        const uint8_t subAck[5] = {0x90, 0x03, rx[i], rx[i + 1], 0x00};
                        sendAllBytes(cfd, subAck, 5);
                    } else if (ptype == 0xC0U) {
                        const uint8_t pingResp[2] = {0xD0, 0x00};
                        sendAllBytes(cfd, pingResp, 2);
                    } else if (ptype == 0x30U && rem >= 2) {
                        uint16_t tLen = (uint16_t(rx[i]) << 8) | rx[i + 1];
                        size_t pPos = i + 2 + tLen + ((((hdr >> 1) & 3U) > 0) ? 2U : 0U);
                        if (pPos <= i + rem) {
                            std::string t(reinterpret_cast<const char*>(rx.data() + i + 2), tLen);
                            std::string p(reinterpret_cast<const char*>(rx.data() + pPos), (i + rem) - pPos);
                            handleFrontendMqttMessage(t, p);
                        }
                    } else if (ptype == 0xE0U) {
                        rx.clear();
                        ::close(cfd);
                        return;
                    }
                    rx.erase(rx.begin(), rx.begin() + i + rem);
                }
            }
            ::close(cfd);
        }).detach();
    }
    ::close(srv);
}

// ============================================================================
// HTTP Server (:80 — /api/mqtt/broker-target + /api/e2e/state + /api/e2e/framebuffer.raw)
// ============================================================================

void handleHttpClient(int cfd) {
    struct timeval tv{2, 0};
    ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    std::string req;
    char buf[4096];
    size_t hdrEnd = std::string::npos;
    size_t expectedBodyLen = 0;
    while (req.size() < 16384U) {
        ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));
        if (hdrEnd == std::string::npos) {
            hdrEnd = req.find("\r\n\r\n");
            if (hdrEnd != std::string::npos) {
                std::string hdrLower = toLowerAscii(req.substr(0, hdrEnd));
                size_t clPos = hdrLower.find("content-length:");
                if (clPos != std::string::npos) {
                    expectedBodyLen = static_cast<size_t>(std::atoi(hdrLower.c_str() + clPos + 15));
                }
            }
        }
        if (hdrEnd != std::string::npos && req.size() >= hdrEnd + 4U + expectedBodyLen) {
            break;
        }
    }
    if (req.empty()) {
        ::close(cfd);
        return;
    }
    size_t sp1 = req.find(' ');
    size_t sp2 = (sp1 != std::string::npos) ? req.find(' ', sp1 + 1) : std::string::npos;
    std::string method = (sp1 != std::string::npos) ? req.substr(0, sp1) : "GET";
    std::string path = (sp1 != std::string::npos && sp2 != std::string::npos) ? req.substr(sp1 + 1, sp2 - sp1 - 1) : "/";
    size_t qmark = path.find('?');
    if (qmark != std::string::npos) path = path.substr(0, qmark);

    auto sendJson = [&](int status, const std::string& json) {
        std::ostringstream hdr;
        hdr << "HTTP/1.1 " << status << (status == 200 ? " OK" : " Bad Request") << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << json.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << json;
        std::string s = hdr.str();
        sendAllBytes(cfd, reinterpret_cast<const uint8_t*>(s.data()), s.size());
    };

    if (method == "GET" && (path == "/api/v1/health" || path == "/api/status")) {
        sendJson(200, "{\"status\":\"ok\",\"target\":\"arcadematrix_esp32_posix\"}");
    } else if (method == "GET" && path == "/api/mqtt/broker-target") {
        std::string host;
        uint16_t port = 1883;
        bool fromShowcase = false;
        {
            std::lock_guard<std::mutex> lk(g_am.stateMu);
            host = g_am.resolvedBrokerHost;
            port = g_am.resolvedBrokerPort;
            fromShowcase = g_am.brokerFromShowcase;
        }
        if (host.empty() || host == "127.0.0.1") {
            sockaddr_in localAddr{};
            socklen_t addrLen = sizeof(localAddr);
            if (::getsockname(cfd, reinterpret_cast<sockaddr*>(&localAddr), &addrLen) == 0) {
                char ipBuf[INET_ADDRSTRLEN] = "";
                if (::inet_ntop(AF_INET, &localAddr.sin_addr, ipBuf, sizeof(ipBuf))) {
                    host = ipBuf;
                }
            }
        }
        std::ostringstream js;
        js << "{"
           << "\"host\":\"" << jsonEscape(host) << "\","
           << "\"port\":" << port << ","
           << "\"source\":\"" << (fromShowcase ? "showcase" : "local") << "\","
           << "\"mqtt_enabled\":" << (g_am.mqttEnabled.load() ? "true" : "false") << ","
           << "\"showcase_broker_active\":" << (fromShowcase ? "true" : "false")
           << "}";
        sendJson(200, js.str());
    } else if (method == "GET" && path == "/api/e2e/framebuffer.raw") {
        uint8_t raw[kFrameBytes];
        {
            std::lock_guard<std::mutex> lk(g_am.fbMu);
            std::memcpy(raw, g_am.publishedFb565, kFrameBytes);
        }
        std::ostringstream hdr;
        hdr << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: application/octet-stream\r\n"
            << "Content-Length: " << kFrameBytes << "\r\n"
            << "Connection: close\r\n\r\n";
        std::string h = hdr.str();
        sendAllBytes(cfd, reinterpret_cast<const uint8_t*>(h.data()), h.size());
        sendAllBytes(cfd, raw, kFrameBytes);
    } else if (method == "GET" && path == "/api/e2e/state") {
        std::string activeSrc, lastTopic, lastSys, lastGame, marqueePath, bHost;
        uint8_t prio = 10;
        bool playing = false, wantsIg = false, fromSc = false, cacheEver = false;
        uint16_t bPort = 1883;
        {
            std::lock_guard<std::mutex> lk(g_am.stateMu);
            activeSrc = g_am.activeSource;
            prio = g_am.activePriority;
            playing = g_am.isGamePlaying;
            wantsIg = g_am.wantsInGameDisplay;
            lastTopic = g_am.lastMqttTopic;
            lastSys = g_am.lastSystem;
            lastGame = g_am.lastGame;
            marqueePath = g_am.activeMarqueePath;
            bHost = g_am.resolvedBrokerHost;
            bPort = g_am.resolvedBrokerPort;
            fromSc = g_am.brokerFromShowcase;
            cacheEver = g_am.sdSyncCacheEverStaged;
        }
        uint32_t crc = 0, nz = 0;
        size_t distinctCrcs = 0;
        {
            std::lock_guard<std::mutex> lk(g_am.fbMu);
            crc = g_am.frameCrc32;
            nz = g_am.nonzeroPixels;
            distinctCrcs = g_am.distinctStreamCrcs.size();
        }
        const bool syncCacheExistsNow = fileExists(g_am.sdRoot + "/cache/showcase_sync.dmdz");
        std::ostringstream js;
        js << "{"
           << "\"target\":\"arcadematrix_esp32\","
           << "\"width\":" << kPanelW << ","
           << "\"height\":" << kPanelH << ","
           << "\"sync_client_id\":\"" << jsonEscape(g_am.syncClientId) << "\","
           << "\"active_source\":\"" << jsonEscape(activeSrc) << "\","
           << "\"active_priority\":" << unsigned(prio) << ","
           << "\"is_playing\":" << (playing ? "true" : "false") << ","
           << "\"wants_ingame_display\":" << (wantsIg ? "true" : "false") << ","
           << "\"last_mqtt_topic\":\"" << jsonEscape(lastTopic) << "\","
           << "\"last_system\":\"" << jsonEscape(lastSys) << "\","
           << "\"last_game\":\"" << jsonEscape(lastGame) << "\","
           << "\"active_marquee_path\":\"" << jsonEscape(marqueePath) << "\","
           << "\"broker_host\":\"" << jsonEscape(bHost) << "\","
           << "\"broker_port\":" << bPort << ","
           << "\"broker_from_showcase\":" << (fromSc ? "true" : "false") << ","
           << "\"mqtt_events_received\":" << g_am.mqttEventsReceived.load() << ","
           << "\"showcase_enqueues\":" << g_am.showcaseEnqueues.load() << ","
           << "\"showcase_streams_completed\":" << g_am.showcaseStreamsCompleted.load() << ","
           << "\"showcase_queue_deletes\":" << g_am.showcaseQueueDeletes.load() << ","
           << "\"presented_stream_frames\":" << g_am.streamPlayer.presentedFrames() << ","
           << "\"distinct_stream_crcs\":" << distinctCrcs << ","
           << "\"sd_sync_cache_staged_ever\":" << (cacheEver ? "true" : "false") << ","
           << "\"sd_sync_cache_exists\":" << (syncCacheExistsNow ? "true" : "false") << ","
           << "\"nonzero_pixels\":" << nz << ","
           << "\"frame_crc32\":" << crc
           << "}";
        sendJson(200, js.str());
    } else if (method == "POST" && path == "/api/e2e/config") {
        size_t bodyPos = req.find("\r\n\r\n");
        std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
        g_am.useShowcaseBroker.store(extractJsonBool(body, "use_showcase_broker", g_am.useShowcaseBroker.load()));
        g_am.ingameShowcaseEnabled.store(extractJsonBool(body, "ingame_showcase_enabled", g_am.ingameShowcaseEnabled.load()));
        refreshBrokerResolution();
        sendJson(200, "{\"ok\":true}");
    } else {
        sendJson(400, "{\"error\":\"not_found\"}");
    }
    ::close(cfd);
}

void runHttpServerLoop(uint16_t port) {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return;
    int opt = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(srv, 32) != 0) {
        std::fprintf(stderr, "[arcadematrix-e2e] Failed to bind HTTP port %u\n", unsigned(port));
        ::close(srv);
        std::exit(1);
    }
    struct timeval tv{0, 250000};
    ::setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (g_running.load(std::memory_order_relaxed)) {
        int cfd = ::accept(srv, nullptr, nullptr);
        if (cfd < 0) continue;
        std::thread(handleHttpClient, cfd).detach();
    }
    ::close(srv);
}

void onSignal(int) {
    g_running.store(false);
}

} // namespace

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    if (const char* v = std::getenv("AM_SD_ROOT")) g_am.sdRoot = v;
    if (const char* v = std::getenv("SHOWCASE_HOST")) g_am.showcaseHost = v;
    if (const char* v = std::getenv("SHOWCASE_PORT")) g_am.showcasePort = static_cast<uint16_t>(std::atoi(v));
    if (const char* v = std::getenv("AM_HTTP_PORT")) g_am.httpPort = static_cast<uint16_t>(std::atoi(v));
    if (const char* v = std::getenv("AM_LOCAL_BROKER_PORT")) g_am.localBrokerPort = static_cast<uint16_t>(std::atoi(v));
    if (const char* v = std::getenv("AM_USE_SHOWCASE_BROKER")) g_am.useShowcaseBroker.store(std::atoi(v) != 0);

    net::ShowcaseDiscovery::formatClientId("games", kPanelW, kPanelH, true, g_am.syncClientId, sizeof(g_am.syncClientId));
    refreshBrokerResolution();

    std::printf(
        "[arcadematrix-e2e] Starting POSIX target: syncClientId=%s showcase=%s:%u http=:%u sd=%s\n",
        g_am.syncClientId,
        g_am.showcaseHost.c_str(),
        unsigned(g_am.showcasePort),
        unsigned(g_am.httpPort),
        g_am.sdRoot.c_str()
    );
    std::fflush(stdout);

    std::thread tBroker(runEmbeddedBrokerLoop, g_am.localBrokerPort);
    std::thread tCore1(core1RenderLoop);
    std::thread tWorker(core0ShowcaseWorkerLoop);
    std::thread tMqtt(mqttSubscriberLoop);
    runHttpServerLoop(g_am.httpPort);

    g_running.store(false);
    tMqtt.join();
    tWorker.join();
    tCore1.join();
    tBroker.join();
    return 0;
}
