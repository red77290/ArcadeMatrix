#include "CompatibilityEvaluator.h"
#include "drawing/DmaMemoryLayout.h"
#include "ConfigLoader.h"
#include <atomic>

#if defined(ESP32)
#include <WiFi.h>
#include <esp_heap_caps.h>
#endif

std::atomic<uint32_t> CompatibilityEvaluator::s_hardwareCapabilityGeneration{1};

uint32_t CompatibilityEvaluator::getHardwareCapabilityGeneration() {
    return s_hardwareCapabilityGeneration.load(std::memory_order_relaxed);
}

void CompatibilityEvaluator::notifyHardwareCapabilityChanged() {
    s_hardwareCapabilityGeneration.fetch_add(1, std::memory_order_relaxed);
}

ReferenceMemoryProfile CompatibilityEvaluator::getReferenceMemoryProfile(HwProfile profile) {
    if (profile == HwProfile::WAVESHARE_S3) {
        return WAVESHARE_S3_REFERENCE;
    }
    return ESP32_STD_REFERENCE;
}

CompatibilityContext CompatibilityEvaluator::buildCurrentContext(EvaluationMode mode) {
    CompatibilityContext ctx;
    ctx.mode = mode;
    ctx.hardware = hardwareHAL.capabilities();

    if (mode == EvaluationMode::ReferenceCapability) {
        ReferenceMemoryProfile ref = getReferenceMemoryProfile(ctx.hardware.profile);
        ctx.memory.freeInternalHeap = ref.freeInternalHeap;
        ctx.memory.largestInternalBlock = ref.largestInternalBlock;
        ctx.memory.freePsram = ref.freePsram;
        ctx.memory.freeDmaHeap = (ctx.hardware.profile == HwProfile::WAVESHARE_S3) ? 80000 : 40000;
#if defined(ESP32)
        ctx.isConnectedWifi = (WiFi.status() == WL_CONNECTED) || (WiFi.getMode() == WIFI_MODE_AP) || ctx.hardware.hasNetwork;
#else
        ctx.isConnectedWifi = true;
#endif
    } else {
#if defined(ESP32)
        ctx.memory.freeInternalHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ctx.memory.largestInternalBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ctx.memory.freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ctx.memory.freeDmaHeap = heap_caps_get_free_size(MALLOC_CAP_DMA);
        ctx.isConnectedWifi = (WiFi.status() == WL_CONNECTED) || (WiFi.getMode() == WIFI_MODE_AP) || ctx.hardware.hasNetwork;
#else
        ctx.memory.freeInternalHeap = 200000;
        ctx.memory.largestInternalBlock = 90000;
        ctx.memory.freePsram = 0;
        ctx.memory.freeDmaHeap = 40000;
        ctx.isConnectedWifi = true;
#endif
    }

    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const auto& snap = guard.get();
    ctx.width = snap.matrix.width > 0 ? snap.matrix.width : 128;
    ctx.height = snap.matrix.height > 0 ? snap.matrix.height : 32;
    ctx.colorDepth = (snap.matrix.colorDepth >= 0 && snap.matrix.colorDepth <= 8) ? snap.matrix.colorDepth : 0;
    ctx.requestedPipeline = snap.matrix.render_pipeline.isEmpty() ? "auto" : snap.matrix.render_pipeline;
    ctx.presentationPolicy.allowBlanking = false;
    ctx.presentationPolicy.degradedBlankingPermitted = false;

    return ctx;
}

