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
