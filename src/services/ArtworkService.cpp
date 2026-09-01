#include "ArtworkService.h"
#include "../core/Logger.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

ArtworkService artworkService;

ArtworkService::ArtworkService()
    : _bitmapBuffer(nullptr), _width(0), _height(0), _hasPsram(false) {
    _hasPsram = psramFound();
}

ArtworkService::~ArtworkService() {
    clear();
}

void ArtworkService::clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_bitmapBuffer) {
        free(_bitmapBuffer);
        _bitmapBuffer = nullptr;
    }
    _width = 0;
    _height = 0;
    _currentArtworkId = "";
    _currentUrl = "";
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

static bool fetchAndDecode(const String& downloadUrl, uint16_t* targetBuf, int targetW, int targetH, bool hasPsram) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(4000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    if (!http.begin(client, downloadUrl)) {
        LOGW("ArtworkService", "HTTP begin failed for URL: %s", downloadUrl.c_str());
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_MOVED_PERMANENTLY) {
        LOGW("ArtworkService", "HTTP GET failed with code: %d for %s", httpCode, downloadUrl.c_str());
        http.end();
        client.stop();
        return false;
    }

    size_t maxAlloc = hasPsram ? (128 * 1024) : (32 * 1024);
    uint8_t* imgData = hasPsram ? 
        (uint8_t*)heap_caps_malloc(maxAlloc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) :
        (uint8_t*)malloc(maxAlloc);

    if (!imgData) {
        LOGE("ArtworkService", "Failed to allocate download buffer (%u bytes)", (unsigned)maxAlloc);
        http.end();
        client.stop();
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
    client.stop();

    if (bytesRead < 10) {
        LOGW("ArtworkService", "Image download was empty or too small (%u bytes) for %s", (unsigned)bytesRead, downloadUrl.c_str());
        free(imgData);
        return false;
    }

    // Decode image into RGB565 bitmap buffer
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

    std::lock_guard<std::mutex> lock(_mutex);
    if (url == _currentUrl && !_currentArtworkId.isEmpty()) {
        return _currentArtworkId; // Cache hit
    }

    _hasPsram = psramFound();
    if (WiFi.status() != WL_CONNECTED) return "";

    _currentUrl = url;
    _width = targetWidth;
    _height = targetHeight;

    size_t bufSize = _width * _height * sizeof(uint16_t);
    if (!_bitmapBuffer) {
        if (_hasPsram) {
            _bitmapBuffer = (uint16_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        } else {
            _bitmapBuffer = (uint16_t*)malloc(bufSize);
        }
    }

    if (!_bitmapBuffer) {
        LOGE("ArtworkService", "Failed to allocate %u bytes for artwork buffer!", (unsigned)bufSize);
        return "";
    }
    memset(_bitmapBuffer, 0, bufSize);

    // 1. Build wsrv.nl proxy URL for downscaling to target dimensions
    String proxyUrl = url;
    if (url.startsWith("http://") || url.startsWith("https://")) {
        if (!url.startsWith("https://wsrv.nl/")) {
            proxyUrl = "https://wsrv.nl/?url=" + url + "&w=" + String(_width) + "&h=" + String(_height) + "&fit=cover&output=png";
        }
    }

    LOGI("ArtworkService", "Downloading album cover (resized %dx%d via wsrv.nl proxy)", _width, _height);
    bool decoded = fetchAndDecode(proxyUrl, _bitmapBuffer, _width, _height, _hasPsram);

    // 2. If proxy fails, try direct URL
    if (!decoded && proxyUrl != url) {
        LOGW("ArtworkService", "wsrv.nl proxy failed, trying direct URL: %s", url.c_str());
        decoded = fetchAndDecode(url, _bitmapBuffer, _width, _height, _hasPsram);
    }

    if (!decoded) {
        LOGW("ArtworkService", "Failed to decode image from URL: %s", url.c_str());
        return "";
    }

    // Generate unique artwork ID
    _currentArtworkId = "art_" + String(millis());
    LOGI("ArtworkService", "Album artwork loaded successfully (ID: %s, %dx%d)", _currentArtworkId.c_str(), _width, _height);
    return _currentArtworkId;
}

const uint16_t* ArtworkService::getArtworkBitmap(const String& artworkId, int& width, int& height) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (artworkId.isEmpty() || artworkId != _currentArtworkId || !_bitmapBuffer) {
        width = 0;
        height = 0;
        return nullptr;
    }
    width = _width;
    height = _height;
    return _bitmapBuffer;
}