CompatibilityVerdict CompatibilityEvaluator::evaluate(
    const EngineDescriptor& desc,
    const CompatibilityContext& ctx)
{
    CompatibilityVerdict verdict;
    const auto& req = desc.requirements;

    verdict.status = CompatibilityStatus::Compatible;
    verdict.primaryReason = CompatibilityReason::None;
    verdict.issueFlags = 0;
    verdict.targetFps = req.targetFps > 0 ? req.targetFps : 60;
    verdict.empiricallyValidated = false;
    verdict.validatedFps = 0;

    // =========================================================================
    // 1. Hardware Peripheral Hard-Constraint Gating
    // =========================================================================
    if (req.needsPsram && !ctx.hardware.hasPsram) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingPsram);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresPsram;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    if (req.needsPsramDma && ctx.hardware.profile != HwProfile::WAVESHARE_S3) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingPsramDma);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresPsramDma;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    if ((req.needsAudioInput || req.needsAudio) && !ctx.hardware.hasMicrophone) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingAudioInput);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresAudioInput;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    if (req.needsAudioOutput && !ctx.hardware.audio.output) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingAudioOutput);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresAudioOutput;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    if (req.needsTempSensor && !ctx.hardware.hasTempSensor) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingTempSensor);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresTemperature;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    if (req.needsGyroscope && !ctx.hardware.hasGyroscope) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingGyroscope);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresGyroscope;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    if (req.needsNetwork && !ctx.isConnectedWifi) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingNetwork);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresNetwork;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    if (req.needsTls && !ctx.isConnectedWifi) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingNetwork);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresNetwork;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    if (req.needsSd && !ctx.hardware.hasSd) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::MissingSdCard);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::RequiresSdCard;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    // Geometry boundaries
    if ((req.minWidth > 0 && ctx.width < req.minWidth) ||
        (req.minHeight > 0 && ctx.height < req.minHeight) ||
        (req.maxWidth > 0 && ctx.width > req.maxWidth) ||
        (req.maxHeight > 0 && ctx.height > req.maxHeight))
    {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::GeometryOutOfRange);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::UnsupportedGeometry;
            verdict.status = CompatibilityStatus::Incompatible;
        }
    }

    // =========================================================================
    // 2. Pipeline Selection Evaluation (Reusing V4 PipelineSelectionPolicy)
    // =========================================================================
    auto policyRes = PipelineSelectionPolicy::evaluate(
        ctx.width,
        ctx.height,
        ctx.colorDepth,
        ctx.requestedPipeline,
        ctx.hardware.hasPsram,
        ctx.memory,
        desc.requirements
    );

    if (!policyRes.valid) {
        verdict.status = CompatibilityStatus::Incompatible;
        verdict.strategy = PresentationStrategy::NONE;
        verdict.storage = CanvasStorage::NONE;
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::UnsupportedGeometry;
        }
    } else {
        verdict.strategy = policyRes.descriptor.strategy;
        verdict.storage = policyRes.descriptor.canvasStorage;

        bool isDouble = (verdict.strategy == PresentationStrategy::CANVAS_BURST_DOUBLE ||
                         verdict.strategy == PresentationStrategy::DIRECT_DMA_DOUBLE);

        if (req.requiresDoubleBuffer && !isDouble) {
            verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::DoubleBufferUnavailable);
            if (verdict.primaryReason == CompatibilityReason::None) {
                verdict.primaryReason = CompatibilityReason::RequiresDoubleBuffer;
                verdict.status = CompatibilityStatus::Incompatible;
            }
        }

        if (!req.supportsSingleBuffer && !isDouble) {
            verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::DoubleBufferUnavailable);
            if (verdict.primaryReason == CompatibilityReason::None) {
                verdict.primaryReason = CompatibilityReason::RequiresDoubleBuffer;
                verdict.status = CompatibilityStatus::Incompatible;
            }
        }

        if (req.prefersDoubleBuffer && !isDouble && verdict.status == CompatibilityStatus::Compatible) {
            verdict.status = CompatibilityStatus::CompatibleDegraded;
            verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::DoubleBufferUnavailable);
            if (verdict.primaryReason == CompatibilityReason::None) {
                verdict.primaryReason = CompatibilityReason::RequiresDoubleBuffer;
            }
        }
    }

    // =========================================================================
    // 3. Presentation Blanking Budget Evaluation (Reusing V4 PresentationTimingModel)
    // =========================================================================
    bool isSingle = (verdict.strategy == PresentationStrategy::CANVAS_BURST_SINGLE ||
                     verdict.strategy == PresentationStrategy::DIRECT_DMA_SINGLE);
    bool isSpiram = (ctx.hardware.profile == HwProfile::WAVESHARE_S3 && ctx.hardware.hasPsram);

    uint32_t estimatedTransferUs = PresentationTimingModel::estimateTransferUs(
        ctx.width, ctx.height, policyRes.effectiveColorDepth, isSingle, isSpiram
    );
    verdict.estimatedBlankUs = estimatedTransferUs;

    if (verdict.strategy == PresentationStrategy::CANVAS_BURST_SINGLE &&
        ctx.presentationPolicy.allowBlanking &&
        ctx.presentationPolicy.maxBlankUs > 0 &&
        estimatedTransferUs > ctx.presentationPolicy.maxBlankUs)
    {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::BlankBudgetExceeded);
        if (!ctx.presentationPolicy.degradedBlankingPermitted) {
            if (verdict.primaryReason == CompatibilityReason::None || verdict.status == CompatibilityStatus::CompatibleDegraded) {
                verdict.primaryReason = CompatibilityReason::BlankBudgetExceeded;
            }
            verdict.status = CompatibilityStatus::Incompatible;
        } else {
            if (verdict.status == CompatibilityStatus::Compatible) {
                verdict.status = CompatibilityStatus::CompatibleDegraded;
                if (verdict.primaryReason == CompatibilityReason::None) {
                    verdict.primaryReason = CompatibilityReason::BlankBudgetExceeded;
                }
            }
        }
    }

    // =========================================================================
    // 4. Memory Tier & Contiguous Block Verification
    //
    // Note: The display infrastructure (HUB75 DMA buffer and shared Canvas) is
    // allocated once at boot/runtime startup. Free internal heap reported in
    // ctx.memory already reflects active display allocation. Engine admission
    // checks the engine's persistent footprint, engine-specific shadow memory,
    // runtime communication reserves (TLS, AsyncTCP, Audio), and system headroom.
    // =========================================================================
    size_t networkReserve = 0;
    if (req.needsTls) {
        networkReserve += ResourceReserve::TLS_SOCKET_ADMISSION_RESERVE;
    }
    if (req.needsNetwork) {
        networkReserve += ResourceReserve::ASYNC_TCP_ADMISSION_RESERVE;
    }

    size_t audioReserve = 0;
    if (req.needsAudioInput || req.needsAudio) {
        audioReserve += ResourceReserve::AUDIO_DMA_RING_ADMISSION_RESERVE;
    }

    size_t persistentInternal = req.internalPersistentBytes;
    size_t shadowBytes = req.shadowBytesPerFrame;
    size_t totalInternalReq = persistentInternal + shadowBytes + networkReserve + audioReserve +
                              ResourceReserve::SYSTEM_MIN_HEADROOM_RESERVE;

    verdict.internalRequiredBytes = totalInternalReq;
    verdict.internalAvailableBytes = ctx.memory.freeInternalHeap;
    verdict.internalHeadroomBytes = (ctx.memory.freeInternalHeap > totalInternalReq) ?
        (ctx.memory.freeInternalHeap - totalInternalReq) : 0;

    // Largest contiguous block needed: engine contiguous buffer, engine shadow canvas, or TLS buffer chunk (~25KB)
    size_t minTlsContiguous = req.needsTls ? 25000 : 0;
    size_t contiguousReq = (std::max)(shadowBytes, (std::max)(static_cast<size_t>(req.internalContiguousBytes), minTlsContiguous));
    verdict.largestRequiredBlockBytes = contiguousReq;
    verdict.largestAvailableBlockBytes = ctx.memory.largestInternalBlock;

    size_t psramReq = req.psramBytes;
    verdict.psramRequiredBytes = psramReq;
    verdict.psramAvailableBytes = ctx.memory.freePsram;

    // Check Total Free DRAM
    if (ctx.memory.freeInternalHeap > 0 && totalInternalReq > ctx.memory.freeInternalHeap) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::LowInternalHeap);
        if (verdict.primaryReason == CompatibilityReason::None || verdict.status == CompatibilityStatus::CompatibleDegraded) {
            verdict.primaryReason = CompatibilityReason::InsufficientInternalHeap;
        }
        verdict.status = CompatibilityStatus::Incompatible;
    }

    // Check Contiguous Free DRAM Block
    if (ctx.memory.largestInternalBlock > 0 && contiguousReq > ctx.memory.largestInternalBlock) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::FragmentedInternalHeap);
        if (verdict.primaryReason == CompatibilityReason::None || verdict.status == CompatibilityStatus::CompatibleDegraded) {
            verdict.primaryReason = CompatibilityReason::InsufficientLargestBlock;
        }
        verdict.status = CompatibilityStatus::Incompatible;
    }

    // Check External PSRAM
    if (psramReq > 0 && ctx.memory.freePsram > 0 && psramReq > ctx.memory.freePsram) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::LowPsram);
        if (verdict.primaryReason == CompatibilityReason::None || verdict.status == CompatibilityStatus::CompatibleDegraded) {
            verdict.primaryReason = CompatibilityReason::InsufficientPsram;
        }
        verdict.status = CompatibilityStatus::Incompatible;
    }

    // Check Pipeline & Color Depth Memory Admission
    if (!PipelineSelectionPolicy::pipelineFits(ctx.width, ctx.height, policyRes.effectiveColorDepth, policyRes.descriptor, ctx.hardware.hasPsram, ctx.memory, req)) {
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::FragmentedInternalHeap);
        if (verdict.primaryReason == CompatibilityReason::None || verdict.status == CompatibilityStatus::CompatibleDegraded) {
            verdict.primaryReason = CompatibilityReason::InsufficientLargestBlock;
        }
        verdict.status = CompatibilityStatus::Incompatible;
    }

    // =========================================================================
    // 5. Performance Modeling
    // =========================================================================
    uint32_t encodeEstUs = (verdict.storage != CanvasStorage::NONE) ?
        ((static_cast<uint32_t>(ctx.width) * ctx.height * policyRes.effectiveColorDepth) / 12) : 0;
    verdict.estimatedFrameUs = estimatedTransferUs + encodeEstUs;

    uint32_t fpsFromPresentation = (verdict.estimatedFrameUs > 0) ?
        (1000000 / verdict.estimatedFrameUs) : 60;
    verdict.estimatedPresentationFps = static_cast<uint16_t>(
        (std::min)(static_cast<uint32_t>(verdict.targetFps), fpsFromPresentation)
    );

    if (verdict.estimatedPresentationFps < (verdict.targetFps / 2) && verdict.status == CompatibilityStatus::Compatible) {
        verdict.status = CompatibilityStatus::CompatibleDegraded;
        verdict.issueFlags |= static_cast<uint32_t>(CompatibilityIssue::FrameBudgetExceeded);
        if (verdict.primaryReason == CompatibilityReason::None) {
            verdict.primaryReason = CompatibilityReason::FrameBudgetExceeded;
        }
    }

    // Empirical hardware validation table lookup
    static const PerformanceValidationEntry s_validationTable[] = {
        { HwProfile::WAVESHARE_S3, 128, 32, 8, PresentationStrategy::DIRECT_DMA_DOUBLE, 60 },
        { HwProfile::WAVESHARE_S3, 128, 32, 8, PresentationStrategy::CANVAS_BURST_DOUBLE, 60 },
        { HwProfile::ESP32_STD,     64, 32, 8, PresentationStrategy::DIRECT_DMA_DOUBLE, 60 },
        { HwProfile::ESP32_STD,    128, 32, 8, PresentationStrategy::CANVAS_BURST_SINGLE, 30 },
    };

    for (const auto& entry : s_validationTable) {
        if (entry.profile == ctx.hardware.profile &&
            entry.width == ctx.width &&
            entry.height == ctx.height &&
            entry.colorDepth == ctx.colorDepth &&
            entry.strategy == verdict.strategy)
        {
            verdict.empiricallyValidated = true;
            verdict.validatedFps = entry.validatedFps;
            break;
        }
    }

    verdict.reasonText = reasonToString(verdict.primaryReason);
    return verdict;
}

