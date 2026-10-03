#include "MemoryManager.h"
#include <algorithm>

MemoryManager& MemoryManager::instance() {
    static MemoryManager s_instance;
    return s_instance;
}

MemorySnapshot MemoryManager::captureSnapshot() const {
    MemorySnapshot snap;

#if defined(ESP32)
    multi_heap_info_t internalInfo;
    heap_caps_get_info(&internalInfo, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snap.internal.freeBytes = internalInfo.total_free_bytes;
    snap.internal.largestBlock = internalInfo.largest_free_block;
    snap.internal.minWatermark = internalInfo.minimum_free_bytes;
    if (snap.internal.freeBytes > 0) {
        snap.internal.fragmentationPct = static_cast<uint8_t>(
            100 - std::min<size_t>(100, (snap.internal.largestBlock * 100) / snap.internal.freeBytes)
        );
    }

    multi_heap_info_t dmaInfo;
    heap_caps_get_info(&dmaInfo, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    snap.dma.freeBytes = dmaInfo.total_free_bytes;
    snap.dma.largestBlock = dmaInfo.largest_free_block;
    snap.dma.minWatermark = dmaInfo.minimum_free_bytes;
    if (snap.dma.freeBytes > 0) {
        snap.dma.fragmentationPct = static_cast<uint8_t>(
            100 - std::min<size_t>(100, (snap.dma.largestBlock * 100) / snap.dma.freeBytes)
        );
    }

    snap.hasPsram = (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0);
    if (snap.hasPsram) {
        multi_heap_info_t psramInfo;
        heap_caps_get_info(&psramInfo, MALLOC_CAP_SPIRAM);
        snap.psram.freeBytes = psramInfo.total_free_bytes;
        snap.psram.largestBlock = psramInfo.largest_free_block;
        snap.psram.minWatermark = psramInfo.minimum_free_bytes;
        if (snap.psram.freeBytes > 0) {
            snap.psram.fragmentationPct = static_cast<uint8_t>(
                100 - std::min<size_t>(100, (snap.psram.largestBlock * 100) / snap.psram.freeBytes)
            );
        }
    }
#else
    // Native / Test mock baseline
    snap.internal.freeBytes = 120000;
    snap.internal.largestBlock = 80000;
    snap.internal.minWatermark = 60000;
    snap.internal.fragmentationPct = 33;

    snap.dma.freeBytes = 60000;
    snap.dma.largestBlock = 40000;
    snap.dma.minWatermark = 30000;
    snap.dma.fragmentationPct = 33;
    snap.hasPsram = false;
#endif

    // Compute discrete pressure level
    if (snap.internal.freeBytes < 35000 || snap.internal.largestBlock < 20000) {
        snap.pressureLevel = MemoryPressureLevel::Critical;
    } else if (snap.internal.freeBytes < 50000 || snap.internal.largestBlock < 30000) {
        snap.pressureLevel = MemoryPressureLevel::Moderate;
    } else {
        snap.pressureLevel = MemoryPressureLevel::Nominal;
    }

    return snap;
}

bool MemoryManager::canAllocate(const MemoryRequirements& req) const {
    if (req.bytes == 0) return true;
#if defined(ESP32)
    size_t largest = heap_caps_get_largest_free_block(req.capabilities);
    size_t align = req.alignment > 0 ? req.alignment : 4;
    size_t alignedBytes = (req.bytes + align - 1) & ~(align - 1);
    return (largest >= alignedBytes);
#else
    return (req.bytes <= 80000);
#endif
}

uint8_t MemoryManager::computeHealthScore() const {
    auto snap = captureSnapshot();
    if (snap.internal.freeBytes == 0) return 0;

    // Weight 1: Free Internal (Target 60 KB = 100%) -> 40 pts
    uint32_t freeScore = std::min<uint32_t>(100, (snap.internal.freeBytes * 100) / 60000);
    // Weight 2: Largest Block (Target 35 KB = 100%) -> 40 pts
    uint32_t blockScore = std::min<uint32_t>(100, (snap.internal.largestBlock * 100) / 35000);
    // Weight 3: Internal Fragmentation Inversion -> 20 pts
    uint32_t fragScore = 100 - snap.internal.fragmentationPct;

    return static_cast<uint8_t>((freeScore * 40 + blockScore * 40 + fragScore * 20) / 100);
}
