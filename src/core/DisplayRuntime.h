#pragma once
#include <Arduino.h>
#include <array>
#include "ConfigLoader.h"
#include "DisplayArbiter.h"
#include "RotationManager.h"
#include "OverlayManager.h"
#include "DisplayOrientationManager.h"
#include "MatrixEngine.h"
#include "FrameScheduler.h"
#include "AppEngineContext.h"

struct RenderSession {
    uint32_t sessionId = 0;
    EngineHandle engineHandle{};
    DisplaySourceId sourceId = DisplaySourceId::ROTATION;
    uint32_t requestId = 0;
    uint32_t startedAtMs = 0;
    RequestLifecycle lifecycle = RequestLifecycle::PERSISTENT;
    OverlayConfig overlayConfig;
    bool requiresClear = true;
    bool allowsOverlay = true;
    bool isRealtime = false;
    IEngine* activeEngine = nullptr;
};

class DisplayRuntime {
public:
    DisplayRuntime();
    
    void begin(AppEngineContext* ctx, MatrixEngine* matrix, RotationManager* rot,
               OverlayManager* ov, DisplayOrientationManager* orient, DisplayArbiter* arb);

    void registerSourceEngine(DisplaySourceId sourceId, IEngine* engine, const EngineHandle& handle = {});
    IEngine* resolveEngine(const EngineHandle& handle, DisplaySourceId sourceId) const;
    inline IEngine* getEngineForSource(DisplaySourceId sourceId, const EngineHandle& handle) const {
        return resolveEngine(handle, sourceId);
    }

    /**
     * @brief Synchronizes configuration with runtime instances (only if version changed).
     */
    void reconcile(const ConfigSnapshot& snapshot);

    /**
     * @brief Evaluates display decision and dispatches lifecycle transitions.
     */
    DisplayDecision update(const ConfigSnapshot& snapshot);

    /**
     * @brief Renders the active session and overlays into the matrix framebuffer.
     */
    FrameRenderResult render(const DisplayDecision& decision, AppEngineContext* appCtx);

    inline FrameScheduler& getScheduler() { return m_scheduler; }
    inline const RenderSession& getCurrentSession() const { return m_session; }

    bool isTransitioning() const {
        return m_orientationManager && m_orientationManager->isTransitioning();
    }

    void renderTransition() {
        if (m_orientationManager) {
            m_orientationManager->renderTransition();
        }
    }

    void transitionSession(const DisplayDecision& decision);

private:
    struct SourceEngineRegistration {
        DisplaySourceId sourceId = DisplaySourceId::ROTATION;
        EngineHandle handle{};
        IEngine* engine = nullptr;

        SourceEngineRegistration() = default;
        SourceEngineRegistration(DisplaySourceId src, const EngineHandle& h, IEngine* eng)
            : sourceId(src), handle(h), engine(eng) {}
    };

    AppEngineContext* m_ctx = nullptr;
    MatrixEngine* m_matrixEngine = nullptr;
    RotationManager* m_rotationManager = nullptr;
    OverlayManager* m_overlayManager = nullptr;
    DisplayOrientationManager* m_orientationManager = nullptr;
    DisplayArbiter* m_arbiter = nullptr;
    FrameScheduler m_scheduler;

    std::array<SourceEngineRegistration, 16> m_registeredSources{};
    size_t m_registeredSourceCount = 0;

    RenderSession m_session;
    IEngine* m_preemptedEngine = nullptr;
    uint32_t m_sessionCounter = 0;
    uint32_t m_lastReconciledVersion = 0;
};
