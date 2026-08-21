#pragma once

#include "EngineContract.h"

class ApplicationContext : public EngineContext {
public:
    ApplicationContext(MatrixPanel_I2S_DMA* display, FrontendSyncEngine* eventBus)
        : _display(display), _eventBus(eventBus) {}

    MatrixPanel_I2S_DMA* getMatrix() override { return _display; }
    FrontendSyncEngine* getEventBus() override { return _eventBus; }

private:
    MatrixPanel_I2S_DMA* _display;
    FrontendSyncEngine* _eventBus;
};
