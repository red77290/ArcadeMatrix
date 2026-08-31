#include "DisplayRuntime.h"
#include "Logger.h"

DisplayRuntime::DisplayRuntime()
    : m_ctx(nullptr), m_matrixEngine(nullptr), m_rotationManager(nullptr),
      m_overlayManager(nullptr), m_orientationManager(nullptr), m_arbiter(nullptr),
      m_registeredSourceCount(0), m_preemptedEngine(nullptr),
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
    m_preemptedEngine = nullptr;
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
        IEngine* instEngine = m_rotationManager->getActiveEngine(handle.instanceId);
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
    IEngine* targetEngine = resolveEngine(decision.engineHandle, decision.sourceId);

    bool isNewSession = (decision.sourceId != m_session.sourceId) ||
                        (decision.requestId != m_session.requestId) ||
                        (targetEngine != m_session.activeEngine);

    if (isNewSession) {
        IEngine* oldEngine = m_session.activeEngine;

        // Check if this is a temporary preemption (e.g. alert/message interrupting rotation)
        bool isPreemption = (decision.sourceId != DisplaySourceId::ROTATION &&
                             m_session.sourceId == DisplaySourceId::ROTATION);

        // Check if this is returning from preemption back to rotation
        bool isReturningFromPreemption = (decision.sourceId == DisplaySourceId::ROTATION &&
                                          m_session.sourceId != DisplaySourceId::ROTATION &&
                                          m_preemptedEngine != nullptr);

        if (oldEngine && oldEngine != targetEngine) {
            if (isPreemption) {
                // Pause baseline engine during temporary preemption
                oldEngine->pause();
                m_preemptedEngine = oldEngine;
            } else {
                // Deactivate engine on normal rotation transition or end of alert
                oldEngine->deactivate();
                if (oldEngine == m_preemptedEngine) {
                    m_preemptedEngine = nullptr;
                }
            }
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

        if (targetEngine) {
            if (isReturningFromPreemption && targetEngine == m_preemptedEngine) {
                // Resume previously paused baseline engine
                targetEngine->resume();
                m_preemptedEngine = nullptr;
            } else {
                // Activate new engine
                targetEngine->activate();
            }
        }
    }
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
