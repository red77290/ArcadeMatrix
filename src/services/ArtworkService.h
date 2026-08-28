#pragma once
#include <Arduino.h>
#include <mutex>
#include "../hal/HardwareHAL.h"

/**
 * @class ArtworkService
 * @brief Memory-bounded Album Art caching in PSRAM.
 * Avoids raw String allocations by streaming network bytes into pre-allocated memory buffers.
 */
class ArtworkService {
public:
    ArtworkService();
    ~ArtworkService();

    /**
     * @brief Fetches and decodes cover artwork from an HTTP URL into PSRAM cache.
     * @param url Image URL (JPEG/PNG)
     * @param targetWidth Desired target width (e.g. 32 or 64)
     * @param targetHeight Desired target height
     * @return Generated artwork identifier string or empty on failure.
     */
    String loadArtwork(const String& url, int targetWidth, int targetHeight);

    /**
     * @brief Returns raw RGB565 pixel buffer for the specified artwork ID.
     */
    const uint16_t* getArtworkBitmap(const String& artworkId, int& width, int& height);

    /**
     * @brief Clears cached artwork buffers and frees PSRAM memory.
     */
    void clear();

private:
    std::mutex _mutex;
    String _currentArtworkId;
    String _currentUrl;
    uint16_t* _bitmapBuffer;
    int _width;
    int _height;
    bool _hasPsram;
};

extern ArtworkService artworkService;
