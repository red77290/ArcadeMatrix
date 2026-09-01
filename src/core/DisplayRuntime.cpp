#include "DisplayRuntime.h"
#include "Logger.h"

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
    
    // Notify rotation manager of config change (reconciles without recreating instances)
    if (m_rotationManager) {
        for (const auto& inst : snapshot.instances) {
            m_rotationManager->notifyConfigChanged(inst.instance_id);
        }
    }
    LOGI("DisplayRuntime", "Reconciled display runtime to config version %u", snapshot.version);
}

void DisplayRuntime::transitionSession(const DisplayDecision& decision) {
    // PHASE 1: Resolve target engine
    IEngine* targetEngine = resolveEngine(decision.engineHandle, decision.sourceId);

    // PHASE 2: Validate target (reject transactionally if target is unresolvable)
    if (!targetEngine) {
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
            (m_session.sourceId == DisplaySourceId::ROTATION) ? DisplayPriority::ROTATION : DisplayPriority::ALERT,
            m_session.requestId,
            m_session.sessionId,
            m_session.startedAtMs,
            m_session.allowsOverlay,
            m_session.isRealtime,
            m_session.requiresClear,
            m_session.lifecycle
        };
        if (targetEngine && !sameEngine) {
            targetEngine->activate();
        }
        
        m_session.sessionId = ++m_sessionCounter;
        m_session.sourceId = decision.sourceId;
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

        resumeEngine->resume();

        // Restore complete parent session snapshot
        m_session.sessionId = parent.sessionId;
        m_session.sourceId = parent.sourceId;
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
    if (oldEngine && !sameEngine) {
        oldEngine->deactivate();
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
        targetEngine->activate();
    }
    m_session.sessionId = ++m_sessionCounter;
    m_session.sourceId = decision.sourceId;
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

    if (decision.sourceId != DisplaySourceId::ROTATION && activeEngine != nullptr) {
        if (activeEngine->needsClear()) {
            m_matrixEngine->getDisplay()->fillScreen(0);
        }
        activeEngine->update(appCtx);
        activeEngine->render(appCtx);
        result.rendered = true;
        result.framebufferChanged = activeEngine->hasNewFrame();
    } else if (m_rotationManager) {
        result.rendered = m_rotationManager->loop();
        result.framebufferChanged = true;
        activeEngine = m_rotationManager->getCurrentActiveEngine();
    }

    // Render Overlays (Fighter etc.) if enabled by decision and active rotation slot
    if (m_overlayManager && decision.allowsOverlay) {
        OverlayConfig activeOverlayConfig;
        if (decision.sourceId == DisplaySourceId::ROTATION && m_rotationManager) {
            activeOverlayConfig = m_rotationManager->getCurrentOverlays();
        }
        m_overlayManager->configure(activeOverlayConfig);
        m_overlayManager->update();
        m_overlayManager->render();
    }

    return result;
}
