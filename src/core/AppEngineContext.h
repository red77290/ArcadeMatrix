#pragma once
#include "core/EngineContract.h"

// Concrete implementation of EngineContext for the main application
class AppEngineContext : public EngineContext {
public:
    AppEngineContext(MatrixPanel_I2S_DMA* matrix, RetroFrontendListener* eventBus)
        : m_matrix(matrix), m_eventBus(eventBus) {}

    MatrixPanel_I2S_DMA* getMatrix() override {
        return m_matrix;
    }

    RetroFrontendListener* getEventBus() override {
        return m_eventBus;
    }

private:
    MatrixPanel_I2S_DMA* m_matrix;
    RetroFrontendListener* m_eventBus;
};
