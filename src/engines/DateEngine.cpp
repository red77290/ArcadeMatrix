#include "DateEngine.h"
#include <string.h>
#include "fonts/ArcadeFonts.h"
#include "../core/ConfigLoader.h"
#include <stdlib.h>

extern ConfigLoader config;

#define NUM_DROPS 8
struct DateDrop {
    int x;
    int y;
    int speed;
    int length;
};
static DateDrop dateDrops[NUM_DROPS];
static bool dateDropsInit = false;
static unsigned long dateLastFrameTime = 0;

DateEngine::DateEngine(MatrixPanel_I2S_DMA* display) : matrix(display) {
    strcpy(currentDate, "01 Jan");
    textColor = matrix->color565(255, 255, 255);
    shadowColor = matrix->color565(0, 0, 0);
    currentTheme = THEME_NONE;
    matrixW = matrix->width();
    matrixH = matrix->height();
}

DateEngine::~DateEngine() {}

void DateEngine::setDate(const char* dateStr) {
    strncpy(currentDate, dateStr, sizeof(currentDate) - 1);
    currentDate[sizeof(currentDate) - 1] = '\0';
}

void DateEngine::setResolution(int width, int height) {
    matrixW = width;
    matrixH = height;
}


void DateEngine::setCharacter(int characterId) {
    // Map legacy character IDs to our new Publisher Themes
    switch (characterId) {
        case 0: setTheme(THEME_CAPCOM); break; // Ryu
        case 1: setTheme(THEME_NINTENDO); break; // Mario
        case 2: setTheme(THEME_SNK); break; // Marco
        case 3: setTheme(THEME_CAPCOM); break; // Megaman
        case 4: setTheme(THEME_TAITO); break; // Space Invader
        case 5: setTheme(THEME_TAITO); break; // Bub
        default: setTheme(THEME_NONE); break;
    }
}

void DateEngine::setTheme(PublisherTheme theme) {
    currentTheme = theme;
}

