#pragma once

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// Custom ArduinoJson allocator that prefers PSRAM (falls back to internal
// DRAM only if no PSRAM is available or the SPIRAM pool is exhausted).
//
// Rationale: JSON parsing buffers for polled network engines (Google Cast,
// Spotify, weather, dashboard providers, ...) are transient but can be
// allocated at a high frequency (e.g. every ~1.5s for continuous status
// polling). Using the default DynamicJsonDocument allocator pulls these
// allocations from internal DRAM every cycle, which fragments/exhausts the
// internal heap much faster than a one-shot allocation would, starving
// concurrent consumers (WebUI AsyncWebServer, TLS handshakes, FighterEngine
// preload, ...). Routing them to PSRAM instead removes that pressure without
// touching the fixed ~16-18KB internal-DRAM cost that TLS handshakes
// themselves still require (see NetworkBudget.h).
struct SpiRamAllocator {
  void* allocate(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(size);
    return p;
  }
  void deallocate(void* pointer) {
    free(pointer);
  }
  void* reallocate(void* ptr, size_t new_size) {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = realloc(ptr, new_size);
    return p;
  }
};

using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;
