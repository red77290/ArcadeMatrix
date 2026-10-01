/**
 * @file PipelineSelectionPolicy.h
 * @brief Centralized authority governing rendering pipeline, color depth admission, and memory tier selection.
 */
#pragma once
#include <Arduino.h>
#include "PipelineDescriptor.h"
#include "DmaMemoryLayout.h"
#include "core/EngineContract.h"

constexpr uint8_t COLOR_DEPTH_AUTO = 0;
constexpr uint8_t COLOR_DEPTH_MIN  = 1;
constexpr uint8_t COLOR_DEPTH_MAX  = 8;

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
    uint8_t effectiveColorDepth = 8;
    SurfaceSelectionReason reason = SurfaceSelectionReason::ExplicitUserPolicy;
    const char* reasonText = "";
    bool valid = true;

    PipelineSelectionResult() = default;
};

class PipelineSelectionPolicy {
public:
    /**
     * @brief Evaluates hardware capabilities, geometry constraints, user preferences, memory budget, and requirements.
     * @param width Physical display width
     * @param height Physical display height
     * @param colorDepth HUB75 color depth (0 = Auto, 1..8 = manual)
     * @param requestedPipeline User-selected pipeline mode ("auto", "canvas_single", etc.)
     * @param hasPsram Whether PSRAM is physically present and enabled
     * @param memory Live memory constraints (or default unconstrained)
     * @param requirements Single engine or aggregated rotation requirements
     * @return PipelineSelectionResult Fully resolved pipeline specification, effective color depth, and reasoning
     */
    static PipelineSelectionResult evaluate(
        uint16_t width,
        uint16_t height,
        uint8_t colorDepth,
        const String& requestedPipeline = "auto",
        bool hasPsram = false,
        const MemoryBudgetConstraints& memory = MemoryBudgetConstraints(),
        const EngineRequirements& requirements = EngineRequirements()
    );

    /**
     * @brief Checks whether a given color depth and pipeline satisfy memory admission and contiguous constraints.
     */
    static bool pipelineFits(
        uint16_t width,
        uint16_t height,
        uint8_t depth,
        const PipelineDescriptor& desc,
        bool hasPsram,
        const MemoryBudgetConstraints& memory,
        const EngineRequirements& requirements
    );

    /**
     * @brief Convenience helper resolving effective HUB75 DMA color depth based on pipeline admission.
     */
    static uint8_t resolveEffectiveColorDepth(
        int configuredDepth,
        uint16_t width,
        uint16_t height,
        bool hasPsram,
        const MemoryBudgetConstraints& memory = MemoryBudgetConstraints(),
        const EngineRequirements& requirements = EngineRequirements()
    );

    /**
     * @brief Resolves target color depth for a specific engine transition under the Dynamic Presentation Pipeline.
     *
     * Automatically calculates the maximum possible color depth (up to configuredDepth ceiling,
     * or 8 in Auto mode) that guarantees sufficient contiguous internal DRAM for the incoming engine's
     * memory requirements (including TLS handshake buffers), scaling down to 2 bits if needed under memory pressure.
     * Exiting a TLS engine automatically restores user configured depth if memory permits.
     */
    static uint8_t resolveTargetDepth(
        uint8_t configuredDepth,
        bool dynamicColorDepth,
        uint16_t width,
        uint16_t height,
        bool hasPsram,
        const EngineRequirements& reqs,
        uint8_t currentDepth = 0,
        size_t currentLargestBlock = 0,
        size_t currentFreeInternalHeap = 0
    );
};
