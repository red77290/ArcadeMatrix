#pragma once
#include "core/EngineContract.h"
#include "../hal/HardwareHAL.h"

#include "drawing/IDrawingSurface.h"
#include "drawing/DirectDmaSurface.h"
#include <memory>

// Concrete implementation of EngineContext for the main application
class AppEngineContext : public EngineContext {
public:
    AppEngineContext(IDrawingSurface* surface, MatrixPanel_I2S_DMA* matrix, FrontendSyncEngine* eventBus)
        : m_surface(surface), m_matrix(matrix), m_eventBus(eventBus) {}

    AppEngineContext(MatrixPanel_I2S_DMA* matrix, FrontendSyncEngine* eventBus)
        : m_surface(nullptr), m_matrix(matrix), m_eventBus(eventBus) {}

    IDrawingSurface* getSurface() override {
        if (m_surface) return m_surface;
        if (m_matrix) {
            if (!m_fallbackSurface) {
                m_fallbackSurface = std::unique_ptr<DirectDmaSurface>(
                    new DirectDmaSurface(m_matrix, m_matrix->width(), m_matrix->height())
                );
            }
            return m_fallbackSurface.get();
        }
        return nullptr;
    }

    MatrixPanel_I2S_DMA* getMatrix() override {
        return m_matrix;
    }

    void setSurface(IDrawingSurface* surface) {
        m_surface = surface;
    }

    FrontendSyncEngine* getEventBus() override {
        return m_eventBus;
    }

    void setEventBus(FrontendSyncEngine* eventBus) {
        m_eventBus = eventBus;
    }

    void getSystemTime(struct tm* timeinfo) override {
        if (!timeinfo) return;
        time_t now = 0;
        time(&now);
        localtime_r(&now, timeinfo);
    }

    bool hasPsram() const override {
        return hardwareHAL.capabilities().hasPsram;
    }

    DisplayGeometry getGeometry() const override;

private:
    IDrawingSurface* m_surface = nullptr;
    std::unique_ptr<DirectDmaSurface> m_fallbackSurface;
    MatrixPanel_I2S_DMA* m_matrix;
    FrontendSyncEngine* m_eventBus;
};
