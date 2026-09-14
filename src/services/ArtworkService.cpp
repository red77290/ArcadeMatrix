#include "ArtworkService.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#include <memory>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

ArtworkService artworkService;

ArtworkService::ArtworkService()
    : _activeBitmapBuffer(nullptr), _retiredBitmapBuffer(nullptr),
      _width(0), _height(0), _generation(0), _hasPsram(false) {
    _hasPsram = psramFound();
}

ArtworkService::~ArtworkService() {
    clear();
}

void ArtworkService::clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_activeBitmapBuffer) {
        free(_activeBitmapBuffer);
        _activeBitmapBuffer = nullptr;
    }
    if (_retiredBitmapBuffer) {
        free(_retiredBitmapBuffer);
        _retiredBitmapBuffer = nullptr;
    }
    _width = 0;
    _height = 0;
    _currentArtworkId = "";
    _currentUrl = "";
    _generation++;
}

String ArtworkService::normalizeArtworkUrl(const String& url) {
    if (url.isEmpty()) return "";
    if (url.startsWith("https://")) {
        int hostStart = 8;
        int hostEnd = url.indexOf('/', hostStart);
        String host = (hostEnd != -1) ? url.substring(hostStart, hostEnd) : url.substring(hostStart);
        if (host.endsWith("googleusercontent.com") || host.endsWith("ggpht.com")) {
            return "http://" + url.substring(8);
        }
    }
    return url;
}

#include <PNGdec.h>
#ifdef INTELSHORT
#undef INTELSHORT
#endif
#ifdef INTELLONG
#undef INTELLONG
#endif
#ifdef MOTOSHORT
#undef MOTOSHORT
#endif
#ifdef MOTOLONG
#undef MOTOLONG
#endif
#include <JPEGDEC.h>

static PNG* s_currentPng = nullptr;
static uint16_t* s_targetBuf = nullptr;
static int s_targetW = 0;
static int s_targetH = 0;

static int pngDrawToBuffer(PNGDRAW *pDraw) {
    if (!s_targetBuf || !s_currentPng || pDraw->y >= s_targetH) return 0;

    uint16_t lineBuf[128];
    s_currentPng->getLineAsRGB565(pDraw, lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);

    int copyW = min((int)pDraw->iWidth, s_targetW);
    for (int x = 0; x < copyW; x++) {
        s_targetBuf[pDraw->y * s_targetW + x] = lineBuf[x];
    }
    return 1;
}

static int jpegDrawToBuffer(JPEGDRAW *pDraw) {
    if (!s_targetBuf || pDraw->y >= s_targetH) return 0;
    int copyW = min((int)pDraw->iWidth, s_targetW - (int)pDraw->x);
    int copyH = min((int)pDraw->iHeight, s_targetH - (int)pDraw->y);
    if (copyW <= 0 || copyH <= 0) return 1;

    for (int y = 0; y < copyH; y++) {
        int targetY = pDraw->y + y;
        if (targetY >= s_targetH) break;
        uint16_t* dst = &s_targetBuf[targetY * s_targetW + pDraw->x];
        const uint16_t* src = &pDraw->pPixels[y * pDraw->iWidth];
        memcpy(dst, src, copyW * sizeof(uint16_t));
    }
    return 1;
}

