#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstdint>

#define MALLOC_CAP_SPIRAM 0x01
#define MALLOC_CAP_8BIT   0x02

inline void* heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}

inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) {
    (void)caps;
    return realloc(ptr, size);
}

inline void heap_caps_free(void* ptr) {
    free(ptr);
}
