#include "DisplayRuntime.h"
#include "Logger.h"

DisplayRuntime::DisplayRuntime()
    : m_ctx(nullptr), m_matrixEngine(nullptr), m_rotationManager(nullptr),
      m_overlayManager(nullptr), m_orientationManager(nullptr), m_arbiter(nullptr),
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
    m_sessionCounter = 0;
    m_lastReconciledVersion = 0;
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
    IEngine* targetEngine = (decision.sourceId != DisplaySourceId::ROTATION)
        ? decision.engine
        : (m_rotationManager ? m_rotationManager->getCurrentActiveEngine() : nullptr);

    bool isNewSession = (decision.sourceId != m_session.sourceId) ||
                        (decision.requestId != m_session.requestId) ||
                        (targetEngine != m_session.activeEngine);

    if (isNewSession) {
        // Deactivate previous engine session if it changed
        if (m_session.activeEngine && m_session.activeEngine != targetEngine) {
            m_session.activeEngine->deactivate();
        }

        m_session.sessionId = ++m_sessionCounter;
        m_session.sourceId = decision.sourceId;
        m_session.requestId = decision.requestId;
        m_session.startedAtMs = millis();
        m_session.activeEngine = targetEngine;
        m_session.requiresClear = decision.needsClear;
        m_session.allowsOverlay = decision.allowsOverlay;
        m_session.isRealtime = decision.isRealtime;
        m_session.lifecycle = decision.lifecycle;

        if (targetEngine) {
            targetEngine->activate();
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

    IEngine* activeEngine = nullptr;

    if (decision.sourceId != DisplaySourceId::ROTATION && decision.engine != nullptr) {
        activeEngine = decision.engine;
        if (activeEngine->needsClear()) {
            m_matrixEngine->getDisplay()->fillScreen(0);
        }
        activeEngine->update(appCtx);
        activeEngine->render(appCtx);
        result.rendered = true;
        result.framebufferChanged = activeEngine->hasNewFrame();
    } else {
        if (m_rotationManager) {
            activeEngine = m_rotationManager->getCurrentActiveEngine();
            if (activeEngine && activeEngine->needsClear()) {
                m_matrixEngine->getDisplay()->fillScreen(0);
            }
            bool shouldFlip = m_rotationManager->loop();
            result.rendered = true;
            result.framebufferChanged = shouldFlip;
        }
    }

    // Overlay compositing
    if (m_overlayManager) {
        if (activeEngine && activeEngine->allowsOverlay() && decision.allowsOverlay) {
            if (decision.sourceId == DisplaySourceId::ROTATION && m_rotationManager) {
                m_overlayManager->configure(m_rotationManager->getCurrentOverlays());
            } else {
                m_overlayManager->configure({});
            }
        } else {
            m_overlayManager->configure({});
        }

        m_overlayManager->update();
        if (m_overlayManager->isActive()) {
            m_overlayManager->render();
            result.mustPresent = true;
        }
    }

    return result;
}
