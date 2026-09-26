/**
 * @file DisplaySurfaceFactory.cpp
 * @brief Implementation of DisplaySurfaceFactory.
 */
#include "DisplaySurfaceFactory.h"
#include "../../hal/HardwareHAL.h"
#include "../../hal/BoardProfile.h"
#include "PipelineSelectionPolicy.h"
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
    uint8_t depth = 8;

    auto policyRes = PipelineSelectionPolicy::evaluate(
        width, height, depth, requestedPipeline, legacyForceSingleBuffer, hasPsram
    );

    result.reason = policyRes.reason;
    result.reasonText = policyRes.reasonText;

    if (!policyRes.valid) {
        result.surface.reset();
        return result;
    }

    IPresentationBackend* backend = matrixEngine ? matrixEngine->getPresentationBackend() : nullptr;
    const auto& desc = policyRes.descriptor;

    if (desc.strategy == PresentationStrategy::CANVAS_BURST_SINGLE ||
        desc.strategy == PresentationStrategy::CANVAS_BURST_DOUBLE)
    {
        bool singleDma = !desc.dmaDoubleBuffered;
        result.surface.reset(new CanvasBufferedSurface(width, height, desc.canvasStorage, backend, singleDma));
    } else {
        // Direct DMA surface
        MatrixPanel_I2S_DMA* disp = matrixEngine ? matrixEngine->getDisplay() : nullptr;
        bool singleDma = !desc.dmaDoubleBuffered;
        result.surface.reset(new DirectDmaSurface(disp, width, height, singleDma, matrixEngine));
    }

    // Safety validation: verify that canvas allocation succeeded for buffered surfaces
    if (result.surface && !result.surface->hasCanvas() &&
        (desc.strategy == PresentationStrategy::CANVAS_BURST_SINGLE || desc.strategy == PresentationStrategy::CANVAS_BURST_DOUBLE))
    {
        LOGE("DisplaySurfaceFactory", "Canvas buffer allocation failed for pipeline (%ux%u). Falling back to Direct DMA.",
             width, height);
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
