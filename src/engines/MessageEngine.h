#pragma once
#include <Arduino.h>
#include "../core/drawing/IDrawingSurface.h"
#include <gfxfont.h>
#include "../core/BitmapFontLoader.h"

struct MessageConfig {
    String text;
    uint16_t color;
    uint8_t size;
    String direction; // "rtl" (Right-To-Left), "ltr", "ttb", "btt"
    int speed;        // Lower is faster (ms per pixel shift)
    unsigned long timeoutSeconds; // 30 by default
    int offsetY;      // vertical shift in pixels for the horizontal directions (negative = higher); brace-initialised messages leave it 0 (centred). No default initialiser: keeps the struct an aggregate for the existing {..} initialisers.
};

#include "../../include/core/EngineContract.h"

class MessageEngine : public IEngine {
public:
    MessageEngine();
    ~MessageEngine() override = default;

    EngineError initialize(EngineContext* context, const EngineConfig* engineConfig) override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void activate() override;
    void deactivate() override;
    void resume() override;
    void onConfigChanged(const EngineConfig* engineConfig) override;
    
    void displayMessage(const MessageConfig& config);
    /// Thread-safe variant for callers on the other core (web server, background tasks): the message is parked and
    /// applied by update() on the render core, so the text is never swapped underneath render().
    void queueMessage(const MessageConfig& config);
    bool isActive() const { return active; }
    bool allowsOverlay() const override { return false; }
    bool needsClear() const override { return true; }

    /// Sets an optional custom GFXfont (e.g. from BitmapFontLoader, loaded from SD) to use
    /// instead of the default 5x7 font for subsequent displayMessage() calls. Pass nullptr to
    /// revert to the default font.
    void setCustomFont(GFXfont* font);
    bool hasCustomFont() const { return customFont != nullptr; }

private:
    enum class ScrollDir : uint8_t {
        RTL = 0,
        LTR,
        TTB,
        BTT,
        STATIC
    };
    static ScrollDir parseScrollDir(const String& dir);
    void measureCurrentMessage(int surfaceW, int surfaceH);

    MessageConfig currentMsg;
    MessageConfig pendingMsg;
    volatile bool pendingFlag = false;
    portMUX_TYPE pendingMux = portMUX_INITIALIZER_UNLOCKED;
    bool active;
    ScrollDir m_configuredDir = ScrollDir::RTL;
    ScrollDir m_effectiveDir = ScrollDir::RTL;
    int m_lastSurfaceW = 0;
    int m_lastSurfaceH = 0;
    GFXfont* customFont;
    GFXfont* m_renderFont = nullptr;
    BitmapFontLoader fontLoader;
    
    unsigned long startTime;
    unsigned long lastUpdate;
    
    float cursorX;
    float cursorY;
    int textWidth = 0;
    int textHeight = 0;
    int m_boundsX1 = 0;
    int m_boundsY1 = 0;
    IDrawingSurface* matrixDisplay = nullptr;
};

class MessageEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};

