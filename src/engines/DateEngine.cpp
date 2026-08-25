#include "DateEngine.h"
#include <string.h>
#include "fonts/ArcadeFonts.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"
#include <stdlib.h>

#include "clocks/CyberpunkClock.h"
#include "clocks/FlipClock.h"
#include "clocks/PongClock.h"
#include "clocks/TetrisClock.h"
#include "clocks/WordClock.h"
#include "clocks/BinaryClock.h"
#include "clocks/PacmanClock.h"
#include "clocks/VersusClock.h"
#include "clocks/SlotMachineClock.h"
#include "clocks/MatrixRainClock.h"


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

DateEngine::DateEngine() : matrix(nullptr), activeFace(nullptr) {
    strcpy(currentDate, "01 Jan");
    currentDateData = {1, 1, 26};
    textColor = 0xFFFF;
    shadowColor = 0;
    currentTheme = THEME_NONE;
    matrixW = 64;
    matrixH = 64;
}

EngineError DateEngine::initialize(EngineContext* context, const EngineConfig* config) {
    matrix = context->getMatrix();
    matrixW = matrix->width();
    matrixH = matrix->height();
    
    textColor = matrix->color565(255, 255, 255);
    shadowColor = matrix->color565(0, 0, 0);

    m_config.theme = config->getInt("date_theme", config->getInt("theme", 0));
    m_config.format = config->getString("date_format", config->getString("format", "%d/%m/%Y").c_str());
    m_config.date_font = config->getString("date_font", config->getString("font", "Default").c_str());
    m_config.date_size = config->getInt("date_size", config->getInt("size", 1));
    m_config.date_offset_x = config->getInt("date_offset_x", config->getInt("offset_x", 0));
    m_config.date_offset_y = config->getInt("date_offset_y", config->getInt("offset_y", 0));
    m_config.date_font_path = config->getString("date_font_path", config->getString("font_path", "").c_str());
    if (m_config.date_font.endsWith(".amf") || m_config.date_font.endsWith(".AMF") || m_config.date_font.startsWith("/")) {
        m_config.date_font_path = m_config.date_font;
    }
    
    reloadCustomFont();
    
    setTheme(static_cast<PublisherTheme>(m_config.theme));
    return EngineError::OK;
}

void DateEngine::onConfigChanged(const EngineConfig* config) {
    if (config) {
        m_config.theme = config->getInt("date_theme", config->getInt("theme", 0));
        m_config.format = config->getString("date_format", config->getString("format", "%d/%m/%Y").c_str());
        m_config.date_font = config->getString("date_font", config->getString("font", "Default").c_str());
        m_config.date_size = config->getInt("date_size", config->getInt("size", 1));
        m_config.date_offset_x = config->getInt("date_offset_x", config->getInt("offset_x", 0));
        m_config.date_offset_y = config->getInt("date_offset_y", config->getInt("offset_y", 0));
        m_config.date_font_path = config->getString("date_font_path", config->getString("font_path", "").c_str());
        if (m_config.date_font.endsWith(".amf") || m_config.date_font.endsWith(".AMF") || m_config.date_font.startsWith("/")) {
            m_config.date_font_path = m_config.date_font;
        }
        m_config.date_color_1 = config->getString("date_color_1", "");
        m_config.date_color_2 = config->getString("date_color_2", "");
        
        reloadCustomFont();
        setTheme(static_cast<PublisherTheme>(m_config.theme));
    }
}

void DateEngine::activate() {}

void DateEngine::update(EngineContext* context) {
    if (context) {
        struct tm timeinfo;
        context->getSystemTime(&timeinfo);
        currentDateData = {(uint8_t)timeinfo.tm_mday, (uint8_t)(timeinfo.tm_mon + 1), (uint8_t)((timeinfo.tm_year + 1900) % 100)};
        
        String format = m_config.format;
        if (format.isEmpty()) format = "%a %d %b";
        strftime(currentDate, sizeof(currentDate), format.c_str(), &timeinfo);
        
        // Handle random theme changes per day if THEME_NONE
        if (m_config.theme == THEME_NONE) {
            static int lastDay = -1;
            if (timeinfo.tm_mday != lastDay) {
                lastDay = timeinfo.tm_mday;
                setTheme(THEME_NONE); // Triggers random pick
            }
        }
    }
    
    if (activeFace) {
        activeFace->update();
    }
}

