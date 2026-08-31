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

static PNG s_png;
static uint16_t* s_targetBuf = nullptr;
static int s_targetW = 0;
static int s_targetH = 0;

static int pngDrawToBuffer(PNGDRAW *pDraw) {
    if (!s_targetBuf || pDraw->y >= s_targetH) return 0;

    uint16_t lineBuf[128];
    s_png.getLineAsRGB565(pDraw, lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);

    int copyW = min((int)pDraw->iWidth, s_targetW);
    for (int x = 0; x < copyW; x++) {
        s_targetBuf[pDraw->y * s_targetW + x] = lineBuf[x];
    }
    return 1;
}

String ArtworkService::loadArtwork(const String& url, int targetWidth, int targetHeight) {
    if (url.isEmpty()) return "";

    std::lock_guard<std::mutex> lock(_mutex);
    if (url == _currentUrl && !_currentArtworkId.isEmpty()) {
        return _currentArtworkId; // Cache hit
    }

    if (!_hasPsram) {
        LOGD("ArtworkService", "Skipping album art download: PSRAM not available.");
        return "";
    }

    if (WiFi.status() != WL_CONNECTED) return "";

    _currentUrl = url;
    _width = targetWidth;
    _height = targetHeight;

    size_t bufSize = _width * _height * sizeof(uint16_t);
    if (!_bitmapBuffer) {
        _bitmapBuffer = (uint16_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    if (!_bitmapBuffer) {
        LOGE("ArtworkService", "Failed to allocate %u bytes in PSRAM for artwork!", (unsigned)bufSize);
        return "";
    }
    memset(_bitmapBuffer, 0, bufSize);

    // Download artwork from HTTP stream
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(client, url)) {
        LOGW("ArtworkService", "HTTP begin failed for URL: %s", url.c_str());
        return "";
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        LOGW("ArtworkService", "HTTP GET failed with code: %d", httpCode);
        http.end();
        client.stop();
        return "";
    }

    int len = http.getSize();
    if (len <= 0 || len > (128 * 1024)) { // Max 128KB image safety bound
        http.end();
        client.stop();
        return "";
    }

    uint8_t* imgData = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!imgData) {
        http.end();
        client.stop();
        return "";
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t bytesRead = 0;
    while (http.connected() && (bytesRead < (size_t)len)) {
        size_t avail = stream->available();
        if (avail) {
            int read = stream->readBytes(imgData + bytesRead, avail);
            bytesRead += read;
        }
        delay(1);
    }
    http.end();
    client.stop();

    // Decode PNG image into RGB565 bitmap buffer
    s_targetBuf = _bitmapBuffer;
    s_targetW = _width;
    s_targetH = _height;

    int rc = s_png.openRAM(imgData, bytesRead, pngDrawToBuffer);
    if (rc == PNG_SUCCESS) {
        s_png.decode(NULL, 0);
        s_png.close();
    }

    free(imgData);

    // Generate unique artwork ID
    _currentArtworkId = "art_" + String(millis());
    LOGI("ArtworkService", "Album artwork loaded (ID: %s, %dx%d)", _currentArtworkId.c_str(), _width, _height);
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