static bool fetchAndDecode(const String& downloadUrl, uint16_t* targetBuf, int targetW, int targetH, bool hasPsram, int redirectDepth = 0) {
    if (redirectDepth > 3) {
        LOGW("ArtworkService", "Exceeded max redirects (3) for %s", downloadUrl.c_str());
        return false;
    }

    const bool isHttps = downloadUrl.startsWith("https://");

    std::unique_ptr<NetworkBudget::ScopedTlsHandshakeLock> tlsLock;
    if (isHttps) {
        if (!NetworkBudget::canStartTlsSession()) {
            LOGW("ArtworkService", "Skipping HTTPS artwork: insufficient internal DRAM for TLS session.");
            return false;
        }
        // Serialize against every other TLS user (Dashboard, Cast, Spotify, etc.) -- see
        // HardwareHAL::begin() for why mbedTLS must stay internal-DRAM-only on this board.
        // Explicitly unlocked (tlsLock.reset()) before the recursive redirect call below,
        // since the mutex is not recursive/re-entrant.
        tlsLock.reset(new NetworkBudget::ScopedTlsHandshakeLock());
        if (!*tlsLock) {
            LOGW("ArtworkService", "Skipping HTTPS artwork: another TLS handshake is in progress.");
            return false;
        }
    } else {
        NetworkBudget::acquireHttp();
    }

    HTTPClient http;
    http.setTimeout(4000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS); // Explicit redirect handling
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    bool beginOk = false;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;

    if (isHttps) {
        secureClient.setInsecure();
        beginOk = http.begin(secureClient, downloadUrl);
    } else {
        beginOk = http.begin(plainClient, downloadUrl);
    }

    if (!beginOk) {
        LOGW("ArtworkService", "HTTP begin failed for URL: %s", downloadUrl.c_str());
        if (!isHttps) NetworkBudget::releaseHttp();
        return false;
    }

    int httpCode = http.GET();

    // Check for HTTP Redirection (3xx)
    if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND ||
        httpCode == HTTP_CODE_SEE_OTHER || httpCode == HTTP_CODE_TEMPORARY_REDIRECT) {
        String newUrl = http.getLocation();
        http.end();
        if (isHttps) secureClient.stop();
        else { plainClient.stop(); NetworkBudget::releaseHttp(); }

        if (newUrl.isEmpty()) {
            LOGW("ArtworkService", "Redirect with empty Location header for %s", downloadUrl.c_str());
            return false;
        }

        LOGI("ArtworkService", "Redirect (%d) -> %s", httpCode, newUrl.c_str());
        tlsLock.reset(); // Release before recursing: the handshake mutex is not re-entrant.
        return fetchAndDecode(newUrl, targetBuf, targetW, targetH, hasPsram, redirectDepth + 1);
    }

    if (httpCode != HTTP_CODE_OK) {
        LOGW("ArtworkService", "HTTP GET failed with code: %d for %s", httpCode, downloadUrl.c_str());
        http.end();
        if (isHttps) secureClient.stop();
        else { plainClient.stop(); NetworkBudget::releaseHttp(); }
        return false;
    }

    size_t maxAlloc = hasPsram ? (128 * 1024) : (32 * 1024);
    uint8_t* imgData = hasPsram ? 
        (uint8_t*)heap_caps_malloc(maxAlloc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) :
        (uint8_t*)malloc(maxAlloc);

    if (!imgData) {
        LOGE("ArtworkService", "Failed to allocate download buffer (%u bytes)", (unsigned)maxAlloc);
        http.end();
        if (isHttps) secureClient.stop();
        else { plainClient.stop(); NetworkBudget::releaseHttp(); }
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t bytesRead = 0;
    uint32_t startRead = millis();

    while (http.connected() && bytesRead < maxAlloc) {
        size_t avail = stream->available();
        if (avail) {
            size_t toRead = min(avail, maxAlloc - bytesRead);
            int r = stream->readBytes(imgData + bytesRead, toRead);
            if (r > 0) bytesRead += r;
            startRead = millis();
        } else {
            if (millis() - startRead > 1500) break;
            delay(2);
        }
    }
    http.end();
    if (isHttps) secureClient.stop();
    else { plainClient.stop(); NetworkBudget::releaseHttp(); }

    if (bytesRead < 10) {
        LOGW("ArtworkService", "Image download was empty or too small (%u bytes) for %s", (unsigned)bytesRead, downloadUrl.c_str());
        free(imgData);
        return false;
    }

    // Decode image into RGB565 bitmap buffer in PSRAM
    s_targetBuf = targetBuf;
    s_targetW = targetW;
    s_targetH = targetH;

    bool decoded = false;
    // 1. Try JPEG decode if JPEG header (0xFF 0xD8)
    if (bytesRead >= 2 && imgData[0] == 0xFF && imgData[1] == 0xD8) {
        auto* jpg = new (std::nothrow) JPEGDEC();
        if (jpg) {
            if (jpg->openRAM(imgData, bytesRead, jpegDrawToBuffer)) {
                if (jpg->decode(0, 0, 0)) {
                    decoded = true;
                }
                jpg->close();
            }
            delete jpg;
        }
    }

    // 2. Fallback to PNG decode
    if (!decoded) {
        auto* png = new (std::nothrow) PNG();
        if (png) {
            s_currentPng = png;
            int rc = png->openRAM(imgData, bytesRead, pngDrawToBuffer);
            if (rc == PNG_SUCCESS) {
                png->decode(NULL, 0);
                png->close();
                decoded = true;
            }
            s_currentPng = nullptr;
            delete png;
        }
    }

    free(imgData);
    return decoded;
}

