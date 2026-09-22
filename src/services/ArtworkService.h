#pragma once
#include <Arduino.h>
#include <mutex>
#include <atomic>
#include "../hal/HardwareHAL.h"

/**
 * @struct ArtworkSnapshot
 * @brief Immutable generational snapshot of decoded album art published to Core 1.
 * Core 1 reads this POD snapshot safely without data races, allocations, or mutexes.
 */
struct ArtworkSnapshot {
    const uint16_t* bitmap = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    uint32_t generation = 0;
};

/**
 * @class ArtworkService
 * @brief Memory-bounded Album Art caching in PSRAM with generational snapshot publishing.
 * Supports plain HTTP (port 80) via WiFiClient and HTTPS via WiFiClientSecure with admission budget.
 */
class ArtworkService {
public:
    ArtworkService();
    ~ArtworkService();

    /**
     * @brief Normalizes artwork URLs: forces 64x64 thumbnail parameters on Google CDN hosts.
     */
    static String normalizeArtworkUrl(const String& url);

    /**
     * @brief Fetches and decodes cover artwork from an HTTP/HTTPS URL into PSRAM cache.
     * @param url Image URL (JPEG/PNG)
     * @param targetWidth Desired target width (e.g. 52 or 64)
     * @param targetHeight Desired target height
     * @return Generated artwork identifier string or empty on failure.
     */
    String loadArtwork(const String& url, int targetWidth, int targetHeight);

    /**
     * @brief Returns an immutable snapshot of current artwork for safe Core 1 rendering.
     * Lock-free, zero-allocation, zero-mutex (Invariant 1).
     */
    ArtworkSnapshot getSnapshot() const;

    /**
     * @brief Helper: returns raw RGB565 pixel buffer for the specified artwork ID.
     */
    const uint16_t* getArtworkBitmap(const char* artworkId, int& width, int& height);
    const uint16_t* getArtworkBitmap(const String& artworkId, int& width, int& height);

    /**
     * @brief Clears cached artwork buffers and frees PSRAM memory.
     */
    void clear();

private:
    mutable std::mutex _mutex;
    String _currentArtworkId;
    String _currentUrl;
    std::atomic<const uint16_t*> _activeBitmapBuffer{nullptr};
    uint16_t* _retiredBitmapBuffer = nullptr;
    std::atomic<int> _width{0};
    std::atomic<int> _height{0};
    std::atomic<uint32_t> _generation{0};
    std::atomic<uint32_t> _currentArtworkTimestamp{0};
    bool _hasPsram = false;
};

extern ArtworkService artworkService;

