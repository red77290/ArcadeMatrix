/**
 * @file PipelineDescriptor.h
 * @brief Formal descriptor encapsulating rendering strategy and memory tiers for ArcadeMatrix v4.
 */
#pragma once
#include <Arduino.h>
#include "IDrawingSurface.h"

/**
 * @struct PipelineDescriptor
 * @brief Immutable specification of an active drawing and presentation pipeline.
 */
struct PipelineDescriptor {
    PresentationStrategy strategy = PresentationStrategy::CANVAS_BURST_SINGLE;
    CanvasStorage canvasStorage = CanvasStorage::SRAM;
    bool doubleBuffered = false;       ///< Logical surface double-buffering
    bool dmaDoubleBuffered = false;    ///< Hardware DMA double-buffering
    bool supportsPSRAMCanvas = false;  ///< Intermediate canvas may reside in external PSRAM
    bool requiresInternalDMA = true;   ///< DMA buffers must reside in internal SRAM
    size_t estimatedCanvasBytes = 0;   ///< Memory allocated for intermediate canvas
    size_t estimatedDmaBytes = 0;      ///< Memory allocated for HUB75 DMA buffers
};
