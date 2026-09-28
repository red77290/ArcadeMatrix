/**
 * @file CompatibilityEvaluator.h
 * @brief Authoritative runtime evaluation engine determining display engine compatibility,
 *        degraded performance trade-offs, and admission control feasibility.
 */
#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include "../../include/core/EngineContract.h"
#include "drawing/IDrawingSurface.h"
#include "drawing/PipelineDescriptor.h"
#include "drawing/PipelineSelectionPolicy.h"
#include "drawing/PresentationTimingModel.h"
#include "drawing/IPresentationBackend.h"
#include "../hal/HardwareHAL.h"

/**
 * @enum CompatibilityStatus
 * @brief Discrete feasibility category of an engine for a specific hardware & panel state.
 */
enum class CompatibilityStatus : uint8_t {
    Compatible = 0,         ///< 🟢 Full framerate, optimal quality, tear-free presentation
    CompatibleDegraded = 1, ///< 🟡 Runnable with trade-offs (e.g. transient blanking, reduced FPS)
    Incompatible = 2        ///< 🔴 Cannot execute (hardware missing or memory budget blown)
};

/**
 * @enum CompatibilityReason
 * @brief Primary root-cause code explaining an incompatible or degraded verdict.
 */
enum class CompatibilityReason : uint8_t {
    None = 0,
    RequiresPsram,
    RequiresPsramDma,
    RequiresAudioInput,
    RequiresAudioOutput,
    RequiresTemperature,
    RequiresGyroscope,
    RequiresNetwork,
    RequiresSdCard,
    InsufficientInternalHeap,
    InsufficientLargestBlock,
    InsufficientPsram,
    InsufficientDmaMemory,
    RequiresDoubleBuffer,
    BlankBudgetExceeded,
    FrameBudgetExceeded,
    UnsupportedGeometry,
    UnsupportedColorDepth,
    NetworkReserveInsufficient
};

/**
 * @enum CompatibilityIssue
 * @brief Bitmask allowing multi-issue reporting for diagnostic tools and WebUI.
 */
enum class CompatibilityIssue : uint32_t {
    None                    = 0,
    MissingPsram            = 1 << 0,
    MissingPsramDma         = 1 << 1,
    MissingAudioInput       = 1 << 2,
    MissingAudioOutput      = 1 << 3,
    MissingTempSensor       = 1 << 4,
    MissingGyroscope        = 1 << 5,
    MissingNetwork          = 1 << 6,
    MissingSdCard           = 1 << 7,
    LowInternalHeap         = 1 << 8,
    FragmentedInternalHeap  = 1 << 9,
    LowPsram                = 1 << 10,
    LowDmaMemory            = 1 << 11,
    DoubleBufferUnavailable = 1 << 12,
    BlankBudgetExceeded     = 1 << 13,
    FrameBudgetExceeded     = 1 << 14,
    GeometryOutOfRange      = 1 << 15,
    ColorDepthUnsupported   = 1 << 16
};

inline CompatibilityIssue operator|(CompatibilityIssue a, CompatibilityIssue b) {
    return static_cast<CompatibilityIssue>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline CompatibilityIssue& operator|=(CompatibilityIssue& a, CompatibilityIssue b) {
    a = a | b;
    return a;
}

inline bool hasIssue(CompatibilityIssue mask, CompatibilityIssue check) {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(check)) != 0;
}

/**
 * @namespace ResourceReserve
 * @brief Conservative admission-control reserves preventing system starvation during engine runs.
 * Note: These are conservative safety margins used for admission prediction, not guaranteed object sizes.
 */
namespace ResourceReserve {
    /// Conservative admission-control reserve for one active TLS connection (lwIP TCP PCB + mbedTLS handshake & context).
    constexpr size_t TLS_SOCKET_ADMISSION_RESERVE     = 45000;

    /// Conservative admission-control reserve for AsyncTCP RX/TX connection buffers.
    constexpr size_t ASYNC_TCP_ADMISSION_RESERVE      = 16000;

    /// Conservative admission-control reserve for I2S audio circular ringbuffers and DMA descriptors.
    constexpr size_t AUDIO_DMA_RING_ADMISSION_RESERVE = 12000;

    /// Conservative admission-control reserve for FreeRTOS kernel, timer service, and task stacks.
    constexpr size_t SYSTEM_MIN_HEADROOM_RESERVE      = 35000;
}

/**
 * @enum EvaluationMode
 * @brief Evaluation policy separating reference/catalog qualification from dynamic runtime admission.
 */
enum class EvaluationMode : uint8_t {
    ReferenceCapability = 0, ///< Static/Catalogue qualification: "Does this hardware profile support this engine under reference budget?"
    RuntimeAdmission    = 1  ///< Dynamic pre-allocation validation: "Does current runtime resource state satisfy declared admission constraints?"
};

/**
 * @struct ReferenceMemoryProfile
 * @brief Measured baseline budget representing qualified memory available at idle reference baseline.
 */