void DateEngine::render(EngineContext* context) {
    loop(); // reuse the old loop code which does the actual drawing
}

void DateEngine::deactivate() {};

DateEngine::~DateEngine() {
    if (activeFace) delete activeFace;
}

void DateEngine::setDateData(const TimeData& d) {
    currentDateData = d;
}

void DateEngine::setDate(const char* dateStr) {
    strncpy(currentDate, dateStr, sizeof(currentDate) - 1);
    currentDate[sizeof(currentDate) - 1] = '\0';
}

void DateEngine::setResolution(int width, int height) {
    matrixW = width;
    matrixH = height;
}


void DateEngine::setCharacter(int characterId) {
    // Backward compatibility mapping for v2 numeric character IDs to new Publisher Themes
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
    if (currentTheme == theme && activeFace != nullptr) {
        return;
    }
    
    if (activeFace) {
        delete activeFace;
        activeFace = nullptr;
    }

    if ((int)theme == 99 || theme == THEME_NONE) {
        int r = random(0, 12);
        static const PublisherTheme available[] = {
            THEME_NINTENDO, THEME_CAPCOM, THEME_TAITO, THEME_SEGA,
            THEME_CAVE, THEME_KONAMI, THEME_SNK, THEME_CYBERPUNK,
            THEME_FLIP, THEME_MATRIX_RAIN, static_cast<PublisherTheme>(23), // Tetris
            static_cast<PublisherTheme>(26) // Pacman
        };
        theme = available[r];
    }
    
    currentTheme = theme;

    if (theme == THEME_CYBERPUNK) {
        activeFace = new CyberpunkClock(matrix);
    } else if (theme == THEME_FLIP) {
        activeFace = new FlipClock(matrix);
    } else if ((int)theme == 22) {
        activeFace = new PongClock(matrix);
    } else if ((int)theme == 23) {
        activeFace = new TetrisClock(matrix, false);
    } else if ((int)theme == 29) {
        activeFace = new TetrisClock(matrix, true);
    } else if ((int)theme == 24) {
        activeFace = new WordClock(matrix);
    } else if ((int)theme == 25) {
        activeFace = new BinaryClock(matrix);
    } else if ((int)theme == 26) {
        activeFace = new PacmanClock(matrix);
    } else if ((int)theme == 27) {
        activeFace = new VersusClock(matrix);
    } else if (theme == THEME_MATRIX_RAIN) {
        activeFace = new MatrixRainClock(matrix);
    } else if ((int)theme == 28) {
        activeFace = new SlotMachineClock(matrix);
    }
}

void DateEngine::reloadCustomFont() {
    if (m_config.date_font_path.length() > 0 && !m_config.date_font_path.equalsIgnoreCase("Default")) {
        if (!customFont.loadFromSD(m_config.date_font_path.c_str())) {
            String altPath = m_config.date_font_path.startsWith("/") ? m_config.date_font_path : ("/fonts/" + m_config.date_font_path);
            if (!customFont.loadFromSD(altPath.c_str())) {
                Serial.printf("DateEngine: date_font '%s' failed to load from SD.\n", m_config.date_font_path.c_str());
            }
        }
    } else {
        customFont.unload();
    }
}

