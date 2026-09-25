/**
 * @file DisplaySurfaceFactory.cpp
 * @brief Implementation of DisplaySurfaceFactory.
 */
#include "DisplaySurfaceFactory.h"
#include "../../hal/HardwareHAL.h"
#include "../MatrixEngine.h"
#include "../Logger.h"

SurfaceCreationResult DisplaySurfaceFactory::createSurface(
    MatrixEngine* matrixEngine,
    uint16_t width,
    uint16_t height,
    const String& requestedPipeline,
    bool legacyForceSingleBuffer)
{
    SurfaceCreationResult result;
    bool hasPsram = hardwareHAL.capabilities().hasPsram;

    String pipeline = requestedPipeline;
    pipeline.toLowerCase();

    // Map legacy force_single_buffer to canvas_single if pipeline is auto or unset
    if (pipeline == "auto" || pipeline.isEmpty()) {
        if (legacyForceSingleBuffer) {
            pipeline = "canvas_single";
        }
    }

    if (pipeline == "canvas_single") {
        CanvasStorage storage = hasPsram ? CanvasStorage::PSRAM : CanvasStorage::SRAM;
        result.surface.reset(new CanvasBufferedSurface(width, height, storage, matrixEngine, true));
        result.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        result.reasonText = "User requested Canvas Buffered + Single DMA";
        return result;
    }

    if (pipeline == "canvas_double") {
        CanvasStorage storage = hasPsram ? CanvasStorage::PSRAM : CanvasStorage::SRAM;
        result.surface.reset(new CanvasBufferedSurface(width, height, storage, matrixEngine, false));
        result.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        result.reasonText = "User requested Canvas Buffered + Double DMA";
        return result;
    }

    if (pipeline == "direct_double") {
        result.surface.reset(new DirectDmaSurface(matrixEngine->getDisplay(), width, height, false));
        result.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        result.reasonText = "User requested Direct DMA Double Buffer (Legacy)";
        return result;
    }

    if (pipeline == "direct_single") {
        result.surface.reset(new DirectDmaSurface(matrixEngine->getDisplay(), width, height, true));
        result.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        result.reasonText = "User requested Direct DMA Single Buffer (Ultra-low RAM)";
        return result;
    }

    // Auto resolution based on Hardware Profile & Memory Tier
    if (hasPsram) {
        // ESP32-S3 or boards with PSRAM: use PSRAM Canvas + Double DMA for maximum throughput
        result.surface.reset(new CanvasBufferedSurface(width, height, CanvasStorage::PSRAM, matrixEngine, false));
        result.reason = SurfaceSelectionReason::AutoResolvedPsramCanvas;
        result.reasonText = "Auto-selected Canvas PSRAM + Double DMA (PSRAM available)";
        return result;
    }

    // Classic ESP32 without PSRAM:
    // If screen size is 64x32 or 64x64, Canvas SRAM (4KB / 8KB) + Single DMA frees ~16-20KB of DMA RAM!
    result.surface.reset(new CanvasBufferedSurface(width, height, CanvasStorage::SRAM, matrixEngine, true));
    result.reason = SurfaceSelectionReason::AutoResolvedSramCanvasLowDma;
    result.reasonText = "Auto-selected Canvas SRAM + Single DMA (Halves DMA RAM on classic ESP32)";
    return result;
}
