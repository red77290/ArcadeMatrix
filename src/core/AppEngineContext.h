#pragma once
#include "core/EngineContract.h"
#include "../hal/HardwareHAL.h"

// Concrete implementation of EngineContext for the main application
class AppEngineContext : public EngineContext {
public:
    AppEngineContext(MatrixPanel_I2S_DMA* matrix, FrontendSyncEngine* eventBus)
        : m_matrix(matrix), m_eventBus(eventBus) {}

    MatrixPanel_I2S_DMA* getMatrix() override {
        return m_matrix;
    }

    FrontendSyncEngine* getEventBus() override {
        return m_eventBus;
    }

    void setEventBus(FrontendSyncEngine* eventBus) {
        m_eventBus = eventBus;
    }

    void getSystemTime(struct tm* timeinfo) override {
        getLocalTime(timeinfo, 0);
    }

    bool hasPsram() const override {
        return hardwareHAL.capabilities().hasPsram;
    }

    DisplayGeometry getGeometry() const override;

private:
    MatrixPanel_I2S_DMA* m_matrix;
    FrontendSyncEngine* m_eventBus;
};
