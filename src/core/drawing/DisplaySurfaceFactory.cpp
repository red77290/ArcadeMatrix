/**
 * @file DisplaySurfaceFactory.cpp
 * @brief Implementation of DisplaySurfaceFactory.
 */
#include "DisplaySurfaceFactory.h"
#include "../../hal/HardwareHAL.h"
#include "../../hal/BoardProfile.h"
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

    // Validate requested dimensions against board profile limits
    const auto& dispCaps = BoardProfile::current().display();
    if (width > dispCaps.maxWidth || height > dispCaps.maxHeight) {
        LOGW("DisplaySurfaceFactory", "Requested geometry (%ux%u) exceeds profile limits (%ux%u)",
             width, height, dispCaps.maxWidth, dispCaps.maxHeight);
    }

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
    } else if (pipeline == "canvas_double") {
        CanvasStorage storage = hasPsram ? CanvasStorage::PSRAM : CanvasStorage::SRAM;
        result.surface.reset(new CanvasBufferedSurface(width, height, storage, matrixEngine, false));
        result.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        result.reasonText = "User requested Canvas Buffered + Double DMA";
    } else if (pipeline == "direct_double") {
        MatrixPanel_I2S_DMA* disp = matrixEngine ? matrixEngine->getDisplay() : nullptr;
        result.surface.reset(new DirectDmaSurface(disp, width, height, false, matrixEngine));
        result.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        result.reasonText = "User requested Direct DMA Double Buffer (Legacy)";
    } else if (pipeline == "direct_single") {
        MatrixPanel_I2S_DMA* disp = matrixEngine ? matrixEngine->getDisplay() : nullptr;
        result.surface.reset(new DirectDmaSurface(disp, width, height, true, matrixEngine));
        result.reason = SurfaceSelectionReason::ExplicitUserPolicy;
        result.reasonText = "User requested Direct DMA Single Buffer (Ultra-low RAM)";
    } else if (hasPsram) {
        // Auto resolution based on Hardware Profile & Memory Tier
        // ESP32-S3 or boards with PSRAM: use PSRAM Canvas + Double DMA for maximum throughput
        result.surface.reset(new CanvasBufferedSurface(width, height, CanvasStorage::PSRAM, matrixEngine, false));
        result.reason = SurfaceSelectionReason::AutoResolvedPsramCanvas;
        result.reasonText = "Auto-selected Canvas PSRAM + Double DMA (PSRAM available)";
    } else {
        // Classic ESP32 without PSRAM:
        // If screen size is 64x32 or 64x64, Canvas SRAM (4KB / 8KB) + Single DMA frees ~16-20KB of DMA RAM!
        result.surface.reset(new CanvasBufferedSurface(width, height, CanvasStorage::SRAM, matrixEngine, true));
        result.reason = SurfaceSelectionReason::AutoResolvedSramCanvasLowDma;
        result.reasonText = "Auto-selected Canvas SRAM + Single DMA (Halves DMA RAM on classic ESP32)";
    }

    // Safety validation: verify that canvas allocation succeeded for buffered surfaces
    if (result.surface && !result.surface->hasCanvas() && (pipeline.startsWith("canvas") || pipeline == "auto" || pipeline.isEmpty())) {
        LOGE("DisplaySurfaceFactory", "Canvas buffer allocation failed for pipeline %s (%ux%u). Falling back to Direct DMA.",
             pipeline.c_str(), width, height);
        MatrixPanel_I2S_DMA* disp = matrixEngine ? matrixEngine->getDisplay() : nullptr;
        result.surface.reset(new DirectDmaSurface(disp, width, height, true, matrixEngine));
        result.reason = SurfaceSelectionReason::FallbackDirectDma;
        result.reasonText = "Fallback to Direct DMA due to canvas allocation failure";
    }

    if (!result.surface) {
        result.reason = SurfaceSelectionReason::FallbackAllocationFailed;
        result.reasonText = "CRITICAL: Surface allocation failed completely";
    }

    return result;
}
