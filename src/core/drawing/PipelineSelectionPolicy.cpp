/**
 * @file PipelineSelectionPolicy.cpp
 * @brief Implementation of PipelineSelectionPolicy with live memory awareness and color depth admission.
 */
#include "PipelineSelectionPolicy.h"
#include "../../hal/BoardProfile.h"
#include "../Logger.h"
#include "../CompatibilityEvaluator.h"
#include "../NetworkBudget.h"

PipelineSelectionResult PipelineSelectionPolicy::evaluate(
    uint16_t width,
    uint16_t height,
    uint8_t colorDepth,
    const String& requestedPipeline,
    bool hasPsram,
    const MemoryBudgetConstraints& memory,
    const EngineRequirements& requirements)
{
    PipelineSelectionResult res;

    // 1. Validate geometry against board profile constraints
    const auto& dispCaps = BoardProfile::current().display();
    if (width > dispCaps.maxWidth || height > dispCaps.maxHeight) {
        LOGE("PipelineSelectionPolicy", "Requested geometry (%ux%u) exceeds profile limits (%ux%u)",
             width, height, dispCaps.maxWidth, dispCaps.maxHeight);
        res.reason = SurfaceSelectionReason::UnsupportedGeometry;
        res.reasonText = "Requested geometry exceeds board profile limits";
        res.valid = false;
        return res;
    }

    // Populate baseline reference memory if unconstrained
    MemoryBudgetConstraints effectiveMem = memory;
    if (effectiveMem.freeInternalHeap == 0 && effectiveMem.largestInternalBlock == 0) {
        ReferenceMemoryProfile ref = CompatibilityEvaluator::getReferenceMemoryProfile(
            hasPsram ? HwProfile::WAVESHARE_S3 : HwProfile::ESP32_STD
        );
        effectiveMem.freeInternalHeap = ref.freeInternalHeap;
        effectiveMem.largestInternalBlock = ref.largestInternalBlock;
        effectiveMem.freePsram = ref.freePsram;
        effectiveMem.freeDmaHeap = hasPsram ? 80000 : 40000;
    }

    String pipeline = requestedPipeline;
    pipeline.toLowerCase();

    // Use candidate nominal depth for initial pipeline sizing
    uint8_t sizingDepth = (colorDepth > 0) ? colorDepth : 8;

    size_t canvasBytes = (size_t)width * height * sizeof(uint16_t);
    size_t dmaBytesSingle = DmaMemoryLayout::calculateTotalBytes(width, height, sizingDepth, false);
    size_t dmaBytesDouble = DmaMemoryLayout::calculateTotalBytes(width, height, sizingDepth, true);

    if (pipeline == "canvas_single") {
        res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
        res.descriptor.canvasStorage = hasPsram ? CanvasStorage::PSRAM : CanvasStorage::SRAM;
        res.descriptor.doubleBuffered = false;
        res.descriptor.dmaDoubleBuffered = false;
        res.descriptor.supportsPSRAMCanvas = hasPsram;
        res.descriptor.requiresInternalDMA = !hasPsram;
        res.descriptor.estimatedCanvasBytes = canvasBytes;
        res.descriptor.estimatedDmaBytes = dmaBytesSingle;
        res.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        res.reasonText = "User requested Canvas Buffered + Single DMA";
    } else if (pipeline == "canvas_double") {
        res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_DOUBLE;
        res.descriptor.canvasStorage = hasPsram ? CanvasStorage::PSRAM : CanvasStorage::SRAM;
        res.descriptor.doubleBuffered = true;
        res.descriptor.dmaDoubleBuffered = true;
        res.descriptor.supportsPSRAMCanvas = hasPsram;
        res.descriptor.requiresInternalDMA = !hasPsram;
        res.descriptor.estimatedCanvasBytes = canvasBytes;
        res.descriptor.estimatedDmaBytes = dmaBytesDouble;
        res.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        res.reasonText = "User requested Canvas Buffered + Double DMA";
    } else if (pipeline == "direct_double") {
        res.descriptor.strategy = PresentationStrategy::DIRECT_DMA_DOUBLE;
        res.descriptor.canvasStorage = CanvasStorage::NONE;
        res.descriptor.doubleBuffered = true;
        res.descriptor.dmaDoubleBuffered = true;
        res.descriptor.supportsPSRAMCanvas = false;
        res.descriptor.requiresInternalDMA = !hasPsram;
        res.descriptor.estimatedCanvasBytes = 0;
        res.descriptor.estimatedDmaBytes = dmaBytesDouble;
        res.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        res.reasonText = "User requested Direct DMA Double Buffer (Legacy)";
    } else if (pipeline == "direct_single") {
        res.descriptor.strategy = PresentationStrategy::DIRECT_DMA_SINGLE;
        res.descriptor.canvasStorage = CanvasStorage::NONE;
        res.descriptor.doubleBuffered = false;
        res.descriptor.dmaDoubleBuffered = false;
        res.descriptor.supportsPSRAMCanvas = false;
        res.descriptor.requiresInternalDMA = !hasPsram;
        res.descriptor.estimatedCanvasBytes = 0;
        res.descriptor.estimatedDmaBytes = dmaBytesSingle;
        res.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        res.reasonText = "User requested Direct DMA Single Buffer (Ultra-low RAM)";
    } else if (hasPsram) {
        // Resource-aware Auto resolution for PSRAM-capable board:
        bool psramCanFitCanvas = (effectiveMem.largestPsramBlock == 0 || effectiveMem.largestPsramBlock >= canvasBytes);
        bool dmaCanFitDouble = (effectiveMem.freeDmaHeap == 0 || effectiveMem.freeDmaHeap >= dmaBytesDouble);

        if (psramCanFitCanvas && dmaCanFitDouble) {
            res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_DOUBLE;
            res.descriptor.canvasStorage = CanvasStorage::PSRAM;
            res.descriptor.doubleBuffered = true;
            res.descriptor.dmaDoubleBuffered = true;
            res.descriptor.supportsPSRAMCanvas = true;
            res.descriptor.requiresInternalDMA = false;
            res.descriptor.estimatedCanvasBytes = canvasBytes;
            res.descriptor.estimatedDmaBytes = dmaBytesDouble;
            res.reason = SurfaceSelectionReason::AutoResolvedPsramCanvas;
            res.reasonText = "Auto-selected Canvas PSRAM + Double DMA (PSRAM & DMA resources verified)";
        } else if (psramCanFitCanvas && !dmaCanFitDouble) {
            res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
            res.descriptor.canvasStorage = CanvasStorage::PSRAM;
            res.descriptor.doubleBuffered = false;
            res.descriptor.dmaDoubleBuffered = false;
            res.descriptor.supportsPSRAMCanvas = true;
            res.descriptor.requiresInternalDMA = false;
            res.descriptor.estimatedCanvasBytes = canvasBytes;
            res.descriptor.estimatedDmaBytes = dmaBytesSingle;
            res.reason = SurfaceSelectionReason::FallbackDirectDma;
            res.reasonText = "Auto-resolved Canvas PSRAM + Single DMA (Constrained DMA heap)";
        } else {
            bool sramCanFitCanvas = (effectiveMem.largestInternalBlock == 0 || effectiveMem.largestInternalBlock >= canvasBytes) &&
                                    (effectiveMem.freeInternalHeap == 0 || effectiveMem.freeInternalHeap >= (canvasBytes + 45000));
            if (sramCanFitCanvas) {
                res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
                res.descriptor.canvasStorage = CanvasStorage::SRAM;
                res.descriptor.doubleBuffered = false;
                res.descriptor.dmaDoubleBuffered = false;
                res.descriptor.supportsPSRAMCanvas = false;
                res.descriptor.requiresInternalDMA = true;
                res.descriptor.estimatedCanvasBytes = canvasBytes;
                res.descriptor.estimatedDmaBytes = dmaBytesSingle;
                res.reason = SurfaceSelectionReason::AutoResolvedSramCanvasLowDma;
                res.reasonText = "Fallback to Canvas SRAM + Single DMA (PSRAM fragmented)";
            } else {
                res.descriptor.strategy = PresentationStrategy::DIRECT_DMA_SINGLE;
                res.descriptor.canvasStorage = CanvasStorage::NONE;
                res.descriptor.doubleBuffered = false;
                res.descriptor.dmaDoubleBuffered = false;
                res.descriptor.supportsPSRAMCanvas = false;
                res.descriptor.requiresInternalDMA = true;
                res.descriptor.estimatedCanvasBytes = 0;
                res.descriptor.estimatedDmaBytes = dmaBytesSingle;
                res.reason = SurfaceSelectionReason::FallbackDirectDma;
                res.reasonText = "Fallback to Direct DMA Single (Insufficient heap for canvas)";
            }
        }
    } else {
        // Resource-aware Auto resolution for Classic ESP32 without PSRAM:
        // Must preserve at least 45KB internal DRAM headroom for networking stack
        bool sramCanFit = (effectiveMem.largestInternalBlock == 0 || effectiveMem.largestInternalBlock >= canvasBytes) &&
                          (effectiveMem.freeInternalHeap == 0 || effectiveMem.freeInternalHeap >= (canvasBytes + 45000));
        if (sramCanFit) {
            res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
            res.descriptor.canvasStorage = CanvasStorage::SRAM;
            res.descriptor.doubleBuffered = false;
            res.descriptor.dmaDoubleBuffered = false;
            res.descriptor.supportsPSRAMCanvas = false;
            res.descriptor.requiresInternalDMA = true;
            res.descriptor.estimatedCanvasBytes = canvasBytes;
            res.descriptor.estimatedDmaBytes = dmaBytesSingle;
            res.reason = SurfaceSelectionReason::AutoResolvedSramCanvasLowDma;
            res.reasonText = "Auto-selected Canvas SRAM + Single DMA (Halves DMA RAM on classic ESP32)";
        } else {
            res.descriptor.strategy = PresentationStrategy::DIRECT_DMA_SINGLE;
            res.descriptor.canvasStorage = CanvasStorage::NONE;
            res.descriptor.doubleBuffered = false;
            res.descriptor.dmaDoubleBuffered = false;
            res.descriptor.supportsPSRAMCanvas = false;
            res.descriptor.requiresInternalDMA = true;
            res.descriptor.estimatedCanvasBytes = 0;
            res.descriptor.estimatedDmaBytes = dmaBytesSingle;
            res.reason = SurfaceSelectionReason::FallbackDirectDma;
            res.reasonText = "Fallback to Direct DMA Single (Insufficient internal DRAM for canvas buffer)";
        }
    }

    // 2. Resolve Effective Color Depth based on pipeline admission
    if (colorDepth > 0) {
        // Manual mode: contractually strictly enforced (no auto-downgrade)
        uint8_t manual = colorDepth;
        if (manual < COLOR_DEPTH_MIN) manual = COLOR_DEPTH_MIN;
        if (manual > COLOR_DEPTH_MAX) manual = COLOR_DEPTH_MAX;
        res.effectiveColorDepth = manual;
    } else {
        // Auto mode: evaluate candidate depths from highest quality downwards (up to 8 bits for all platforms)
        static const uint8_t s_candidates[] = {8, 7, 6, 5, 4, 3, 2, 1};
        const uint8_t* candidates = s_candidates;
        size_t numCandidates = sizeof(s_candidates);

        uint8_t selectedDepth = candidates[numCandidates - 1]; // fallback lowest
        for (size_t i = 0; i < numCandidates; ++i) {
            uint8_t d = candidates[i];
            if (pipelineFits(width, height, d, res.descriptor, hasPsram, effectiveMem, requirements)) {
                selectedDepth = d;
                break;
            }
        }
        res.effectiveColorDepth = selectedDepth;
    }

    // Recompute estimated DMA bytes for selected depth
    res.descriptor.estimatedDmaBytes = DmaMemoryLayout::calculateTotalBytes(
        width, height, res.effectiveColorDepth, res.descriptor.dmaDoubleBuffered
    );

    res.valid = true;
    return res;
}

