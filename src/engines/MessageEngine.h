#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

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

private:
    MatrixPanel_I2S_DMA* matrix;
    MessageConfig currentMsg;
    bool active;
    
    unsigned long startTime;
    unsigned long lastUpdate;
    
    int cursorX;
    int cursorY;
    int textWidth;
    int textHeight;
};
