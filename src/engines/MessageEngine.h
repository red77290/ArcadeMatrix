#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <gfxfont.h>

struct MessageConfig {
    String text;
    uint16_t color;
    uint8_t size;
    String direction; // "rtl" (Right-To-Left), "ltr", "ttb", "btt"
    int speed;        // Lower is faster (ms per pixel shift)
    unsigned long timeoutSeconds; // 30 by default
};

class MessageEngine {
public:
    MessageEngine(MatrixPanel_I2S_DMA* display);
    
    void displayMessage(const MessageConfig& config);
    bool isActive();
    void loop();
    void stop();

    /// Sets an optional custom GFXfont (e.g. from BitmapFontLoader, loaded from SD) to use
    /// instead of the default 5x7 font for subsequent displayMessage() calls. Pass nullptr to
    /// revert to the default font.
    void setCustomFont(GFXfont* font);

private:
    MatrixPanel_I2S_DMA* matrix;
    MessageConfig currentMsg;
    bool active;
    GFXfont* customFont;
    
    unsigned long startTime;
    unsigned long lastUpdate;
    
    int cursorX;
    int cursorY;
    int textWidth;
    int textHeight;
};