String ArtworkService::loadArtwork(const String& url, int targetWidth, int targetHeight) {
    if (url.isEmpty()) return "";

    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (url == _currentUrl && !_currentArtworkId.isEmpty()) {
            return _currentArtworkId; // Cache hit
        }
    }

    _hasPsram = psramFound();
    if (WiFi.status() != WL_CONNECTED) return "";

    String effectiveUrl = normalizeArtworkUrl(url);

    size_t bufSize = targetWidth * targetHeight * sizeof(uint16_t);
    uint16_t* newBitmapBuffer = nullptr;
    if (_hasPsram) {
        newBitmapBuffer = (uint16_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        newBitmapBuffer = (uint16_t*)malloc(bufSize);
    }

    if (!newBitmapBuffer) {
        LOGE("ArtworkService", "Failed to allocate %u bytes for new artwork buffer!", (unsigned)bufSize);
        return "";
    }
    memset(newBitmapBuffer, 0, bufSize);

    LOGI("ArtworkService", "Fetching album cover: %s (direct %s, %dx%d)",
         effectiveUrl.c_str(), effectiveUrl.startsWith("https://") ? "HTTPS" : "HTTP", targetWidth, targetHeight);

    bool decoded = fetchAndDecode(effectiveUrl, newBitmapBuffer, targetWidth, targetHeight, _hasPsram);

    if (!decoded) {
        LOGW("ArtworkService", "Failed to decode image from URL: %s", effectiveUrl.c_str());
        free(newBitmapBuffer);
        return "";
    }

    // Publish new snapshot generation and retire previous buffer
    String newId = "art_" + String(millis());
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_retiredBitmapBuffer) {
            free(_retiredBitmapBuffer);
            _retiredBitmapBuffer = nullptr;
        }
        _retiredBitmapBuffer = _activeBitmapBuffer;
        _activeBitmapBuffer = newBitmapBuffer;
        _width = targetWidth;
        _height = targetHeight;
        _currentUrl = url;
        _currentArtworkId = newId;
        _generation++;
    }

    LOGI("ArtworkService", "Album artwork published successfully (ID: %s, gen: %u, %dx%d)",
         newId.c_str(), _generation, targetWidth, targetHeight);
    return newId;
}

ArtworkSnapshot ArtworkService::getSnapshot() const {
    std::lock_guard<std::mutex> lock(_mutex);
    ArtworkSnapshot snap;
    snap.bitmap = _activeBitmapBuffer;
    snap.width = _width;
    snap.height = _height;
    snap.stride = _width;
    snap.generation = _generation;
    snap.artworkId = _currentArtworkId;
    return snap;
}

const uint16_t* ArtworkService::getArtworkBitmap(const String& artworkId, int& width, int& height) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (artworkId.isEmpty() || artworkId != _currentArtworkId || !_activeBitmapBuffer) {
        width = 0;
        height = 0;
        return nullptr;
    }
    width = _width;
    height = _height;
    return _activeBitmapBuffer;
}
