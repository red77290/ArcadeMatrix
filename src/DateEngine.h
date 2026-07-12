#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

enum PublisherTheme {
    THEME_NONE = -1,
    THEME_NINTENDO = 0,
    THEME_CAPCOM,
    THEME_TAITO,
    THEME_SEGA,
    THEME_CAVE,
    THEME_KONAMI,
    THEME_SNK,
    THEME_TECHNOS,
    THEME_IGS,
    THEME_HUDSON = 9,
    THEME_BANPRESTO = 10,
    THEME_NAMCO = 11,
    THEME_RYU = 12,
    THEME_MARIO = 13,
    THEME_MARCO = 14,
    THEME_MEGAMAN = 15,
    THEME_SPACE = 16,
    THEME_BUB = 17,
    THEME_CYBERPUNK = 18,
    THEME_FLIP = 19
};

class DateEngine {
public:
    DateEngine(MatrixPanel_I2S_DMA* display);
    ~DateEngine();

    void loop();
    
    // Updates the current date string (e.g. "Mer 08 Jul")
    void setDate(const char* dateStr);
    
    // Legacy mapping (maps legacy character ID to new PublisherTheme)
    void setCharacter(int characterId);
    
    void setTheme(PublisherTheme theme);
    
    // Configures font scaling for high-res matrices (128x32, 256x64, etc.)
    void setResolution(int width, int height);

private:
    MatrixPanel_I2S_DMA* matrix;
    char currentDate[32];
    uint16_t textColor;
    uint16_t shadowColor;
    
    PublisherTheme currentTheme;
    
    int matrixW;
    int matrixH;
    
    void applyThemeSettings();
};
