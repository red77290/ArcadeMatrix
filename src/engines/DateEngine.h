#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../core/BitmapFontLoader.h"

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
    THEME_FLIP = 19,
    // 20 is reserved for a "Custom gradient" theme handled by ArcadeClock (parity with the RPi's
    // theme ID 20). 21 mirrors the RPi's TrueMatrixRenderer (character-based digital rain), unlike
    // THEME_CYBERPUNK above which only animates falling pixel dots.
    THEME_MATRIX_RAIN = 21,
    THEME_TETRIS_GB = 29
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
    
    // Reloads the custom SD font from config.dateSettings.date_font_path (or unloads it if now
    // empty), so a live web UI change takes effect immediately instead of only after reboot.
    void reloadCustomFont();
    
    // Configures font scaling for high-res matrices (128x32, 256x64, etc.)
    void setResolution(int width, int height);

private:
    MatrixPanel_I2S_DMA* matrix;
    char currentDate[32];
    uint16_t textColor;
    uint16_t shadowColor;
    
    PublisherTheme currentTheme;
    BitmapFontLoader customFont; ///< Optional user-provided .amf font (config.dateSettings.date_font_path)
    
    int matrixW;
    int matrixH;
    
    void applyThemeSettings();
};