struct ReferenceMemoryProfile {
    uint32_t freeInternalHeap;       ///< Measured free heap: BEFORE engine allocation, AFTER permanent firmware infrastructure, BEFORE transient engine resources, EXCLUDING safety reserve.
    uint32_t largestInternalBlock;   ///< Measured largest contiguous internal block at the reference idle baseline.
    uint32_t freePsram;              ///< Available external SPIRAM
    uint8_t  accountingVersion;      ///< Schema version tracking profile measurements
};

// Qualification baselines validated with tests/tools/matrix_generator.cpp
constexpr ReferenceMemoryProfile ESP32_STD_REFERENCE {
    .freeInternalHeap = 140000,
    .largestInternalBlock = 70000,
    .freePsram = 0,
    .accountingVersion = 1
};

constexpr ReferenceMemoryProfile WAVESHARE_S3_REFERENCE {
    .freeInternalHeap = 220000,
    .largestInternalBlock = 110000,
    .freePsram = 7500000,
    .accountingVersion = 1
};

/**
 * @struct CompatibilityContext
 * @brief Complete snapshot of hardware, memory, panel geometry, presentation policy, and evaluation mode.
 */
struct CompatibilityContext {
    HardwareCapabilities hardware;
    MemoryBudgetConstraints memory;
    uint16_t width = 128;
    uint16_t height = 32;
    uint8_t colorDepth = 8;
    String requestedPipeline = "auto";
    PresentationPolicy presentationPolicy;
    bool isConnectedWifi = true;
    EvaluationMode mode = EvaluationMode::ReferenceCapability;
};

/**
 * @struct CompatibilityVerdict
 * @brief Comprehensive feasibility decision returned by CompatibilityEvaluator.
 */
struct CompatibilityVerdict {
    CompatibilityStatus status = CompatibilityStatus::Compatible;
    CompatibilityReason primaryReason = CompatibilityReason::None;
    uint32_t issueFlags = 0;            ///< Bitmask of CompatibilityIssue
    const char* reasonText = "Compatible";
    PresentationStrategy strategy = PresentationStrategy::NONE;
    CanvasStorage storage = CanvasStorage::NONE;

    // Performance Modeling
    uint16_t targetFps = 60;
    uint16_t estimatedPresentationFps = 60;
    uint16_t validatedFps = 0;          ///< 0 if not hardware-validated
    bool empiricallyValidated = false;
    uint32_t estimatedBlankUs = 0;
    uint32_t estimatedFrameUs = 0;

    // Granular Diagnostic Telemetry
    size_t internalRequiredBytes = 0;
    size_t internalAvailableBytes = 0;
    size_t internalHeadroomBytes = 0;
    size_t largestRequiredBlockBytes = 0;
    size_t largestAvailableBlockBytes = 0;
    size_t psramRequiredBytes = 0;
    size_t psramAvailableBytes = 0;

    inline bool compatible() const { return status != CompatibilityStatus::Incompatible; }
    inline bool degraded() const { return status == CompatibilityStatus::CompatibleDegraded; }
};

/**
 * @struct PerformanceValidationEntry
 * @brief Empirical hardware validation record for a specific profile, geometry, and strategy.
 */
struct PerformanceValidationEntry {
    HwProfile profile;
    uint16_t width;
    uint16_t height;
    uint8_t colorDepth;
    PresentationStrategy strategy;
    uint16_t validatedFps;
};

/**
 * @class CompatibilityEvaluator
 * @brief Centralized authority evaluating engine compatibility against live hardware context.
 */
class CompatibilityEvaluator {
private:
    static std::atomic<uint32_t> s_hardwareCapabilityGeneration;

public:
    /**
     * @brief Gets the current hardware capability generation counter.
     */
    static uint32_t getHardwareCapabilityGeneration();

    /**
     * @brief Increments capability generation when relevant system context changes.
     */
    static void notifyHardwareCapabilityChanged();

    /**
     * @brief Gets the static reference memory profile for a given hardware profile.
     */
    static ReferenceMemoryProfile getReferenceMemoryProfile(HwProfile profile);

    /**
     * @brief Captures compatibility context from system hardware and configuration.
     * @param mode Evaluation mode (ReferenceCapability by default)
     */
    static CompatibilityContext buildCurrentContext(EvaluationMode mode = EvaluationMode::ReferenceCapability);

    /**
     * @brief Evaluates an engine descriptor against a concrete compatibility context.
     * @param desc Engine descriptor declaring metadata, capabilities, and requirements
     * @param ctx Live snapshot of hardware, memory, geometry, and presentation policies
     * @return CompatibilityVerdict Feasibility classification, resolved pipeline, and diagnostic metrics
     */
    static CompatibilityVerdict evaluate(
        const EngineDescriptor& desc,
        const CompatibilityContext& ctx
    );

    /**
     * @brief Translates discrete reason codes to human-readable English descriptions.
     */
    static const char* reasonToString(CompatibilityReason reason);

    /**
     * @brief Translates status enum to string ("compatible", "degraded", "incompatible").
     */
    static const char* statusToString(CompatibilityStatus status);

    /**
     * @brief Translates strategy enum to string ("direct_dma_double", "canvas_burst_single", etc.).
     */
    static const char* strategyToString(PresentationStrategy strategy);

    /**
     * @brief Translates canvas storage enum to string ("sram", "psram", "none").
     */
    static const char* storageToString(CanvasStorage storage);
};
