#include "MemTrace.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Logger.h"

namespace MemTrace {

void mark(const char* label) {
    LOGI("MemTrace", "%-28s free=%u largest=%u | dmaFree=%u dmaLargest=%u | minEver=%u | taskStackFree=%u",
         label,
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
         (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
         (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

}  // namespace MemTrace

#if defined(MEM_TRACE) && MEM_TRACE

#include <rom/ets_sys.h>
#include <esp_debug_helpers.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>

namespace {
volatile bool s_armed = false;
constexpr size_t kMinTracedBytes = 256;
constexpr int kMaxFrames = 8;
// Per-task re-entrancy guard: the report path must never recurse into the traced allocators.
__thread bool t_inHook = false;

// Current HTTP request being served by the single async_tcp task (fixed buffer: no allocation).
char s_req[96] = "-";
volatile uint32_t s_reqStartFree = 0;

void report(char kind, size_t size, uint32_t caps, void* result) {
    if (!s_armed || size < kMinTracedBytes || t_inHook) return;
    t_inHook = true;
    uint32_t pcs[kMaxFrames];
    int n = 0;
    esp_backtrace_frame_t frame;
    esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
    // Skip the hook frame itself; collect the callers.
    while (n < kMaxFrames) {
        pcs[n++] = (frame.pc & 0x3FFFFFFFu) | 0x40000000u;
        if (!esp_backtrace_get_next_frame(&frame)) break;
    }
    const char* task = pcTaskGetName(nullptr);
    const bool onAsync = task && task[0] == 'a' && task[1] == 's' && task[2] == 'y';  // "async_tcp"
    ets_printf("[MT] %c %u caps=0x%x ok=%d ptr=0x%x core=%d task=%s req=%s free=%u big=%u bt=", kind, (unsigned)size, (unsigned)caps,
               result != nullptr, (unsigned)(uintptr_t)result, (int)xPortGetCoreID(), task, onAsync ? s_req : "-",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    for (int i = 0; i < n; i++) ets_printf("0x%x ", (unsigned)pcs[i]);
    ets_printf("\n");
    t_inHook = false;
}
// ---- Live allocation table (S1): which blocks are alive, who allocated them -------------------------
// 769 slots x 16 B = 12.3 KB of .bss (memtrace env only; perturbs the heap by that amount, accepted).
// Open addressing keyed on ptr, tombstone = 1. Tracks every allocation >= kMinTableBytes since boot.
constexpr size_t kMinTableBytes = 48;
constexpr uint32_t kSlots = 769;
constexpr uint32_t kTomb = 1;
struct __attribute__((packed)) Slot {
    uint32_t ptr;
    uint16_t size;
    uint8_t pc[9];  // 3 caller PCs, 24 bits each (0x40000000 | value)
};
static_assert(sizeof(Slot) == 15, "Slot packing");
Slot s_tab[kSlots];
portMUX_TYPE s_tabMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t s_live = 0;
volatile uint32_t s_overflow = 0;
volatile uint32_t s_lastDumpMs = 0;

inline void pack3(uint8_t* d, uint32_t pc) { d[0] = pc & 0xFF; d[1] = (pc >> 8) & 0xFF; d[2] = (pc >> 16) & 0xFF; }
inline uint32_t unpack3(const uint8_t* s) { return 0x40000000u | s[0] | (s[1] << 8) | (s[2] << 16); }

void untrack(void* p) {
    if (!p) return;
    const uint32_t key = (uint32_t)(uintptr_t)p;
    portENTER_CRITICAL(&s_tabMux);
    uint32_t i = (key >> 3) % kSlots;
    for (uint32_t n = 0; n < kSlots; ++n, i = (i + 1) % kSlots) {
        if (s_tab[i].ptr == 0) break;
        if (s_tab[i].ptr == key) { s_tab[i].ptr = kTomb; --s_live; break; }
    }
    portEXIT_CRITICAL(&s_tabMux);
}

void track(void* p, size_t size) {
    if (!p || size < kMinTableBytes) return;
    uint32_t pcs[5];
    int n = 0;
    esp_backtrace_frame_t frame;
    esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
    while (n < 5) {
        pcs[n++] = frame.pc;
        if (!esp_backtrace_get_next_frame(&frame)) break;
    }
    // Frames 0/1 are this tracker and the __wrap_ function; keep the next three callers.
    uint32_t c[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) c[k] = (2 + k < n) ? pcs[2 + k] : 0;
    const uint32_t key = (uint32_t)(uintptr_t)p;
    portENTER_CRITICAL(&s_tabMux);
    uint32_t i = (key >> 3) % kSlots;
    bool placed = false;
    for (uint32_t k = 0; k < kSlots; ++k, i = (i + 1) % kSlots) {
        if (s_tab[i].ptr == 0 || s_tab[i].ptr == kTomb) {
            s_tab[i].ptr = key;
            s_tab[i].size = size > 0xFFFF ? 0xFFFF : (uint16_t)size;
            for (int q = 0; q < 3; ++q) pack3(&s_tab[i].pc[q * 3], c[q]);
            ++s_live;
            placed = true;
            break;
        }
    }
    if (!placed) ++s_overflow;
    portEXIT_CRITICAL(&s_tabMux);
}
}  // namespace

void MemTrace::dumpSurvivors(const char* tag) {
    const uint32_t now = millis();
    if (s_lastDumpMs != 0 && now - s_lastDumpMs < 20000) return;  // rate limit: the dump is slow (UART)
    s_lastDumpMs = now;
    t_inHook = true;
    ets_printf("[MS-BEGIN] %s t=%u\n", tag, (unsigned)now);
    for (uint32_t i = 0; i < kSlots; ++i) {
        Slot s;
        portENTER_CRITICAL(&s_tabMux);
        s = s_tab[i];
        portEXIT_CRITICAL(&s_tabMux);
        if (s.ptr <= kTomb) continue;
        ets_printf("[MS] 0x%x %u 0x%x 0x%x 0x%x\n", (unsigned)s.ptr, (unsigned)s.size,
                   (unsigned)unpack3(&s.pc[0]), (unsigned)unpack3(&s.pc[3]), (unsigned)unpack3(&s.pc[6]));
        if ((i & 15) == 0) esp_task_wdt_reset();
    }
    ets_printf("[MS-SUM] %s live=%u overflow=%u free=%u largest=%u\n", tag, (unsigned)s_live, (unsigned)s_overflow,
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    t_inHook = false;
}

void MemTrace::arm() { s_armed = true; }

void MemTrace::requestBegin(const char* method, const char* url, size_t contentLength) {
    // Fixed-buffer copy: no allocation (this runs on the async_tcp task, inside the traced allocators' domain).
    size_t i = 0;
    for (const char* p = method; p && *p && i < 8; ++p) s_req[i++] = *p;
    s_req[i++] = ':';
    for (const char* p = url; p && *p && i < sizeof(s_req) - 1; ++p) s_req[i++] = *p;
    s_req[i] = '\0';
    s_reqStartFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_armed) {
        ets_printf("[MT] REQ %s len=%u free=%u largest=%u\n", s_req, (unsigned)contentLength, (unsigned)s_reqStartFree,
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
}

void MemTrace::requestEnd() {
    if (s_armed) {
        const uint32_t nowFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ets_printf("[MT] REQ-END %s delta=%d free=%u largest=%u\n", s_req, (int)nowFree - (int)s_reqStartFree,
                   (unsigned)nowFree, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
}

extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void* __real_heap_caps_malloc(size_t, uint32_t);
void* __real_heap_caps_calloc(size_t, size_t, uint32_t);
void* __real_heap_caps_realloc(void*, size_t, uint32_t);

void __real_free(void*);
void __real_heap_caps_free(void*);

void* __wrap_malloc(size_t s) { void* p = __real_malloc(s); track(p, s); report('m', s, 0, p); return p; }
void* __wrap_calloc(size_t n, size_t s) { void* p = __real_calloc(n, s); track(p, n * s); report('c', n * s, 0, p); return p; }
void* __wrap_realloc(void* o, size_t s) { void* p = __real_realloc(o, s); if (p) { untrack(o); track(p, s); } report('r', s, 0, p); return p; }
void* __wrap_heap_caps_malloc(size_t s, uint32_t c) { void* p = __real_heap_caps_malloc(s, c); track(p, s); report('M', s, c, p); return p; }
void* __wrap_heap_caps_calloc(size_t n, size_t s, uint32_t c) { void* p = __real_heap_caps_calloc(n, s, c); track(p, n * s); report('C', n * s, c, p); return p; }
void* __wrap_heap_caps_realloc(void* o, size_t s, uint32_t c) { void* p = __real_heap_caps_realloc(o, s, c); if (p) { untrack(o); track(p, s); } report('R', s, c, p); return p; }
void __wrap_free(void* p) { untrack(p); __real_free(p); }
void __wrap_heap_caps_free(void* p) { untrack(p); __real_heap_caps_free(p); }
}

#else

void MemTrace::arm() {}

#endif
