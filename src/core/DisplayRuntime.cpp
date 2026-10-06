#include "DisplayRuntime.h"
#include "drawing/IDrawingSurface.h"
#include "drawing/PipelineSelectionPolicy.h"
#include "core/EngineRegistry.h"
#include "core/NetworkBudget.h"
#include "MemTrace.h"
#include "hal/HardwareHAL.h"
#include "Logger.h"
#include "MatrixEngine.h"
#include "memory/MemoryManager.h"
#include "SystemWatchdog.h"

#if defined(ESP32)
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#endif

extern MatrixEngine matrixEngine;

DisplayRuntime::DisplayRuntime()
    : m_ctx(nullptr), m_matrixEngine(nullptr), m_rotationManager(nullptr),
      m_overlayManager(nullptr), m_orientationManager(nullptr), m_arbiter(nullptr),
      m_registeredSourceCount(0), m_preemptionDepth(0),
      m_sessionCounter(0), m_lastReconciledVersion(0) {
}

void DisplayRuntime::begin(AppEngineContext* ctx, MatrixEngine* matrix, RotationManager* rot,
                           OverlayManager* ov, DisplayOrientationManager* orient, DisplayArbiter* arb) {
    m_ctx = ctx;
    m_matrixEngine = matrix;
    m_rotationManager = rot;
    if (m_rotationManager) {
        m_rotationManager->setDisplayRuntime(this);
    }
    m_overlayManager = ov;
    m_orientationManager = orient;
    m_arbiter = arb;
    m_registeredSourceCount = 0;
    m_preemptionDepth = 0;
    m_sessionCounter = 0;
    m_lastReconciledVersion = 0;
}

void DisplayRuntime::registerSourceEngine(DisplaySourceId sourceId, IEngine* engine, const EngineHandle& handle) {
    for (size_t i = 0; i < m_registeredSourceCount; ++i) {
        if (m_registeredSources[i].sourceId == sourceId) {
            m_registeredSources[i].engine = engine;
            m_registeredSources[i].handle = handle;
            return;
        }
    }
    if (m_registeredSourceCount < m_registeredSources.size()) {
        m_registeredSources[m_registeredSourceCount++] = {sourceId, handle, engine};
    }
}

IEngine* DisplayRuntime::resolveEngine(const EngineHandle& handle, DisplaySourceId sourceId) const {
    // 1. If explicit instanceId is provided in handle, resolve via RotationManager
    if (handle.instanceId[0] != '\0' && m_rotationManager) {
        IEngine* instEngine = m_rotationManager->findActiveEngine(handle.instanceId);
        if (instEngine) return instEngine;
    }

    // 2. If sourceId is ROTATION, active rotation engine
    if (sourceId == DisplaySourceId::ROTATION) {
        return m_rotationManager ? m_rotationManager->getCurrentActiveEngine() : nullptr;
    }

    // 3. Resolve registered specialized source engines
    for (size_t i = 0; i < m_registeredSourceCount; ++i) {
        if (m_registeredSources[i].sourceId == sourceId) {
            return m_registeredSources[i].engine;
        }
        if (!handle.isEmpty() && m_registeredSources[i].handle == handle) {
            return m_registeredSources[i].engine;
        }
    }

    return nullptr;
}

void DisplayRuntime::reconcile(const ConfigSnapshot& snapshot) {
    if (snapshot.version == m_lastReconciledVersion) {
        return;
    }
    m_lastReconciledVersion = snapshot.version;
    
    // Only notify the currently active instance if present to avoid
    // Core 1 allocation and mutex contention on hot-path (Invariant 1)
    if (m_rotationManager && m_session.activeEngine && m_session.engineHandle.instanceId[0] != '\0') {
        m_rotationManager->notifyConfigChanged(m_session.engineHandle.instanceId);
    }
    LOGI("DisplayRuntime", "Reconciled display runtime to config version %u", snapshot.version);
}

