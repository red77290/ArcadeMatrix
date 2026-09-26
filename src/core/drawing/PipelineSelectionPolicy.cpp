/**
 * @file PipelineSelectionPolicy.cpp
 * @brief Implementation of PipelineSelectionPolicy with live memory awareness.
 */
#include "PipelineSelectionPolicy.h"
#include "../../hal/BoardProfile.h"
#include "../Logger.h"

PipelineSelectionResult PipelineSelectionPolicy::evaluate(
    uint16_t width,
    uint16_t height,
    uint8_t colorDepth,
    const String& requestedPipeline,
    bool legacyForceSingleBuffer,
    bool hasPsram,
    const MemoryBudgetConstraints& memory)
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

    String pipeline = requestedPipeline;
    pipeline.toLowerCase();

    // Map legacy forceSingleBuffer if auto or empty
    if (pipeline == "auto" || pipeline.isEmpty()) {
        if (legacyForceSingleBuffer) {
            pipeline = "canvas_single";
        }
    }

    size_t canvasBytes = (size_t)width * height * sizeof(uint16_t);
    size_t dmaBytesSingle = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, false);
    size_t dmaBytesDouble = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, true);

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
        // Check if PSRAM can fit the canvas without fragmentation failure
        bool psramCanFitCanvas = (memory.largestPsramBlock == 0 || memory.largestPsramBlock >= canvasBytes);
        bool dmaCanFitDouble = (memory.freeDmaHeap == 0 || memory.freeDmaHeap >= dmaBytesDouble);

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
            // PSRAM fits canvas, but DMA heap is tight: save half DMA RAM by using Single DMA
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
            // PSRAM cannot fit canvas: check if SRAM can fit
            bool sramCanFitCanvas = (memory.largestInternalBlock == 0 || memory.largestInternalBlock >= canvasBytes) &&
                                    (memory.freeInternalHeap == 0 || memory.freeInternalHeap >= (canvasBytes + 45000));
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
                // Cannot fit canvas in either PSRAM or SRAM: fall back to Direct DMA Single
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
        bool sramCanFit = (memory.largestInternalBlock == 0 || memory.largestInternalBlock >= canvasBytes) &&
                          (memory.freeInternalHeap == 0 || memory.freeInternalHeap >= (canvasBytes + 45000));
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
            // Cannot fit canvas intermediate buffer: fallback to Direct DMA Single
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

    res.valid = true;
    return res;
}
