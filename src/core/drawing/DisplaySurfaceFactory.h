/**
 * @file DisplaySurfaceFactory.h
 * @brief Abstract Factory creating concrete IDrawingSurface instances based on hardware profile and policy.
 */
#pragma once

#include "IDrawingSurface.h"
#include "DirectDmaSurface.h"
#include "CanvasBufferedSurface.h"
#include <memory>

class MatrixEngine;

enum class SurfaceSelectionReason : uint8_t {
    AutoResolvedPsramCanvas,
    AutoResolvedSramCanvasLowDma,
    ExplicitUserPolicy,
    FallbackDirectDma,
    FallbackAllocationFailed
};

struct SurfaceCreationResult {
    std::unique_ptr<IDrawingSurface> surface;
    SurfaceSelectionReason reason;
    const char* reasonText = "";
};

class DisplaySurfaceFactory {
public:
    static SurfaceCreationResult createSurface(
        MatrixEngine* matrixEngine,
        uint16_t width,
        uint16_t height,
        const String& requestedPipeline = "auto",
        bool legacyForceSingleBuffer = false
    );
};
