/**
 * @file PipelineSelectionPolicy.h
 * @brief Centralized authority governing rendering pipeline and memory tier selection.
 */
#pragma once
#include <Arduino.h>
#include "PipelineDescriptor.h"
#include "DmaMemoryLayout.h"

enum class SurfaceSelectionReason : uint8_t {
    AutoResolvedPsramCanvas,
    AutoResolvedSramCanvasLowDma,
    ExplicitUserPolicy,
    FallbackDirectDma,
    FallbackAllocationFailed,
    UnsupportedGeometry
};

struct MemoryBudgetConstraints {
    size_t freeInternalHeap = 0;       ///< Free internal DRAM (0 = unconstrained)
    size_t largestInternalBlock = 0;   ///< Largest contiguous free internal DRAM block
    size_t freeDmaHeap = 0;            ///< Free DMA-capable memory
    size_t freePsram = 0;              ///< Free external SPIRAM
    size_t largestPsramBlock = 0;      ///< Largest contiguous SPIRAM block

    MemoryBudgetConstraints() = default;
    MemoryBudgetConstraints(size_t fInt, size_t lInt, size_t fDma, size_t fPsram = 0, size_t lPsram = 0)
        : freeInternalHeap(fInt), largestInternalBlock(lInt), freeDmaHeap(fDma),
          freePsram(fPsram), largestPsramBlock(lPsram) {}
};

struct PipelineSelectionResult {
    PipelineDescriptor descriptor;
    SurfaceSelectionReason reason = SurfaceSelectionReason::ExplicitUserPolicy;
    const char* reasonText = "";
    bool valid = true;

    PipelineSelectionResult() = default;
};

class PipelineSelectionPolicy {
public:
    /**
     * @brief Evaluates hardware capabilities, geometry constraints, user preferences, and live memory.
     * @param width Physical display width
     * @param height Physical display height
     * @param colorDepth HUB75 color depth (bits per channel)
     * @param requestedPipeline User-selected pipeline mode ("auto", "canvas_single", etc.)
     * @param hasPsram Whether PSRAM is physically present and enabled
     * @param memory Live memory constraints (or default unconstrained)
     * @return PipelineSelectionResult Fully resolved pipeline specification and reasoning
     */
    static PipelineSelectionResult evaluate(
        uint16_t width,
        uint16_t height,
        uint8_t colorDepth,
        const String& requestedPipeline = "auto",
        bool hasPsram = false,
        const MemoryBudgetConstraints& memory = MemoryBudgetConstraints()
    );
};
