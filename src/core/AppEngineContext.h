#pragma once
#include "core/EngineContract.h"
#include "../hal/HardwareHAL.h"

class IDrawingSurface;

// Concrete implementation of EngineContext for the main application
class AppEngineContext : public EngineContext {
public:
    AppEngineContext(IDrawingSurface* surface, MatrixPanel_I2S_DMA* matrix, FrontendSyncEngine* eventBus)
        : m_surface(surface), m_matrix(matrix), m_eventBus(eventBus) {}

    AppEngineContext(MatrixPanel_I2S_DMA* matrix, FrontendSyncEngine* eventBus)
        : m_surface(nullptr), m_matrix(matrix), m_eventBus(eventBus) {}

    IDrawingSurface* getSurface() override {
        return m_surface;
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
    MatrixPanel_I2S_DMA* m_matrix;
    FrontendSyncEngine* m_eventBus;
};