/**
 * @brief Reset the Adafruit_GFX text state shared by every engine through the panel object.
 *
 * Font, text size and wrap are properties of MatrixPanel_I2S_DMA, not of the engine. Engines such
 * as WordClock, MessageEngine, DateEngine and ArcadeClock install a custom GFXfont and do not
 * restore it, and a GFXfont anchors glyphs on their baseline while the built-in font anchors on the
 * top-left corner. Inheriting a leaked font therefore shifts the next engine's whole layout
 * upwards. Resetting at every lifecycle boundary removes the entire bug class instead of relying
 * on each engine to defend itself.
 */
void DisplayRuntime::resetSharedTextState() {
    if (m_surface) {
        m_surface->setFont(nullptr);
        m_surface->setTextSize(1);
        m_surface->setTextWrap(false);
    }
    if (m_matrixEngine && m_matrixEngine->getDisplay()) {
        auto* display = m_matrixEngine->getDisplay();
        display->setFont(nullptr);
        display->setTextSize(1);
        display->setTextWrap(false);
    }
}

void DisplayRuntime::purgeEngineReferences(IEngine* engine, const char* instanceId) {
    if (m_session.activeEngine == engine ||
        (instanceId && instanceId[0] != '\0' && strcmp(m_session.engineHandle.instanceId, instanceId) == 0)) {
        m_session.activeEngine = nullptr;
    }
    for (size_t i = 0; i < m_preemptionDepth; ) {
        bool matches = false;
        if (instanceId && instanceId[0] != '\0' && strcmp(m_preemptionStack[i].handle.instanceId, instanceId) == 0) {
            matches = true;
        } else if (engine && resolveEngine(m_preemptionStack[i].handle, m_preemptionStack[i].sourceId) == engine) {
            matches = true;
        }
        if (matches) {
            for (size_t j = i; j < m_preemptionDepth - 1; ++j) {
                m_preemptionStack[j] = m_preemptionStack[j + 1];
            }
            m_preemptionDepth--;
        } else {
            i++;
        }
    }
}