bool PipelineSelectionPolicy::pipelineFits(
    uint16_t width,
    uint16_t height,
    uint8_t depth,
    const PipelineDescriptor& desc,
    bool hasPsram,
    const MemoryBudgetConstraints& memory,
    const EngineRequirements& requirements)
{
    size_t canvasBytes = (desc.canvasStorage == CanvasStorage::SRAM) ? ((size_t)width * height * sizeof(uint16_t)) : 0;
    size_t dmaBytes = DmaMemoryLayout::calculateTotalBytes(width, height, depth, desc.dmaDoubleBuffered);

    // Add HUB75 DMA descriptor overhead (~16 bytes per row plane)
    size_t rows = height / 2;
    size_t descOverhead = rows * depth * (desc.dmaDoubleBuffered ? 2 : 1) * 16;
    size_t totalDmaBytes = dmaBytes + descOverhead;

    if (hasPsram) {
        if (desc.canvasStorage == CanvasStorage::PSRAM) {
            size_t psramCanvas = (size_t)width * height * sizeof(uint16_t);
            if (memory.freePsram > 0 && psramCanvas > memory.freePsram) {
                return false;
            }
            if (memory.largestPsramBlock > 0 && psramCanvas > memory.largestPsramBlock) {
                return false;
            }
        }
        if (memory.freeDmaHeap > 0 && totalDmaBytes > memory.freeDmaHeap) {
            return false;
        }
        return true;
    }

    // Classic ESP32 (no PSRAM):
    // Display allocations come entirely from internal DRAM
    size_t displayInternalBytes = canvasBytes + totalDmaBytes;

    // Calculate system & engine reserves
    size_t netReserve = 0;
    if (requirements.needsTls) {
        netReserve += ResourceReserve::TLS_SOCKET_ADMISSION_RESERVE;
    } else if (requirements.needsNetwork) {
        netReserve += ResourceReserve::ASYNC_TCP_ADMISSION_RESERVE;
    }

    size_t audioReserve = (requirements.needsAudio || requirements.needsAudioInput || requirements.needsAudioOutput)
        ? ResourceReserve::AUDIO_DMA_RING_ADMISSION_RESERVE : 0;

    size_t totalInternalNeeded = displayInternalBytes + netReserve + audioReserve +
                                 ResourceReserve::SYSTEM_MIN_HEADROOM_RESERVE +
                                 requirements.internalPersistentBytes +
                                 requirements.shadowBytesPerFrame;

    if (memory.freeInternalHeap > 0 && totalInternalNeeded > memory.freeInternalHeap) {
        return false;
    }

    // Contiguous internal DRAM block qualification:
    // When display allocations are committed, they consume contiguous blocks from the free heap.
    // The largest remaining contiguous block must be able to satisfy both engine contiguous requirements
    // and the TLS contiguous allocation reserve (if TLS is active).
    size_t minContiguousNeeded = requirements.internalContiguousBytes;
    if (requirements.shadowBytesPerFrame > minContiguousNeeded) {
        minContiguousNeeded = requirements.shadowBytesPerFrame;
    }
    if (requirements.needsTls && ResourceReserve::TLS_CONTIGUOUS_HEADROOM_RESERVE > minContiguousNeeded) {
        minContiguousNeeded = ResourceReserve::TLS_CONTIGUOUS_HEADROOM_RESERVE;
    }

    if (memory.largestInternalBlock > 0) {
        // Nominal baseline display footprint already accounted in reference profile (64x32 @ 6-bit)
        constexpr size_t nominalDisplay = 16384;
        size_t displayDelta = (displayInternalBytes > nominalDisplay) ? (displayInternalBytes - nominalDisplay) : 0;
        size_t remainingLargestBlock = (memory.largestInternalBlock > displayDelta)
            ? (memory.largestInternalBlock - displayDelta) : 0;

        if (minContiguousNeeded > 0 && remainingLargestBlock < minContiguousNeeded) {
            return false;
        }
    }

    return true;
}