void DateEngine::applyThemeSettings() {
    // Base font size dependent on screen height
    bool isHD = (matrixH >= 64);
    matrix->setTextSize(1);

    switch (currentTheme) {
        case THEME_NINTENDO:
            textColor = matrix->color565(228, 0, 15); // Nintendo Red
            shadowColor = matrix->color565(255, 255, 255); // White outline
            matrix->setFont(isHD ? &FreeSansBold12pt7b : &FreeSansBold9pt7b);
            break;
            
        case THEME_CAPCOM:
            textColor = matrix->color565(255, 215, 0); // Yellow
            shadowColor = matrix->color565(0, 75, 175); // Blue
            matrix->setFont(isHD ? &namco__12pt7b : &namco__9pt7b);
            break;
            
        case THEME_TAITO:
            textColor = matrix->color565(0, 155, 219); // Light Blue
            shadowColor = matrix->color565(255, 255, 255); // White
            matrix->setFont(isHD ? &Retro_Gaming12pt7b : &Retro_Gaming9pt7b);
            break;
            
        case THEME_SEGA:
            textColor = matrix->color565(0, 85, 170); // Sega Blue
            shadowColor = matrix->color565(255, 255, 255); // White
            matrix->setFont(isHD ? &FreeMonoBold12pt7b : &FreeMonoBold9pt7b);
            break;
            
        case THEME_CAVE:
            textColor = matrix->color565(138, 43, 226); // Purple
            shadowColor = matrix->color565(255, 255, 0); // Yellow
            matrix->setFont(isHD ? &PressStart2P12pt7b : &PressStart2P9pt7b);
            break;
            
        case THEME_KONAMI:
            textColor = matrix->color565(255, 69, 0); // Orange Red
            shadowColor = matrix->color565(255, 255, 255); // White
            matrix->setFont(isHD ? &Retro_Gaming12pt7b : &Retro_Gaming9pt7b);
            break;
            
        case THEME_SNK:
            textColor = matrix->color565(30, 144, 255); // Dodger Blue
            shadowColor = matrix->color565(255, 215, 0); // Gold
            matrix->setFont(isHD ? &PressStart2P12pt7b : &PressStart2P9pt7b);
            break;
            
        case THEME_TECHNOS:
            textColor = matrix->color565(0, 0, 139); // Dark Blue
            shadowColor = matrix->color565(255, 255, 255);
            matrix->setFont(isHD ? &PressStart2P12pt7b : &PressStart2P9pt7b);
            break;
            
        case THEME_IGS:
            textColor = matrix->color565(50, 205, 50); // Lime Green
            shadowColor = matrix->color565(255, 215, 0); // Gold
            matrix->setFont(isHD ? &namco__12pt7b : &namco__9pt7b);
            break;
            
        case THEME_HUDSON:
            textColor = matrix->color565(255, 255, 0); // Yellow
            shadowColor = matrix->color565(0, 0, 0); // Black
            matrix->setFont(isHD ? &FreeSansBold12pt7b : &FreeSansBold9pt7b);
            break;
            
        case THEME_BANPRESTO:
            textColor = matrix->color565(255, 0, 0); // Red
            shadowColor = matrix->color565(0, 0, 0); // Black
            matrix->setFont(isHD ? &namco__12pt7b : &namco__9pt7b);
            break;
            
        case THEME_NAMCO:
            textColor = matrix->color565(255, 0, 0); // Red
            shadowColor = matrix->color565(255, 215, 0); // Yellow
            matrix->setFont(isHD ? &namco__12pt7b : &namco__9pt7b);
            break;

        case THEME_RYU:
            textColor = matrix->color565(255, 255, 255);
            shadowColor = matrix->color565(200, 0, 0);
            break;
        case THEME_MARIO:
            textColor = matrix->color565(255, 0, 0);
            shadowColor = matrix->color565(0, 0, 200);
            break;
        case THEME_MARCO:
            textColor = matrix->color565(0, 255, 0);
            shadowColor = matrix->color565(200, 200, 0);
            break;
        case THEME_MEGAMAN:
            textColor = matrix->color565(0, 255, 255);
            shadowColor = matrix->color565(0, 0, 200);
            break;
        case THEME_SPACE:
            textColor = matrix->color565(0, 255, 0);
            shadowColor = matrix->color565(255, 255, 255);
            break;
        case THEME_BUB:
            textColor = matrix->color565(255, 255, 0);
            shadowColor = matrix->color565(0, 200, 0);
            break;
            
        case THEME_CYBERPUNK:
            textColor = matrix->color565(200, 255, 200);
            shadowColor = matrix->color565(0, 0, 0);
            matrix->setFont(isHD ? &FreeMonoBold12pt7b : &FreeMonoBold9pt7b);
            break;
            
        case THEME_FLIP:
            textColor = matrix->color565(255, 255, 255);
            shadowColor = matrix->color565(40, 40, 40); // Used for background panel color here
            matrix->setFont(nullptr);
            break;

        case THEME_NONE:
        default:
            textColor = matrix->color565(255, 255, 255);
            shadowColor = matrix->color565(0, 0, 0);
            break;
    }

    // Apply font based on user config, independently of the theme
    if (currentTheme != THEME_BANPRESTO && currentTheme != THEME_NAMCO && currentTheme != THEME_CYBERPUNK && currentTheme != THEME_FLIP) {
        switch (config.dateSettings.date_font) {
            case THEME_NINTENDO:
            case THEME_HUDSON:
                matrix->setFont(isHD ? &FreeSansBold12pt7b : &FreeSansBold9pt7b); break;
            case THEME_SEGA:
                matrix->setFont(isHD ? &FreeMonoBold12pt7b : &FreeMonoBold9pt7b); break;
            case THEME_CAVE:
            case THEME_SNK:
            case THEME_TECHNOS:
                matrix->setFont(isHD ? &PressStart2P12pt7b : &PressStart2P9pt7b); break;
            case THEME_TAITO:
            case THEME_KONAMI:
                matrix->setFont(isHD ? &Retro_Gaming12pt7b : &Retro_Gaming9pt7b); break;
            case THEME_CAPCOM:
            case THEME_IGS:
            case THEME_BANPRESTO:
            case THEME_NAMCO:
                matrix->setFont(isHD ? &namco__12pt7b : &namco__9pt7b); break;
            default: matrix->setFont(nullptr); break;
        }
    }
    
    // Responsive scaling
    int logicalSize = config.dateSettings.date_size > 0 ? config.dateSettings.date_size : 1;
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds("88 MMM", 0, 0, &bx, &by, &bw, &bh);
    if (bw == 0 || bh == 0) { bw = 48; bh = 7; }
    
    int sMax = min((int)(matrixW / bw), (int)(matrixH / bh));
    if (sMax < 1) sMax = 1;
    
    int gfxSize = 1;
    if (logicalSize == 3) gfxSize = sMax;
    else if (logicalSize == 2) gfxSize = max(1, (sMax * 2) / 3);
    else gfxSize = max(1, sMax / 3);
    
    matrix->setTextSize(gfxSize);
    
    matrix->getTextBounds("88 MMM", 0, 0, &bx, &by, &bw, &bh);
    if (bw > matrixW) {
        matrix->setFont(nullptr);
        matrix->setTextSize(1);
    }
}