void DateEngine::applyThemeSettings() {
    if (matrix) {
        matrixW = matrix->width();
        matrixH = matrix->height();
    }
    bool isHD = (matrixH >= 64);
    matrix->setTextSize(1);

    GFXfont* selectedFont = nullptr;

    switch (currentTheme) {
        case THEME_NINTENDO:
            textColor = matrix->color565(228, 0, 15); // Nintendo Red
            shadowColor = matrix->color565(255, 255, 255); // White outline
            selectedFont = isHD ? (GFXfont*)&FreeSansBold12pt7b : (GFXfont*)&FreeSansBold9pt7b;
            break;
            
        case THEME_CAPCOM:
            textColor = matrix->color565(255, 215, 0); // Yellow
            shadowColor = matrix->color565(0, 75, 175); // Blue
            selectedFont = isHD ? (GFXfont*)&namco__12pt7b : (GFXfont*)&namco__9pt7b;
            break;
            
        case THEME_TAITO:
            textColor = matrix->color565(0, 155, 219); // Light Blue
            shadowColor = matrix->color565(255, 255, 255); // White
            selectedFont = isHD ? (GFXfont*)&Retro_Gaming12pt7b : (GFXfont*)&Retro_Gaming9pt7b;
            break;
            
        case THEME_SEGA:
            textColor = matrix->color565(0, 85, 170); // Sega Blue
            shadowColor = matrix->color565(255, 255, 255); // White
            selectedFont = isHD ? (GFXfont*)&FreeMonoBold12pt7b : (GFXfont*)&FreeMonoBold9pt7b;
            break;
            
        case THEME_CAVE:
            textColor = matrix->color565(138, 43, 226); // Purple
            shadowColor = matrix->color565(255, 255, 0); // Yellow
            selectedFont = isHD ? (GFXfont*)&PressStart2P12pt7b : (GFXfont*)&PressStart2P9pt7b;
            break;
            
        case THEME_KONAMI:
            textColor = matrix->color565(255, 69, 0); // Orange Red
            shadowColor = matrix->color565(255, 255, 255); // White
            selectedFont = isHD ? (GFXfont*)&Retro_Gaming12pt7b : (GFXfont*)&Retro_Gaming9pt7b;
            break;
            
        case THEME_SNK:
            textColor = matrix->color565(30, 144, 255); // Dodger Blue
            shadowColor = matrix->color565(255, 215, 0); // Gold
            selectedFont = isHD ? (GFXfont*)&PressStart2P12pt7b : (GFXfont*)&PressStart2P9pt7b;
            break;
            
        case THEME_TECHNOS:
            textColor = matrix->color565(0, 0, 139); // Dark Blue
            shadowColor = matrix->color565(255, 255, 255);
            selectedFont = isHD ? (GFXfont*)&PressStart2P12pt7b : (GFXfont*)&PressStart2P9pt7b;
            break;
            
        case THEME_IGS:
            textColor = matrix->color565(50, 205, 50); // Lime Green
            shadowColor = matrix->color565(255, 215, 0); // Gold
            selectedFont = isHD ? (GFXfont*)&namco__12pt7b : (GFXfont*)&namco__9pt7b;
            break;
            
        case THEME_HUDSON:
            textColor = matrix->color565(255, 255, 0); // Yellow
            shadowColor = matrix->color565(0, 0, 0); // Black
            selectedFont = isHD ? (GFXfont*)&FreeSansBold12pt7b : (GFXfont*)&FreeSansBold9pt7b;
            break;
            
        case THEME_BANPRESTO:
            textColor = matrix->color565(255, 0, 0); // Red
            shadowColor = matrix->color565(0, 0, 0); // Black
            selectedFont = isHD ? (GFXfont*)&namco__12pt7b : (GFXfont*)&namco__9pt7b;
            break;
            
        case THEME_NAMCO:
            textColor = matrix->color565(255, 0, 0); // Red
            shadowColor = matrix->color565(255, 215, 0); // Yellow
            selectedFont = isHD ? (GFXfont*)&namco__12pt7b : (GFXfont*)&namco__9pt7b;
            break;

        case THEME_RYU:
            textColor = matrix->color565(255, 255, 255);
            shadowColor = matrix->color565(200, 0, 0);
            selectedFont = isHD ? (GFXfont*)&namco__12pt7b : (GFXfont*)&namco__9pt7b;
            break;
        case THEME_MARIO:
            textColor = matrix->color565(255, 0, 0);
            shadowColor = matrix->color565(0, 0, 200);
            selectedFont = isHD ? (GFXfont*)&FreeSansBold12pt7b : (GFXfont*)&FreeSansBold9pt7b;
            break;
        case THEME_MARCO:
            textColor = matrix->color565(0, 255, 0);
            shadowColor = matrix->color565(200, 200, 0);
            selectedFont = isHD ? (GFXfont*)&PressStart2P12pt7b : (GFXfont*)&PressStart2P9pt7b;
            break;
        case THEME_MEGAMAN:
            textColor = matrix->color565(0, 255, 255);
            shadowColor = matrix->color565(0, 0, 200);
            selectedFont = isHD ? (GFXfont*)&namco__12pt7b : (GFXfont*)&namco__9pt7b;
            break;
        case THEME_SPACE:
            textColor = matrix->color565(0, 255, 0);
            shadowColor = matrix->color565(255, 255, 255);
            selectedFont = isHD ? (GFXfont*)&Retro_Gaming12pt7b : (GFXfont*)&Retro_Gaming9pt7b;
            break;
        case THEME_BUB:
            textColor = matrix->color565(255, 255, 0);
            shadowColor = matrix->color565(0, 200, 0);
            selectedFont = isHD ? (GFXfont*)&namco__12pt7b : (GFXfont*)&namco__9pt7b;
            break;
            
        case THEME_CYBERPUNK:
            textColor = matrix->color565(0, 140, 0);
            shadowColor = matrix->color565(0, 0, 0);
            selectedFont = isHD ? (GFXfont*)&FreeMonoBold12pt7b : (GFXfont*)&FreeMonoBold9pt7b;
            break;

        case THEME_MATRIX_RAIN:
            textColor = matrix->color565(0, 140, 0);
            shadowColor = matrix->color565(0, 0, 0);
            selectedFont = nullptr;
            break;
            
        case THEME_FLIP:
            textColor = matrix->color565(255, 255, 255);
            shadowColor = matrix->color565(40, 40, 40);
            selectedFont = nullptr;
            break;

        case THEME_CUSTOM_GRADIENT: {
            uint16_t defaultC1 = matrix->color565(0, 255, 255);  // Cyan
            uint16_t defaultC2 = matrix->color565(255, 0, 255);  // Magenta
            if (m_config.date_color_1.length() > 0) {
                const char* hex1 = m_config.date_color_1.c_str();
                if (hex1[0] == '#') hex1++;
                if (strlen(hex1) >= 6) {
                    long val1 = strtol(hex1, NULL, 16);
                    defaultC1 = matrix->color565((val1 >> 16) & 0xFF, (val1 >> 8) & 0xFF, val1 & 0xFF);
                }
            }
            if (m_config.date_color_2.length() > 0) {
                const char* hex2 = m_config.date_color_2.c_str();
                if (hex2[0] == '#') hex2++;
                if (strlen(hex2) >= 6) {
                    long val2 = strtol(hex2, NULL, 16);
                    defaultC2 = matrix->color565((val2 >> 16) & 0xFF, (val2 >> 8) & 0xFF, val2 & 0xFF);
                }
            }
            textColor = defaultC1;
            shadowColor = defaultC2;
            selectedFont = isHD ? (GFXfont*)&FreeSansBold12pt7b : (GFXfont*)&FreeSansBold9pt7b;
            break;
        }

        case THEME_NONE:
        default:
            textColor = matrix->color565(255, 255, 255);
            shadowColor = matrix->color565(0, 0, 0);
            selectedFont = nullptr;
            break;
    }

    // Apply custom font or user-configured font override ONLY if set
    GFXfont* loadedCustomFont = customFont.getFont();
    if (loadedCustomFont) {
        selectedFont = loadedCustomFont;
    } else if (m_config.date_font.equalsIgnoreCase("PressStart2P") || m_config.date_font.equalsIgnoreCase("PressStart2P.ttf")) {
        selectedFont = isHD ? (GFXfont*)&PressStart2P12pt7b : (GFXfont*)&PressStart2P9pt7b;
    } else if (m_config.date_font.equalsIgnoreCase("namco") || m_config.date_font.equalsIgnoreCase("namco.ttf")) {
        selectedFont = isHD ? (GFXfont*)&namco__12pt7b : (GFXfont*)&namco__9pt7b;
    } else if (m_config.date_font.equalsIgnoreCase("FreeSansBold") || m_config.date_font.equalsIgnoreCase("FreeSansBold.ttf")) {
        selectedFont = isHD ? (GFXfont*)&FreeSansBold12pt7b : (GFXfont*)&FreeSansBold9pt7b;
    } else if (m_config.date_font.equalsIgnoreCase("FreeMonoBold") || m_config.date_font.equalsIgnoreCase("FreeMonoBold.ttf")) {
        selectedFont = isHD ? (GFXfont*)&FreeMonoBold12pt7b : (GFXfont*)&FreeMonoBold9pt7b;
    } else if (m_config.date_font.equalsIgnoreCase("RetroGaming") || m_config.date_font.equalsIgnoreCase("Retro_Gaming") || m_config.date_font.equalsIgnoreCase("RetroGaming.ttf")) {
        selectedFont = isHD ? (GFXfont*)&Retro_Gaming12pt7b : (GFXfont*)&Retro_Gaming9pt7b;
    } else if (m_config.date_font.equalsIgnoreCase("Default")) {
        selectedFont = nullptr;
    }
    
    matrix->setFont(selectedFont);

    // Apply user-configured date size directly
    int targetSize = m_config.date_size > 0 ? m_config.date_size : 1;
    matrix->setTextSize(targetSize);

    // Check bounds at targetSize
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds(currentDate, 0, 0, &bx, &by, &bw, &bh);
    
    // Fallback if GFX font overflows total matrix bounds at targetSize
    if (selectedFont != nullptr && (bw > matrixW || bh > matrixH)) {
        if (targetSize > 1) {
            targetSize = 1;
            matrix->setTextSize(1);
            matrix->getTextBounds(currentDate, 0, 0, &bx, &by, &bw, &bh);
        }
        if (bw > matrixW || bh > matrixH) {
            selectedFont = nullptr;
            matrix->setFont(nullptr);
            matrix->setTextSize(1);
        }
    }
}