void DisplayRuntime::transitionSession(const DisplayDecision& decision) {    // PHASE 1: Resolve target engine
    IEngine* targetEngine = resolveEngine(decision.engineHandle, decision.sourceId);

    // PHASE 2: Validate target (reject transactionally if target is unresolvable)
    //
    // ROTATION is the one source whose engine lifetime is owned by RotationManager, not by the
    // runtime: getCurrentActiveEngine() legitimately returns nullptr until RotationManager::loop()
    // has selected its first slot. Rejecting that decision froze the baseline session at boot and
    // spammed the log, so ROTATION binds lazily instead: the session takes ownership now and the
    // next update() promotes it to a REPLACE once the engine exists.
    const bool rotationDeferredBinding =
        (decision.sourceId == DisplaySourceId::ROTATION) && (m_rotationManager != nullptr);

    if (!targetEngine && !rotationDeferredBinding) {
        LOGW("DisplayRuntime", "Rejecting transition: target engine not found");
        return; // Session and stack remain 100% intact
    }

    IEngine* oldEngine = m_session.activeEngine;
    const bool sameEngine = (oldEngine == targetEngine);
    const bool sameSource = (decision.sourceId == m_session.sourceId);
    const bool sameHandle = (decision.engineHandle == m_session.engineHandle);

    // PHASE 3: CLASSIFY & EXECUTE FSM

    // CASE 1: REFRESH IN-PLACE (sameSource && sameHandle && sameEngine)
    if (sameSource && sameHandle && sameEngine) {
        m_session.requestId = decision.requestId;
        m_session.startedAtMs = millis();
        return; // Zero lifecycle, sessionId and stack preserved (internal runtime refresh)
    }

    // CASE 2: PREEMPTION (preemptive && not rotation && new source)
    if (decision.preemptive && decision.sourceId != DisplaySourceId::ROTATION && !sameSource) {
        if (m_preemptionDepth >= MAX_PREEMPTION_DEPTH) {
            LOGW("DisplayRuntime", "Preemption stack full (%u), rejecting preemption", (unsigned)m_preemptionDepth);
            return; // Deterministic rejection: protect baseline session without corruption
        }
        if (oldEngine && !sameEngine) {
            oldEngine->pause();
        }
        m_preemptionStack[m_preemptionDepth++] = PreemptionEntry{
            m_session.engineHandle,
            m_session.sourceId,
            m_session.priority,
            m_session.requestId,
            m_session.sessionId,
            m_session.startedAtMs,
            m_session.allowsOverlay,
            m_session.isRealtime,
            m_session.requiresClear,
            m_session.lifecycle
        };
        if (targetEngine && !sameEngine) {
            maybeReconfigurePipelineFor(targetEngine, decision.engineHandle, decision.sourceId);
            targetEngine->activate();
        }
        
        m_session.sessionId = ++m_sessionCounter;
        m_session.sourceId = decision.sourceId;
        m_session.priority = decision.priority;
        m_session.engineHandle = decision.engineHandle;
        m_session.requestId = decision.requestId;
        m_session.startedAtMs = millis();
        m_session.activeEngine = targetEngine;
        m_session.requiresClear = decision.needsClear;
        m_session.allowsOverlay = decision.allowsOverlay;
        m_session.isRealtime = decision.isRealtime;
        m_session.lifecycle = decision.lifecycle;
        m_session.lastTransitionMode = TransitionMode::PREEMPT;
        return;
    }

    // CASE 3: RESUME (Matches parent in PreemptionStack)
    int parentIdx = -1;
    for (int i = (int)m_preemptionDepth - 1; i >= 0; --i) {
        if (m_preemptionStack[i].sourceId == decision.sourceId &&
            (decision.sourceId == DisplaySourceId::ROTATION || m_preemptionStack[i].handle == decision.engineHandle)) {
            parentIdx = i;
            break;
        }
    }

    if (parentIdx >= 0) {
        // Phase A: Pre-validate parent and cleanup targets BEFORE any side effects
        IEngine* resumeEngine = resolveEngine(m_preemptionStack[parentIdx].handle, m_preemptionStack[parentIdx].sourceId);
        if (!resumeEngine) {
            LOGW("DisplayRuntime", "Parent engine could not be resolved, rejecting RESUME");
            return; // Session and stack remain 100% intact
        }

        // Phase B: Execute lifecycle transitions
        if (oldEngine) {
            oldEngine->deactivate();
        }
        // Cleanup expired intermediate submerged sessions
        for (int i = (int)m_preemptionDepth - 1; i > parentIdx; --i) {
            IEngine* expiredEngine = resolveEngine(m_preemptionStack[i].handle, m_preemptionStack[i].sourceId);
            if (expiredEngine) {
                expiredEngine->deactivate();
            }
        }

        PreemptionEntry parent = m_preemptionStack[parentIdx];
        m_preemptionDepth = (uint8_t)parentIdx; // Secure unwinding

        maybeReconfigurePipelineFor(resumeEngine, parent.handle, parent.sourceId);
        resumeEngine->resume();

        // Restore complete parent session snapshot
        m_session.sessionId = parent.sessionId;
        m_session.sourceId = parent.sourceId;
        m_session.priority = parent.priority;
        m_session.engineHandle = parent.handle;
        m_session.requestId = parent.requestId;
        m_session.startedAtMs = parent.startedAtMs;
        m_session.activeEngine = resumeEngine;
        m_session.requiresClear = parent.requiresClear;
        m_session.allowsOverlay = parent.allowsOverlay;
        m_session.isRealtime = parent.isRealtime;
        m_session.lifecycle = parent.lifecycle;
        m_session.lastTransitionMode = TransitionMode::RESUME;
        return;
    }

    // CASE 4: REPLACE
    const bool isInternalRotationSwitch = (decision.sourceId == DisplaySourceId::ROTATION && m_session.sourceId == DisplaySourceId::ROTATION);

    if (oldEngine && !sameEngine) {
        if (!isInternalRotationSwitch) {
            oldEngine->deactivate();
        }
    }
    // If replacing baseline without preemption, unwind any orphaned preemption entries safely
    if (!decision.preemptive && m_preemptionDepth > 0) {
        for (int i = (int)m_preemptionDepth - 1; i >= 0; --i) {
            IEngine* orphan = resolveEngine(m_preemptionStack[i].handle, m_preemptionStack[i].sourceId);
            if (orphan && orphan != targetEngine && orphan != oldEngine) {
                orphan->deactivate();
            }
        }
        m_preemptionDepth = 0;
    }
    if (targetEngine && !sameEngine) {
        if (!isInternalRotationSwitch) {
            maybeReconfigurePipelineFor(targetEngine, decision.engineHandle, decision.sourceId);
            targetEngine->activate();
        }
    }
    m_session.sessionId = ++m_sessionCounter;
    m_session.sourceId = decision.sourceId;
    m_session.priority = decision.priority;
    m_session.engineHandle = decision.engineHandle;
    m_session.requestId = decision.requestId;
    m_session.startedAtMs = millis();
    m_session.activeEngine = targetEngine;
    m_session.requiresClear = decision.needsClear;
    m_session.allowsOverlay = decision.allowsOverlay;
    m_session.isRealtime = decision.isRealtime;
    m_session.lifecycle = decision.lifecycle;
    m_session.lastTransitionMode = TransitionMode::REPLACE;
}

