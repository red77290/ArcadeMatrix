#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstdint>

#define MALLOC_CAP_SPIRAM   0x01
#define MALLOC_CAP_8BIT     0x02
#define MALLOC_CAP_INTERNAL 0x04

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

inline size_t heap_caps_get_free_size(uint32_t caps) {
    (void)caps;
    return 200000U;
}

inline size_t heap_caps_get_largest_free_block(uint32_t caps) {
    (void)caps;
    return 120000U;
}
