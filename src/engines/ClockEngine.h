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

    /// False when the face paints every pixel itself and wants the previous frame left alone, which
    /// is what lets a face repaint only the part that moved instead of the whole panel.
    virtual bool wantsClear() const { return true; }
    /// False when nothing has changed since the last frame, so the rotation can skip the flip.
    virtual bool hasNewFrame() const { return true; }
    /// The face is about to be shown: a face that only repaints on change has to paint now.
    virtual void onActivated() {}

protected:
    MatrixPanel_I2S_DMA* matrix;
};

enum class ClockFormatMode : uint8_t {
    SYSTEM = 0,
    FORCE_12H = 1,
    FORCE_24H = 2
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
    bool needsClear() const override;
    bool hasNewFrame() const override;
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
    ClockFormatMode _formatMode = ClockFormatMode::SYSTEM;

    void updateFormatMode(const EngineConfig* config);
};

class ClockEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};

