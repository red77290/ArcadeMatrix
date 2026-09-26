/**
 * @file PipelineSelectionPolicy.cpp
 * @brief Implementation of PipelineSelectionPolicy.
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
    bool hasPsram)
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

    if (pipeline == "canvas_single") {
        res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
        res.descriptor.canvasStorage = hasPsram ? CanvasStorage::PSRAM : CanvasStorage::SRAM;
        res.descriptor.doubleBuffered = false;
        res.descriptor.dmaDoubleBuffered = false;
        res.descriptor.supportsPSRAMCanvas = hasPsram;
        res.descriptor.requiresInternalDMA = !hasPsram;
        res.descriptor.estimatedCanvasBytes = canvasBytes;
        res.descriptor.estimatedDmaBytes = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, false);
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
        res.descriptor.estimatedDmaBytes = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, true);
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
        res.descriptor.estimatedDmaBytes = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, true);
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
        res.descriptor.estimatedDmaBytes = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, false);
        res.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        res.reasonText = "User requested Direct DMA Single Buffer (Ultra-low RAM)";
    } else if (hasPsram) {
        // Auto resolution: PSRAM available -> PSRAM Canvas + Double DMA
        res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_DOUBLE;
        res.descriptor.canvasStorage = CanvasStorage::PSRAM;
        res.descriptor.doubleBuffered = true;
        res.descriptor.dmaDoubleBuffered = true;
        res.descriptor.supportsPSRAMCanvas = true;
        res.descriptor.requiresInternalDMA = false;
        res.descriptor.estimatedCanvasBytes = canvasBytes;
        res.descriptor.estimatedDmaBytes = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, true);
        res.reason = SurfaceSelectionReason::AutoResolvedPsramCanvas;
        res.reasonText = "Auto-selected Canvas PSRAM + Double DMA (PSRAM available)";
    } else {
        // Auto resolution: Classic ESP32 without PSRAM -> SRAM Canvas + Single DMA
        res.descriptor.strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
        res.descriptor.canvasStorage = CanvasStorage::SRAM;
        res.descriptor.doubleBuffered = false;
        res.descriptor.dmaDoubleBuffered = false;
        res.descriptor.supportsPSRAMCanvas = false;
        res.descriptor.requiresInternalDMA = true;
        res.descriptor.estimatedCanvasBytes = canvasBytes;
        res.descriptor.estimatedDmaBytes = DmaMemoryLayout::calculateTotalBytes(width, height, colorDepth, false);
        res.reason = SurfaceSelectionReason::AutoResolvedSramCanvasLowDma;
        res.reasonText = "Auto-selected Canvas SRAM + Single DMA (Halves DMA RAM on classic ESP32)";
    }

    res.valid = true;
    return res;
}
