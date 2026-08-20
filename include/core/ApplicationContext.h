#pragma once

#include "EngineContract.h"

class ApplicationContext : public EngineContext {
public:
    ApplicationContext(MatrixPanel_I2S_DMA* display, RetroFrontendListener* eventBus)
        : _display(display), _eventBus(eventBus) {}

    MatrixPanel_I2S_DMA* getMatrix() override { return _display; }
    RetroFrontendListener* getEventBus() override { return _eventBus; }

private:
    MatrixPanel_I2S_DMA* _display;
    RetroFrontendListener* _eventBus;
};