DisplayDecision DisplayRuntime::update(const ConfigSnapshot& snapshot) {
    reconcile(snapshot);

    // Consume non-blocking memory pressure without locks (Sprint 3)
    MemoryPressureLevel pressure = MemoryManager::instance().consumePendingPressure();
    if (pressure != MemoryPressureLevel::Nominal && m_session.activeEngine) {
        m_session.activeEngine->onMemoryPressure(static_cast<uint8_t>(pressure));
    }

    if (m_orientationManager) {
        m_orientationManager->update(snapshot.matrix.auto_rotate, snapshot.matrix.rotation_offset);
    }

    DisplayDecision decision;
    if (m_arbiter) {
        decision = m_arbiter->evaluate();
    }

    transitionSession(decision);
    return decision;
}

FrameRenderResult DisplayRuntime::render(const DisplayDecision& decision, AppEngineContext* appCtx) {
    FrameRenderResult result;
    if (!m_matrixEngine || !m_matrixEngine->getDisplay()) {
        return result;
    }

    IEngine* activeEngine = getEngineForSource(decision.sourceId, decision.engineHandle);

    // Adafruit_GFX text state (font, size, wrap) lives on the shared panel object, not on the
    // engine. WordClock, MessageEngine, DateEngine and ArcadeClock install a custom GFXfont and do
    // not restore it, and a GFXfont anchors glyphs on their baseline while the built-in font
    // anchors on the top-left corner. Any engine that does not set a font itself (GNews, Crypto,
    // Stock) would therefore inherit the leaked one and render its whole layout shifted upwards.
    // Every font-installing engine re-applies its font inside its own draw path, so resetting once
    // per frame here is safe and removes the entire bug class at its single point of truth.
    resetSharedTextState();

    if (decision.sourceId != DisplaySourceId::ROTATION && activeEngine != nullptr) {
        if (activeEngine->needsClear()) {
            if (m_surface) {
                m_surface->clear(0);
            } else if (m_matrixEngine && m_matrixEngine->getDisplay()) {
                m_matrixEngine->getDisplay()->fillScreen(0);
                matrixEngine.markExternalDraw();
            }
        }
        activeEngine->update(appCtx);
        activeEngine->render(appCtx);
        result.rendered = true;
        result.framebufferChanged = activeEngine->hasNewFrame();
    } else if (m_rotationManager) {
        result.rendered = m_rotationManager->loop();
        result.framebufferChanged = result.rendered;
        activeEngine = m_rotationManager->getCurrentActiveEngine();
    }
    if (activeEngine) {
        result.nextDueInMs = activeEngine->nextFrameDueInMs();
    }

    // Render Overlays (Fighter etc.) if enabled by decision and active rotation slot
    if (m_overlayManager && decision.allowsOverlay && (!activeEngine || activeEngine->allowsOverlay())) {
        OverlayConfig activeOverlayConfig;
        if (decision.sourceId == DisplaySourceId::ROTATION && m_rotationManager) {
            activeOverlayConfig = m_rotationManager->getCurrentOverlays();
        } else if (decision.sourceId == DisplaySourceId::MQTT || decision.sourceId == DisplaySourceId::MARQUEE) {
            activeOverlayConfig.fighter = FighterOverride::Enabled;
        }
        m_overlayManager->configure(activeOverlayConfig);
        m_overlayManager->update();
        m_overlayManager->render();
        // An active overlay draws over the engine's frame; a dirty-pixel engine must repaint under it.
        if (m_overlayManager->isActive()) matrixEngine.markExternalDraw();
    } else if (m_overlayManager) {
        m_overlayManager->deactivate();
    }

    return result;
}