const char* CompatibilityEvaluator::reasonToString(CompatibilityReason reason) {
    switch (reason) {
        case CompatibilityReason::None:
            return "Fully compatible";
        case CompatibilityReason::RequiresPsram:
            return "Requires external PSRAM memory";
        case CompatibilityReason::RequiresPsramDma:
            return "Requires PSRAM with direct DMA capability";
        case CompatibilityReason::RequiresAudioInput:
            return "Requires I2S microphone hardware";
        case CompatibilityReason::RequiresAudioOutput:
            return "Requires I2S audio speaker/DAC output";
        case CompatibilityReason::RequiresTemperature:
            return "Requires SHTC3 temperature sensor";
        case CompatibilityReason::RequiresGyroscope:
            return "Requires QMI8658 gyroscope/IMU sensor";
        case CompatibilityReason::RequiresNetwork:
            return "Requires active Wi-Fi connection";
        case CompatibilityReason::RequiresSdCard:
            return "Requires MicroSD card storage";
        case CompatibilityReason::InsufficientInternalHeap:
            return "Insufficient internal DRAM headroom";
        case CompatibilityReason::InsufficientLargestBlock:
            return "Internal memory fragmented (largest block too small)";
        case CompatibilityReason::InsufficientPsram:
            return "Insufficient free PSRAM memory";
        case CompatibilityReason::InsufficientDmaMemory:
            return "Insufficient DMA-capable internal memory";
        case CompatibilityReason::RequiresDoubleBuffer:
            return "Engine requires double buffering (tear-free)";
        case CompatibilityReason::BlankBudgetExceeded:
            return "Single-buffer blanking budget exceeded (>400µs)";
        case CompatibilityReason::FrameBudgetExceeded:
            return "Frame period budget exceeded (>16.6ms)";
        case CompatibilityReason::UnsupportedGeometry:
            return "Matrix panel dimensions unsupported";
        case CompatibilityReason::UnsupportedColorDepth:
            return "Color depth unsupported by configuration";
        case CompatibilityReason::NetworkReserveInsufficient:
            return "Insufficient memory reserve for TLS/network sockets";
        default:
            return "Unknown compatibility constraint";
    }
}

const char* CompatibilityEvaluator::statusToString(CompatibilityStatus status) {
    switch (status) {
        case CompatibilityStatus::Compatible:
            return "compatible";
        case CompatibilityStatus::CompatibleDegraded:
            return "degraded";
        case CompatibilityStatus::Incompatible:
            return "incompatible";
        default:
            return "unknown";
    }
}

const char* CompatibilityEvaluator::strategyToString(PresentationStrategy strategy) {
    switch (strategy) {
        case PresentationStrategy::DIRECT_DMA_DOUBLE:
            return "direct_dma_double";
        case PresentationStrategy::DIRECT_DMA_SINGLE:
            return "direct_dma_single";
        case PresentationStrategy::CANVAS_BURST_DOUBLE:
            return "canvas_burst_double";
        case PresentationStrategy::CANVAS_BURST_SINGLE:
            return "canvas_burst_single";
        case PresentationStrategy::NONE:
        default:
            return "none";
    }
}

const char* CompatibilityEvaluator::storageToString(CanvasStorage storage) {
    switch (storage) {
        case CanvasStorage::SRAM:
            return "sram";
        case CanvasStorage::PSRAM:
            return "psram";
        case CanvasStorage::NONE:
        default:
            return "none";
    }
}