uint8_t PipelineSelectionPolicy::resolveEffectiveColorDepth(
    int configuredDepth,
    uint16_t width,
    uint16_t height,
    bool hasPsram,
    const MemoryBudgetConstraints& memory,
    const EngineRequirements& requirements)
{
    uint8_t cDepth = (configuredDepth > 0) ? static_cast<uint8_t>(configuredDepth) : COLOR_DEPTH_AUTO;
    PipelineSelectionResult sel = evaluate(width, height, cDepth, "auto", hasPsram, memory, requirements);
    return sel.effectiveColorDepth;
}

uint8_t PipelineSelectionPolicy::resolveTargetDepth(
    uint8_t configuredDepth,
    bool dynamicColorDepth,
    uint16_t width,
    uint16_t height,
    bool hasPsram,
    const EngineRequirements& reqs,
    uint8_t currentDepth,
    size_t currentLargestBlock,
    size_t currentFreeInternalHeap,
    size_t currentFreeDma,
    bool isDoubleBuffer,
    bool hasCanvas)
{
    // Auto mode resolves to maximum capability ceiling (up to 8 bits)
    uint8_t maxDepthCeiling = (configuredDepth == COLOR_DEPTH_AUTO || configuredDepth == 0)
        ? 8
        : configuredDepth;

    if (maxDepthCeiling > 8) maxDepthCeiling = 8;
    if (maxDepthCeiling < 2) maxDepthCeiling = 2;

    // Dynamic presentation adaptation disabled: enforce configured depth strictly
    if (!dynamicColorDepth) {
        if (configuredDepth == COLOR_DEPTH_AUTO || configuredDepth == 0) {
            return resolveEffectiveColorDepth(0, width, height, hasPsram);
        }
        return configuredDepth;
    }

    // Panels smaller than 128x32 consume <= 8 KB DMA even at 8 bits; no reduction needed
    if (width * height < 128 * 32) {
        return maxDepthCeiling;
    }

    // Calculate bytes reclaimed or consumed per bit of depth
    size_t bytesPerBit = DmaMemoryLayout::calculateTotalBytes(width, height, 1, isDoubleBuffer);
    if (bytesPerBit == 0) {
        bytesPerBit = (size_t)(width * height / 2) * (isDoubleBuffer ? 2 : 1);
    }

    // If current depth is unset or invalid, assume maxDepthCeiling
    if (currentDepth < 2 || currentDepth > 8) {
        currentDepth = maxDepthCeiling;
    }

    // Fallback if no runtime memory metrics were provided (e.g. host unit tests / static evaluation)
    if (currentLargestBlock == 0 && currentFreeInternalHeap == 0) {
        if (reqs.needsTls) return 4;
        return maxDepthCeiling;
    }

    // --- MATHEMATICAL ADMISSION & DEPTH MAXIMIZER ---
    // Invariant: Permanent heap allocations are ALREADY deducted from currentFreeInternalHeap.
    // We only model the DELTA of DMA memory (+/- deltaMem) and the NEW TRANSIENT requirements
    // of the incoming engine.

    // 1. Operating system safety reserve (FreeRTOS kernel, ISR stacks, LwIP core, Wi-Fi driver)
    constexpr size_t SYSTEM_MIN_RESERVE = 12288; // 12 KB incompressible safety floor

    // 2. Incoming engine working memory
    size_t engineRam = (reqs.internalPersistentBytes > reqs.minFreeInternalHeapBytes)
        ? reqs.internalPersistentBytes : reqs.minFreeInternalHeapBytes;
    engineRam += reqs.shadowBytesPerFrame;
    if (hasPsram) {
        // On boards with external PSRAM, persistent engine working sets and shadow buffers
        // reside in SPIRAM, sparing internal SRAM.
        engineRam = 0;
    }

    // 3. Network & Cryptographic TLS admission reserve
    size_t netReserve = 0;
    size_t minContiguousNeeded = 0;
    size_t minDmaNeeded = 4096;

    if (reqs.needsTls) {
        // mbedTLS needs dual in/out record buffers (~33 KB) + context & RSA BIGNUM working limbs
        netReserve = ResourceReserve::TLS_SOCKET_ADMISSION_RESERVE; // 45,000 bytes
        minContiguousNeeded = NetworkBudget::TLS_MIN_COMBINED_BLOCK; // 28,672 bytes
        minDmaNeeded = NetworkBudget::TLS_MIN_FREE_DMA;              // 16,384 bytes
        if (reqs.internalContiguousBytes + 4096 > minContiguousNeeded) {
            minContiguousNeeded = reqs.internalContiguousBytes + 4096;
        }
    } else if (reqs.needsNetwork) {
        // Plain network (AsyncTCP socket RX/TX buffers)
        netReserve = ResourceReserve::ASYNC_TCP_ADMISSION_RESERVE; // 16,000 bytes
        minContiguousNeeded = reqs.internalContiguousBytes > 0 ? (reqs.internalContiguousBytes + 4096) : 10240;
    } else {
        // Pure graphics engine (Clock, Canvas, Tetris, etc.)
        netReserve = 0;
        minContiguousNeeded = reqs.internalContiguousBytes > 0 ? (reqs.internalContiguousBytes + 4096) : 8192;
    }

    // 4. Audio peripherals (I2S DMA ringbuffers)
    size_t audioReserve = (reqs.needsAudio || reqs.needsAudioInput || reqs.needsAudioOutput)
        ? ResourceReserve::AUDIO_DMA_RING_ADMISSION_RESERVE : 0;

    // Total new internal DRAM required to safely run the incoming engine
    size_t totalNewRamNeeded = SYSTEM_MIN_RESERVE + engineRam + netReserve + audioReserve;

    // Query PSRAM headroom if present (ESP32-S3)
    size_t freePsram = 0;
#if defined(ESP32)
    if (hasPsram) {
        freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }
#endif

    // Evaluate candidates from maxDepthCeiling (e.g. 8 bits) down to 2 bits
    for (int candidate = (int)maxDepthCeiling; candidate >= 2; --candidate) {
        int64_t deltaMem = ((int64_t)currentDepth - candidate) * (int64_t)bytesPerBit;
        int64_t estimatedFree = (int64_t)currentFreeInternalHeap + deltaMem;
        int64_t estimatedBlock = (int64_t)currentLargestBlock + deltaMem;
        int64_t estimatedDma = (int64_t)currentFreeDma + deltaMem;

        bool freeOk = (currentFreeInternalHeap == 0) || (estimatedFree >= (int64_t)totalNewRamNeeded);
        bool blockOk = (currentLargestBlock == 0) || (estimatedBlock >= (int64_t)minContiguousNeeded);
        bool dmaOk = (currentFreeDma == 0) || (estimatedDma >= (int64_t)minDmaNeeded);
        bool psramOk = (!hasPsram) || (freePsram == 0) || (freePsram >= (reqs.psramBytes + 65536));

        if (freeOk && blockOk && dmaOk && psramOk) {
            return (uint8_t)candidate;
        }
    }

    // Extreme memory pressure fallback floor: minimum 2 bits (maximum possible DRAM reclaimed)
    return 2;
}

