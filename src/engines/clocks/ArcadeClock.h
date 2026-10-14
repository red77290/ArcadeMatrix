#pragma once
#include "../ClockEngine.h"
#include "../../core/SDUtils.h"
#include "../DateEngine.h" // For PublisherTheme
#include "../../core/BitmapFontLoader.h"

#include "ClockFaceFont.h"

class ArcadeClock : public ClockFace {
public:
    ArcadeClock(IDrawingSurface* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override {}
    void setTheme(PublisherTheme theme);

private:
    ClockFaceFont::Glow glow;   ///< resolved once at build time, never on the draw path
    uint8_t lastMinute;
    TimeData storedTime;
    PublisherTheme currentTheme;
    BitmapFontLoader customFont; ///< Optional user-provided .amf font (config.time.clock_font_path)

    // Animation state
    bool isAnimating;
    int animationFrame;
    unsigned long lastFrameTime;

    // Cached configuration (resolved once at construction / theme change)
    bool m_showSeconds = true;
    bool m_hasFontOverride = false;
    const GFXfont* m_overrideFont9pt = nullptr;
    const GFXfont* m_overrideFont12pt = nullptr;
    int m_logicalSize = 1;
    int m_tateLogicalSize = 0;
    int m_offsetX = 0;
    int m_offsetY = 0;
    uint16_t m_customColor1 = 0x07FF;
    uint16_t m_customColor2 = 0xF81F;

    void drawStaticTime();
    void drawTateTime();
    void drawTextWithShadow(int x, int y, uint16_t textColor, uint16_t shadowColor, int scale = 1);
    void drawStrWithShadow(const char* str, int x, int y, uint16_t textColor, uint16_t shadowColor, int scale = 1);
    void applyThemeFont();
    
    // Animations
    void triggerAnimation();
    void updateMarioAnimation();
};
