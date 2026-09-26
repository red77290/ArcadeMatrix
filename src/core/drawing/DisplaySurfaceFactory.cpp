/**
 * @file DisplaySurfaceFactory.cpp
 * @brief Implementation of DisplaySurfaceFactory with live resource awareness.
 */
#include "DisplaySurfaceFactory.h"
#include "../../hal/HardwareHAL.h"
#include "../../hal/BoardProfile.h"
#include "PipelineSelectionPolicy.h"
#include "../MatrixEngine.h"
#include "../Logger.h"

#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

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

    MemoryBudgetConstraints mem;
#if defined(ESP32)
    mem.freeInternalHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    mem.largestInternalBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    mem.freeDmaHeap = heap_caps_get_free_size(MALLOC_CAP_DMA);
    if (hasPsram) {
        mem.freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        mem.largestPsramBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
#endif

    auto policyRes = PipelineSelectionPolicy::evaluate(
        width, height, depth, requestedPipeline, legacyForceSingleBuffer, hasPsram, mem
    );

    result.reason = policyRes.reason;
    result.reasonText = policyRes.reasonText;

    if (!policyRes.valid) {
        result.surface.reset();
        return result;
    }

    IPresentationBackend* backend = matrixEngine ? matrixEngine->getPresentationBackend() : nullptr;
    const auto& desc = policyRes.descriptor;

    result.requestedStrategy = desc.strategy;
    result.requestedCanvasStorage = desc.canvasStorage;

    if (desc.strategy == PresentationStrategy::CANVAS_BURST_SINGLE ||
        desc.strategy == PresentationStrategy::CANVAS_BURST_DOUBLE)
    {
        bool singleDma = !desc.dmaDoubleBuffered;
        result.surface.reset(new CanvasBufferedSurface(width, height, desc.canvasStorage, backend, singleDma));

        // Detect if CanvasBufferedSurface had to fall back internally (e.g. PSRAM -> SRAM)
        if (result.surface && result.surface->canvasStorage() != desc.canvasStorage) {
            LOGW("DisplaySurfaceFactory", "Requested canvas storage was downgraded to %s",
                 result.surface->canvasStorage() == CanvasStorage::SRAM ? "SRAM" : "NONE");
            result.reason = SurfaceSelectionReason::FallbackDirectDma;
            result.reasonText = "Downgraded intermediate canvas storage due to allocation constraints";
            result.fallbackReason = "Intermediate canvas storage downgraded";
        }
    } else {
        // Direct DMA surface (legacy fallback)
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
        result.fallbackReason = "Canvas buffer allocation failed, fell back to Direct DMA single";
    }

    if (!result.surface) {
        result.reason = SurfaceSelectionReason::FallbackAllocationFailed;
        result.reasonText = "CRITICAL: Surface allocation failed completely";
        result.fallbackReason = "Surface allocation failed completely";
        result.actualStrategy = PresentationStrategy::NONE;
        result.actualCanvasStorage = CanvasStorage::NONE;
    } else {
        result.actualStrategy = result.surface->presentationStrategy();
        result.actualCanvasStorage = result.surface->canvasStorage();
    }

    return result;
}