bool DateEngine::loop() {
    if (matrix) {
        matrixW = matrix->width();
        matrixH = matrix->height();
    }
    
    if (activeFace) {
        activeFace->draw(currentDateData);
        activeFace->update();
        return true;
    }
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
    
    // 3. Calculate exact positioning (identical to ArcadeClock for perfect alignment)
    int16_t x1, y1;
    uint16_t w, h;
    matrix->getTextBounds(currentDate, 0, 0, &x1, &y1, &w, &h);
    
        int logicalSize = m_config.date_size > 0 ? m_config.date_size : 1;
    int effectDepth = (logicalSize >= 5) ? 2 : 1;
    
    int leftExtra = 0, rightExtra = 0, topExtra = 0, bottomExtra = 0;
    if (currentTheme >= THEME_CAVE && currentTheme <= THEME_BUB) {
        leftExtra = 1; rightExtra = effectDepth + 1;
        topExtra = 1; bottomExtra = effectDepth + 1;
    } else if (currentTheme == THEME_NINTENDO || currentTheme == THEME_CAPCOM || currentTheme == THEME_SEGA) {
        leftExtra = effectDepth; rightExtra = effectDepth;
        topExtra = effectDepth; bottomExtra = effectDepth;
    } else if (currentTheme != THEME_FLIP && currentTheme != THEME_NONE) {
        rightExtra = effectDepth; bottomExtra = effectDepth;
    }
    
    int fullW = leftExtra + w + rightExtra;
    int fullH = topExtra + h + bottomExtra;
    
    int x = (matrixW - fullW) / 2 + leftExtra + m_config.date_offset_x - x1;
    int y = (matrixH - fullH) / 2 + topExtra + m_config.date_offset_y - y1;

    // Draw shadow/outline. Mirrors ArcadeClock::drawTextWithShadow()
    matrix->setTextColor(shadowColor);
    if (currentTheme == THEME_NINTENDO || currentTheme == THEME_CAPCOM || currentTheme == THEME_SEGA) {
        // Full outline for certain publishers
        for (int i = 1; i <= effectDepth; i++) {
            matrix->setCursor(x + i, y); matrix->print(currentDate);
            matrix->setCursor(x - i, y); matrix->print(currentDate);
            matrix->setCursor(x, y + i); matrix->print(currentDate);
            matrix->setCursor(x, y - i); matrix->print(currentDate);
        }
    } else if (currentTheme >= THEME_CAVE && currentTheme <= THEME_BUB) {
        // Arcade 3D Outline Effect
        int shadowDepth = effectDepth + 1;
        for (int i = 1; i <= shadowDepth; i++) {
            matrix->setCursor(x + i, y + i); matrix->print(currentDate);
            matrix->setCursor(x + i - 1, y + i); matrix->print(currentDate);
            matrix->setCursor(x + i, y + i - 1); matrix->print(currentDate);
        }

        uint16_t outline = matrix->color565(0, 0, 0);
        matrix->setTextColor(outline);
        matrix->setCursor(x - 1, y - 1); matrix->print(currentDate);
        matrix->setCursor(x, y - 1); matrix->print(currentDate);
        matrix->setCursor(x + 1, y - 1); matrix->print(currentDate);
        matrix->setCursor(x - 1, y); matrix->print(currentDate);
        matrix->setCursor(x + 1, y); matrix->print(currentDate);
        matrix->setCursor(x - 1, y + 1); matrix->print(currentDate);
        matrix->setCursor(x, y + 1); matrix->print(currentDate);
        matrix->setCursor(x + 1, y + 1); matrix->print(currentDate);
    } else {
        // Drop shadow
        for (int i = 1; i <= effectDepth; i++) {
            matrix->setCursor(x + i, y + i); matrix->print(currentDate);
        }
    }
    
    // Draw main text
    matrix->setTextColor(textColor);
    matrix->setCursor(x, y);
    matrix->print(currentDate);
    return true;
}

