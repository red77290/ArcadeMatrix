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
    TransitionMode lastTransitionMode = TransitionMode::NONE;
};

struct PreemptionEntry {
    EngineHandle handle{};
    DisplaySourceId sourceId = DisplaySourceId::ROTATION;
    DisplayPriority priority = DisplayPriority::ROTATION;
    uint32_t requestId = 0;
    uint32_t sessionId = 0;
    uint32_t startedAtMs = 0;
    bool allowsOverlay = true;
    bool isRealtime = false;
    bool requiresClear = true;
    RequestLifecycle lifecycle = RequestLifecycle::PERSISTENT;

    PreemptionEntry() = default;
    PreemptionEntry(const EngineHandle& h, DisplaySourceId s = DisplaySourceId::ROTATION,
                    DisplayPriority p = DisplayPriority::ROTATION, uint32_t req = 0,
                    uint32_t sess = 0, uint32_t started = 0,
                    bool allowOv = true, bool realtime = false, bool clear = true,
                    RequestLifecycle life = RequestLifecycle::PERSISTENT)
        : handle(h), sourceId(s), priority(p), requestId(req), sessionId(sess),
          startedAtMs(started), allowsOverlay(allowOv), isRealtime(realtime),
          requiresClear(clear), lifecycle(life) {}
};

class DisplayRuntime {
public:
    static constexpr size_t MAX_PREEMPTION_DEPTH = 4;

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
    inline uint8_t getPreemptionDepth() const { return m_preemptionDepth; }

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
    std::array<PreemptionEntry, MAX_PREEMPTION_DEPTH> m_preemptionStack{};
    uint8_t m_preemptionDepth = 0;
    uint32_t m_sessionCounter = 0;
    uint32_t m_lastReconciledVersion = 0;
};

