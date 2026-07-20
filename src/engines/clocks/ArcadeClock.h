#pragma once
#include "../ClockEngine.h"
#include <SD.h>
#include "../DateEngine.h" // For PublisherTheme
#include "../../core/BitmapFontLoader.h"

class ArcadeClock : public ClockFace {
public:
    ArcadeClock(MatrixPanel_I2S_DMA* display);
    void draw(const TimeData& t) override;
    void update() override;
    void setTheme(PublisherTheme theme);

private:
    uint8_t lastMinute;
    TimeData storedTime;
    PublisherTheme currentTheme;
    BitmapFontLoader customFont; ///< Optional user-provided .amf font (config.time.clock_font_path)

    // Animation state
    bool isAnimating;
    int animationFrame;
    unsigned long lastFrameTime;

    void drawStaticTime();
    void drawTextWithShadow(int x, int y, uint16_t textColor, uint16_t shadowColor);
    void applyThemeFont();
    
    // Animations
    void triggerAnimation();
    void updateRyuAnimation();
    void updateMarioAnimation();
};