EngineDescriptor DateEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_date;
    desc_date.metadata = {"date", "Date", "info", FIRMWARE_VERSION};
    desc_date.capabilities.realtime = false;
    desc_date.requirements.needsAudio = false;
    desc_date.requirements.needsNetwork = false;
    desc_date.schema.fields = {
        ConfigField("date_theme", ConfigType::ENUM, "Date Theme", "Visual theme for date", "0", false, "", "", "", "", "/api/themes", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("date_format", ConfigType::STRING, "Date Format", "Format for date display", "%d/%m/%Y", false, "", "", "", "%d/%m/%Y,%Y-%m-%d,%d %b %Y,%A %d %B", "", false, "", ValidationPolicy::Ignore),
        ConfigField("date_font", ConfigType::ENUM, "Font", "Display typeface", "PressStart2P.ttf", false, "", "", "", "", "/api/fonts", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("timezone", ConfigType::ENUM, "Timezone", "Select timezone or region", "Europe/Paris", false, "", "", "", "", "/api/timezones", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("date_size", ConfigType::INTEGER, "Font Size", "Text scaling multiplier", "1", false, "1", "3", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("date_color_1", ConfigType::COLOR, "Primary Color", "Custom gradient top color", "#ffffff", false, "", "", "", "", "", false, "date_theme=20", ValidationPolicy::Ignore),
        ConfigField("date_color_2", ConfigType::COLOR, "Secondary Color", "Custom gradient bottom color", "#00ffff", false, "", "", "", "", "", false, "date_theme=20", ValidationPolicy::Ignore),
        ConfigField("date_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("date_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_date.factory = []() { return std::unique_ptr<IEngine>(new DateEngine()); };
    return desc_date;
}

