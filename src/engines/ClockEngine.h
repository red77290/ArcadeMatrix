#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "core/EngineContract.h"

// ClockType is removed since we use PublisherTheme from DateEngine.h for everything


#include "TimeData.h"

#include "DateEngine.h" // For PublisherTheme

// Abstract base class for all clock faces
class ClockFace {
public:
    ClockFace(MatrixPanel_I2S_DMA* display, const EngineConfig* config = nullptr) : matrix(display), engineConfig(config) {}
    const EngineConfig* engineConfig;
    virtual ~ClockFace() = default;

    virtual void draw(const TimeData& t) = 0;
    
    // Call frequently for sub-second animations (like matrix rain, sprite interactions)
    virtual void update() = 0; 
    virtual void onDisplayGeometryChanged(const DisplayGeometry& geometry) {}

protected:
    MatrixPanel_I2S_DMA* matrix;
};

class ClockEngine : public IEngine {
public:
    ClockEngine();
    ClockEngine(MatrixPanel_I2S_DMA* display);
    ~ClockEngine() override;

    void setTheme(PublisherTheme theme, bool forceReload = false, const EngineConfig* config = nullptr);
    void updateTime(const TimeData& t);
    bool loop(); 

    // IEngine implementation
    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

private:
    ClockFace* activeFace;
    PublisherTheme currentTheme;
    TimeData currentTime;
    const EngineConfig* currentConfig = nullptr;
    volatile bool configDirty = false;
    MatrixPanel_I2S_DMA* matrixDisplay;
};

class ClockEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};

