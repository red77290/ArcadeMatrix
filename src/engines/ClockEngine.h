#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ClockType is removed since we use PublisherTheme from DateEngine.h for everything


#include "TimeData.h"

#include "DateEngine.h" // For PublisherTheme

// Abstract base class for all clock faces
class ClockFace {
public:
    ClockFace(MatrixPanel_I2S_DMA* display) : matrix(display) {}
    virtual ~ClockFace() = default;

    virtual void draw(const TimeData& t) = 0;
    
    // Call frequently for sub-second animations (like matrix rain, sprite interactions)
    virtual void update() = 0; 

protected:
    MatrixPanel_I2S_DMA* matrix;
};

class ClockEngine {
public:
    ClockEngine(MatrixPanel_I2S_DMA* display);
    ~ClockEngine();

    void setTheme(PublisherTheme theme, bool forceReload = false);
    void updateTime(const TimeData& t);
    
    // Handles continuous frame rendering
    bool loop(); 

private:
    MatrixPanel_I2S_DMA* matrix;
    ClockFace* activeFace;
    PublisherTheme currentTheme;
    TimeData currentTime;
};