void DateEngine::loop() {
    
    if (currentTheme == THEME_CYBERPUNK) {
        if (!dateDropsInit) {
            for (int i=0; i<NUM_DROPS; i++) {
                dateDrops[i].x = rand() % matrixW;
                dateDrops[i].y = (rand() % matrixH) - matrixH;
                dateDrops[i].speed = (rand() % 3) + 1;
                dateDrops[i].length = (rand() % 10) + 5;
            }
            dateDropsInit = true;
        }

        if (millis() - dateLastFrameTime > 100) {
            dateLastFrameTime = millis();
            for (int i=0; i<NUM_DROPS; i++) {
                dateDrops[i].y += dateDrops[i].speed;
                if (dateDrops[i].y - dateDrops[i].length > matrixH) {
                    dateDrops[i].x = rand() % matrixW;
                    dateDrops[i].y = (rand() % 10) * -1;
                    dateDrops[i].speed = (rand() % 3) + 1;
                    dateDrops[i].length = (rand() % 10) + 5;
                }
            }
        }

        for (int i=0; i<NUM_DROPS; i++) {
            for (int j=0; j<dateDrops[i].length; j++) {
                int py = dateDrops[i].y - j;
                if (py >= 0 && py < matrixH) {
                    uint16_t color;
                    if (j == 0) color = matrix->color565(255, 255, 255);
                    else {
                        int green = 255 - (j * (255 / dateDrops[i].length));
                        if (green < 0) green = 0;
                        color = matrix->color565(0, green, 0);
                    }
                    matrix->drawPixel(dateDrops[i].x, py, color);
                }
            }
        }
    }

    // 2. Set Font and Color based on theme
    applyThemeSettings();
    
    // 3. Calculate positioning
    int16_t x1, y1;
    uint16_t w, h;
    matrix->getTextBounds(currentDate, 0, 0, &x1, &y1, &w, &h);
    
    // Use bounds x1 and y1 for precise centering, taking into account font offsets
    int x = (matrixW - w) / 2 - x1 + config.dateSettings.date_offset_x;
    int y;
    if (currentTheme == THEME_NONE) {
        y = (matrixH - 8) / 2 + config.dateSettings.date_offset_y; // Center default font vertically
    } else {
        // GFX Fonts draw from baseline, so we adjust y accurately using y1 bound
        y = (matrixH - h) / 2 - y1 + config.dateSettings.date_offset_y; 
    }
    

    // Draw shadow/outline (offset by 1 pixel in 4 directions for outline, or just bottom-right for shadow)
    matrix->setTextColor(shadowColor);
    if (currentTheme == THEME_NINTENDO || currentTheme == THEME_CAPCOM || currentTheme == THEME_SEGA) {
        // Full outline for certain publishers
        matrix->setCursor(x + 1, y); matrix->print(currentDate);
        matrix->setCursor(x - 1, y); matrix->print(currentDate);
        matrix->setCursor(x, y + 1); matrix->print(currentDate);
        matrix->setCursor(x, y - 1); matrix->print(currentDate);
    } else {
        // Drop shadow
        matrix->setCursor(x + 1, y + 1); matrix->print(currentDate);
    }
    
    // Draw main text
    matrix->setTextColor(textColor);
    matrix->setCursor(x, y);
    matrix->print(currentDate);
}
