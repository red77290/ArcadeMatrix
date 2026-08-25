#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../core/BitmapFontLoader.h"
#include "../../include/core/EngineContract.h"

#include "TimeData.h"

class ClockFace;

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
    THEME_CUSTOM_GRADIENT = 20,
    THEME_MATRIX_RAIN = 21,
    THEME_TETRIS_GB = 29
};

struct DateConfig {
    int theme;
    String format;
    String date_font;
    int date_size;
    int date_offset_x;
    int date_offset_y;
    String date_font_path;
    String date_color_1;
    String date_color_2;
};

class DateEngine : public IEngine {
public:
    DateEngine();
    ~DateEngine() override;

    // IEngine lifecycle
    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    
    // Updates the current date string (e.g. "Mer 08 Jul")
    void setDate(const char* dateStr);
    void setDateData(const TimeData& d);
    
    // Backward compatibility mapping (maps legacy numeric character ID to new PublisherTheme)
    void setCharacter(int characterId);
    
    void setTheme(PublisherTheme theme);
    
    // Reloads the custom SD font from config.dateSettings.date_font_path (or unloads it if now
    // empty), so a live web UI change takes effect immediately instead of only after reboot.
    void reloadCustomFont();
    
    // Configures font scaling for high-res matrices (128x32, 256x64, etc.)
    void setResolution(int width, int height);

private:
    bool loop();
    MatrixPanel_I2S_DMA* matrix;
    DateConfig m_config;
    char currentDate[32];
    uint16_t textColor;
    uint16_t shadowColor;
    
    PublisherTheme currentTheme;
    BitmapFontLoader customFont; ///< Optional user-provided .amf font (config.dateSettings.date_font_path)
    ClockFace* activeFace;
    TimeData currentDateData;
    
    int matrixW;
    int matrixH;
    
    void applyThemeSettings();
};

class DateEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};