void DisplayRuntime::maybeReconfigurePipelineFor(IEngine* targetEngine, const EngineHandle& handle, DisplaySourceId sourceId) {
    if (!m_matrixEngine) return;

    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const auto& matrixCfg = guard->matrix;
    if (!matrixCfg.dynamicColorDepth) return;

    const char* descId = handle.descriptorId;
    String engineIdStr;
    if ((!descId || descId[0] == '\0') && sourceId == DisplaySourceId::ROTATION && m_rotationManager) {
        engineIdStr = m_rotationManager->getCurrentEngineId();
        descId = engineIdStr.c_str();
    }

    EngineRequirements reqs;
    if (descId && descId[0] != '\0') {
        const EngineDescriptor* desc = EngineRegistry::getDescriptor(descId);
        if (desc) {
            reqs = desc->requirements;
        }
    }

    // Dynamic TLS requirement refinement:
    // If the engine descriptor declares needsTls, check if the target instance actually needs a TLS fetch
    // or if its cache is still fresh (< TTL). If cache is fresh, no TLS burst is needed on this rotation!
    if (reqs.needsTls && targetEngine) {
        reqs.needsTls = targetEngine->needsTlsFetch();
    }

    const bool hasPsram = hardwareHAL.capabilities().hasPsram;

    // Resource-aware TLS gating:
    // If the engine requires TLS on non-PSRAM hardware, verify if NetworkBudget admits a TLS session.
    // If memory/network conditions cannot accommodate TLS right now (e.g. bulk transfer or low DRAM),
    // suppress reqs.needsTls to prevent futile panel teardown and blackout (Invariant 17, 20).
    if (reqs.needsTls && !hasPsram && !NetworkBudget::canStartTlsSession()) {
        LOGI("DisplayRuntime", "TLS session not admitted for '%s': suppressing clean-window teardown to maintain display.",
             descId ? descId : "unknown");
        reqs.needsTls = false;
    }

    size_t largestBlock = 0;
    size_t freeInternal = 0;
    size_t freeDma = 0;
    auto measure = [&]() {
        largestBlock = 0; freeInternal = 0; freeDma = 0;
#if defined(ESP32)
        largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        freeInternal = esp_get_free_internal_heap_size();
        freeDma = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#endif
    };
    measure();

    uint16_t totalWidth = matrixCfg.width * (matrixCfg.chainLength > 0 ? matrixCfg.chainLength : 1);
    uint8_t currentDepth = m_matrixEngine ? m_matrixEngine->getActiveColorDepth() : 0;
    bool isDbl = m_matrixEngine ? m_matrixEngine->isDoubleBuffered() : false;
    bool hasCanvas = (m_surface != nullptr);

    // Stage 1: cheap estimate with the current panel still allocated.
    uint8_t stage1Target = PipelineSelectionPolicy::resolveTargetDepth(
        matrixCfg.colorDepth, matrixCfg.dynamicColorDepth, totalWidth, matrixCfg.height,
        hasPsram, reqs, currentDepth, largestBlock, freeInternal, freeDma, isDbl, hasCanvas);

    LOGI("DisplayRuntime", "Auto Depth Eval for '%s': cur=%u, target=%u (ceil=%u, free=%u, largestBlock=%u, tls=%d)",
         descId ? descId : "unknown", currentDepth, stage1Target, matrixCfg.colorDepth, (unsigned)freeInternal, (unsigned)largestBlock, reqs.needsTls);

    uint8_t targetDepth = stage1Target;
    // Stage 2 (teardown-then-measure): a depth change, or a TLS engine on a zone whose largest block
    // is too small, frees the whole DMA sandbox first. The depth is then decided on the emptied zone
    // (exact measurement, no stale view of the outgoing panel) and the panel is rebuilt. Only the HUB75
    // output goes dark (hundreds of ms); the system zone (Wi-Fi, web server, tasks) keeps running.
    // On classic ESP32 without PSRAM, any TLS engine requires the clean DMA-released window for prefetchData()
    // because mbedTLS cannot allocate its record/BIGNUM buffers while HUB75 DMA is active.
    const bool tlsNeedsCleanWindow = reqs.needsTls && !hasPsram;
    const bool needTeardown = m_matrixEngine && (targetDepth != currentDepth || tlsNeedsCleanWindow);
    if (needTeardown) {
        if (m_surface) {
            m_surface->setPresentationBackend(nullptr);
        }
        m_matrixEngine->releasePanel();
        measure();
        MemTrace::dumpSurvivors("teardown");  // memtrace env only: who pins the emptied zone
        // Invariant: Stage 2 re-evaluates admission on the emptied zone but must never escalate
        // depth beyond stage1Target (which was already bounded by running baseline and engine requirements).
        targetDepth = PipelineSelectionPolicy::resolveTargetDepth(
            stage1Target, matrixCfg.dynamicColorDepth, totalWidth, matrixCfg.height,
            hasPsram, reqs, currentDepth, largestBlock, freeInternal, freeDma, isDbl, hasCanvas,
            /*panelReleased=*/true);
        LOGI("DisplayRuntime", "Teardown-then-measure for '%s': cur=%u -> target=%u (free=%u, largestBlock=%u)",
             descId ? descId : "unknown", currentDepth, targetDepth, (unsigned)freeInternal, (unsigned)largestBlock);

        // Pre-fetch initial data in DMA-released memory window:
        // Strictly gated to memory-constrained platforms (!hasPsram) running TLS engines when WiFi is connected.
        // Boards with PSRAM (ESP32-S3) or unconstrained heap NEVER execute this hook.
        bool wifiReady = false;
#if defined(ESP32)
        wifiReady = (WiFi.status() == WL_CONNECTED);
#endif
        if (targetEngine && reqs.needsTls && !hasPsram && wifiReady) {
#if defined(ESP32)
            esp_task_wdt_reset();
#endif
            targetEngine->prefetchData();
#if defined(ESP32)
            esp_task_wdt_reset();
#endif
            measure();
        }

        auto res = m_matrixEngine->reconfigurePresentationPipeline(targetDepth);
        if (m_surface) {
            m_surface->setPresentationBackend(m_matrixEngine->getPresentationBackend());
        }
    }
    SystemWatchdog::instance().recordEngineState(
        descId,
        m_matrixEngine ? m_matrixEngine->getActiveColorDepth() : targetDepth,
        reqs.needsTls
    );
}
