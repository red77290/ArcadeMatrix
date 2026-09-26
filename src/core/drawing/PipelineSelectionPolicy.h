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

struct PipelineSelectionResult {
    PipelineDescriptor descriptor;
    SurfaceSelectionReason reason = SurfaceSelectionReason::ExplicitUserPolicy;
    const char* reasonText = "";
    bool valid = true;
};

class PipelineSelectionPolicy {
public:
    /**
     * @brief Evaluates hardware capabilities, geometry constraints, and user preferences.
     * @param width Physical display width
     * @param height Physical display height
     * @param colorDepth HUB75 color depth (bits per channel)
     * @param requestedPipeline User-selected pipeline mode ("auto", "canvas_single", etc.)
     * @param legacyForceSingleBuffer Legacy fallback flag
     * @param hasPsram Whether PSRAM is physically present and enabled
     * @return PipelineSelectionResult Fully resolved pipeline specification and reasoning
     */
    static PipelineSelectionResult evaluate(
        uint16_t width,
        uint16_t height,
        uint8_t colorDepth,
        const String& requestedPipeline = "auto",
        bool legacyForceSingleBuffer = false,
        bool hasPsram = false
    );
};
