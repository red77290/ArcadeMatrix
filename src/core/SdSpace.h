#pragma once
#include <stdint.h>

/**
 * @brief SD card capacity and free space, measured off the render core and cached.
 *
 * Counting free clusters walks the allocation table, which on a large FAT32 card takes a second or
 * two on the 1-bit bus while holding the SD lock, so it is never done inside a request handler. A
 * Core 0 task measures once shortly after boot, again whenever the library changes (upload, delete,
 * rename, rescan call requestRefresh()) and every 30 minutes, and /api/stats reports the cached
 * figures with their age.
 */
namespace SdSpace {
    /// Start the measuring task; call once after the card is mounted.
    void start();
    /// Ask for a new measurement soon (rate-limited to one per minute).
    void requestRefresh();
    /// Cached figures. Returns false until the first measurement has completed.
    bool get(uint64_t& totalBytes, uint64_t& freeBytes, uint32_t& ageMs);
}
