/**
 * @file MemTrace.h
 * @brief Optional DRAM accounting used to size the system zone vs. the engine sandbox.
 *
 * - mark(): always available, logs the internal heap state at a boot milestone (no allocation).
 * - Allocation tracer: only compiled with -DMEM_TRACE=1 (see the esp32dev_memtrace environment). It
 *   wraps malloc/calloc/realloc/heap_caps_* at link time and prints every allocation >= threshold made
 *   AFTER arm(), with the call stack, so every post-boot allocation source can be identified and made static.
 *   The hooks never allocate (ROM ets_printf only) and are a no-op in regular builds.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace MemTrace {

/// Log free heap, largest block and DMA-capable free/largest block with a milestone label.
void mark(const char* label);

/// Start reporting post-boot allocations (call once initialization is complete).
void arm();

#if defined(MEM_TRACE) && MEM_TRACE
/// Mark the start/end of an HTTP request handled on the async_tcp task. Allocations traced in between
/// are tagged with the request, and the free-heap delta of the handler is printed at the end.
void requestBegin(const char* method, const char* url, size_t contentLength);
void requestEnd();
/// Dump every live tracked allocation (>= 48 B, tracked since boot, address unsorted) as `[MS]` lines,
/// plus a `[MS-SUM]` summary. Analyse offline with scripts/analyze_survivors.py. Rate-limited.
void dumpSurvivors(const char* tag);
#else
inline void requestBegin(const char*, const char*, size_t) {}
inline void requestEnd() {}
inline void dumpSurvivors(const char*) {}
#endif

}  // namespace MemTrace
