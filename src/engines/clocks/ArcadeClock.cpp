#include "ArcadeClock.h"
#include "../../core/ConfigLoader.h"
#include "../fonts/ArcadeFonts.h"

ArcadeClock::ArcadeClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config) {
    lastMinute = 255;
    isAnimating = false;
    animationFrame = 0;
    lastFrameTime = 0;
    currentTheme = static_cast<PublisherTheme>((engineConfig ? engineConfig->getInt("clock_theme", engineConfig->getInt("theme", 0)) : 0));
    String fontSetting = engineConfig ? engineConfig->getString("clock_font", "") : "";
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("font", "");
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("clock_font_path", "");
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("font_path", "");
    if (fontSetting.length() > 0 && !fontSetting.equalsIgnoreCase("Default")) {
        if (fontSetting.endsWith(".amf") || fontSetting.endsWith(".AMF") || fontSetting.startsWith("/")) {
            if (!customFont.loadFromSD(fontSetting.c_str())) {
                String altPath = fontSetting.startsWith("/") ? fontSetting : ("/fonts/" + fontSetting);
                if (!customFont.loadFromSD(altPath.c_str())) {
                    Serial.printf("ArcadeClock: clock_font '%s' failed to load from SD.\n", fontSetting.c_str());
                }
            }
        }
    }
}

void ArcadeClock::setTheme(PublisherTheme theme) {
    currentTheme = theme;
}

void ArcadeClock::draw(const TimeData& t) {
    bool minuteChanged = (lastMinute != t.minutes);
    storedTime = t;
    
    if (minuteChanged && !isAnimating) {
        triggerAnimation();
    }
    
    lastMinute = t.minutes;
}

void ArcadeClock::drawStrWithShadow(const char* str, int x, int y, uint16_t textColor, uint16_t shadowColor, int scale) {
    if (!str || !matrix) return;
    int currentScale = max(1, scale);
    int effectDepth = (currentScale >= 5) ? 2 : 1;

    if (currentTheme == THEME_NINTENDO || currentTheme == THEME_CAPCOM || currentTheme == THEME_SEGA) {
        matrix->setTextColor(shadowColor);
        for (int i = 1; i <= effectDepth; i++) {
            matrix->setCursor(x + i, y); matrix->print(str);
            matrix->setCursor(x - i, y); matrix->print(str);
            matrix->setCursor(x, y + i); matrix->print(str);
            matrix->setCursor(x, y - i); matrix->print(str);
        }
    } else if (currentTheme >= THEME_CAVE && currentTheme <= THEME_BUB) {
        // Arcade 3D Outline Effect
        matrix->setTextColor(shadowColor);
        int shadowDepth = effectDepth + 1;
        for (int i = 1; i <= shadowDepth; i++) {
            matrix->setCursor(x + i, y + i); matrix->print(str);
            matrix->setCursor(x + i - 1, y + i); matrix->print(str);
            matrix->setCursor(x + i, y + i - 1); matrix->print(str);
        }

        uint16_t outline = matrix->color565(0, 0, 0);
        matrix->setTextColor(outline);
        matrix->setCursor(x - 1, y - 1); matrix->print(str);
        matrix->setCursor(x, y - 1); matrix->print(str);
        matrix->setCursor(x + 1, y - 1); matrix->print(str);
        matrix->setCursor(x - 1, y); matrix->print(str);
        matrix->setCursor(x + 1, y); matrix->print(str);
        matrix->setCursor(x - 1, y + 1); matrix->print(str);
        matrix->setCursor(x, y + 1); matrix->print(str);
        matrix->setCursor(x + 1, y + 1); matrix->print(str);
    } else if (currentTheme != THEME_FLIP && currentTheme != THEME_NONE) {
        matrix->setTextColor(shadowColor);
        matrix->setCursor(x + effectDepth, y + effectDepth); matrix->print(str);
        matrix->setCursor(x + effectDepth - 1, y + effectDepth); matrix->print(str);
        matrix->setCursor(x + effectDepth, y + effectDepth - 1); matrix->print(str);
    }

    matrix->setTextColor(textColor);
    matrix->setCursor(x, y);
    matrix->print(str);
}

void ArcadeClock::drawTextWithShadow(int x, int y, uint16_t textColor, uint16_t shadowColor, int scale) {
    char timeStr[16];
    String fmt = engineConfig ? engineConfig->getString("clock_format", "%H:%M:%S") : "%H:%M:%S";
    if (fmt.isEmpty() && engineConfig) fmt = engineConfig->getString("format", "%H:%M:%S");
    if (fmt == "%H:%M") {
        sprintf(timeStr, "%02d:%02d", storedTime.hours, storedTime.minutes);
    } else {
        sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    }
    drawStrWithShadow(timeStr, x, y, textColor, shadowColor, scale);
}

void ArcadeClock::drawTateTime() {
    int w = matrix->width();
    int h = matrix->height();

    uint16_t textColor = matrix->color565(255, 255, 255);
    uint16_t shadowColor = matrix->color565(0, 0, 0);

    switch (currentTheme) {
        case THEME_NINTENDO: textColor = matrix->color565(228, 0, 15); shadowColor = matrix->color565(255, 255, 255); break;
        case THEME_CAPCOM: textColor = matrix->color565(255, 215, 0); shadowColor = matrix->color565(0, 75, 175); break;
        case THEME_TAITO: textColor = matrix->color565(0, 155, 219); shadowColor = matrix->color565(255, 255, 255); break;
        case THEME_SEGA: textColor = matrix->color565(0, 85, 170); shadowColor = matrix->color565(255, 255, 255); break;
        case THEME_CAVE: textColor = matrix->color565(138, 43, 226); shadowColor = matrix->color565(255, 255, 0); break;
        case THEME_KONAMI: textColor = matrix->color565(255, 69, 0); shadowColor = matrix->color565(255, 255, 255); break;
        case THEME_SNK: textColor = matrix->color565(30, 144, 255); shadowColor = matrix->color565(255, 215, 0); break;
        case THEME_TECHNOS: textColor = matrix->color565(0, 0, 139); shadowColor = matrix->color565(255, 255, 255); break;
        case THEME_IGS: textColor = matrix->color565(50, 205, 50); shadowColor = matrix->color565(255, 215, 0); break;
        case THEME_HUDSON: textColor = matrix->color565(255, 255, 0); shadowColor = matrix->color565(0, 0, 0); break;
        case THEME_BANPRESTO: case THEME_NAMCO: textColor = matrix->color565(255, 0, 0); shadowColor = matrix->color565(255, 215, 0); break;
        default: textColor = matrix->color565(0, 220, 255); shadowColor = matrix->color565(0, 50, 120); break;
    }

    char hStr[8], mStr[8], sStr[8];
    sprintf(hStr, "%02d", storedTime.hours);
    sprintf(mStr, "%02d", storedTime.minutes);
    sprintf(sStr, "%02d", storedTime.seconds);

    matrix->setFont(nullptr);
    int scale = (w >= 64) ? 4 : 2;
    matrix->setTextSize(scale);

    int digitW = 2 * 6 * scale - scale;
    int digitH = 8 * scale;
    int drawX = (w - digitW) / 2;

    int offsetX = engineConfig ? engineConfig->getInt("clock_offset_x", engineConfig->getInt("offset_x", 0)) : 0;
    int offsetY = engineConfig ? engineConfig->getInt("clock_offset_y", engineConfig->getInt("offset_y", 0)) : 0;
    drawX += offsetX;

    if (h >= 128) {
        // 3 Tiers (Hours, Minutes, Seconds)
        int yH = (h / 6) - (digitH / 2) + offsetY;
        int yM = (h / 2) - (digitH / 2) + offsetY;
        int yS = (5 * h / 6) - (digitH / 2) + offsetY;

        drawStrWithShadow(hStr, drawX, yH, textColor, shadowColor, scale);
        drawStrWithShadow(mStr, drawX, yM, textColor, shadowColor, scale);
        drawStrWithShadow(sStr, drawX, yS, matrix->color565(200, 200, 220), shadowColor, max(1, scale - 1));
    } else {
        // 2 Tiers (Hours top, Minutes bottom, pulsing dots in center)
        int yH = (h / 4) - (digitH / 2) + offsetY + 2;
        int yM = (3 * h / 4) - (digitH / 2) + offsetY - 2;

        drawStrWithShadow(hStr, drawX, yH, textColor, shadowColor, scale);
        drawStrWithShadow(mStr, drawX, yM, textColor, shadowColor, scale);

        // Center Pulsing Colon
        int dotX = (w / 2) - 1 + offsetX;
        int dotY1 = (h / 2) - 3 + offsetY;
        int dotY2 = (h / 2) + 2 + offsetY;
        matrix->fillRect(dotX, dotY1, 2, 2, textColor);
        matrix->fillRect(dotX, dotY2, 2, 2, textColor);
    }
}

void ArcadeClock::drawStaticTime() {
    if (matrix && matrix->height() > matrix->width()) {
        drawTateTime();
        return;
    }
    int logicalSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (logicalSize < 1) logicalSize = 1;
    bool isHD = (matrix->height() >= 64);
    
    const GFXfont* font9pt = nullptr;
    const GFXfont* font12pt = nullptr;
    
    String fontSetting = engineConfig ? engineConfig->getString("clock_font", "") : "";
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("font", "");
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("clock_font_path", "");
    if (fontSetting.isEmpty() && engineConfig) fontSetting = engineConfig->getString("font_path", "");
    
    if (fontSetting.equalsIgnoreCase("PressStart2P") || fontSetting.equalsIgnoreCase("PressStart2P.ttf")) {
        font9pt = &PressStart2P9pt7b; font12pt = &PressStart2P12pt7b;
    } else if (fontSetting.equalsIgnoreCase("namco") || fontSetting.equalsIgnoreCase("namco.ttf")) {
        font9pt = &namco__9pt7b; font12pt = &namco__12pt7b;
    } else if (fontSetting.equalsIgnoreCase("FreeSansBold") || fontSetting.equalsIgnoreCase("FreeSansBold.ttf")) {
        font9pt = &FreeSansBold9pt7b; font12pt = &FreeSansBold12pt7b;
    } else if (fontSetting.equalsIgnoreCase("FreeMonoBold") || fontSetting.equalsIgnoreCase("FreeMonoBold.ttf")) {
        font9pt = &FreeMonoBold9pt7b; font12pt = &FreeMonoBold12pt7b;
    } else if (fontSetting.equalsIgnoreCase("RetroGaming") || fontSetting.equalsIgnoreCase("Retro_Gaming") || fontSetting.equalsIgnoreCase("RetroGaming.ttf")) {
        font9pt = &Retro_Gaming9pt7b; font12pt = &Retro_Gaming12pt7b;
    } else if (customFont.getFont()) {
        font9pt = customFont.getFont();
        font12pt = customFont.getFont();
    } else if (fontSetting.equalsIgnoreCase("Default")) {
        font9pt = nullptr; font12pt = nullptr;
    } else {
        switch (currentTheme) {
            case THEME_NINTENDO: case THEME_HUDSON: font9pt = &FreeSansBold9pt7b; font12pt = &FreeSansBold12pt7b; break;
            case THEME_SEGA: font9pt = &FreeMonoBold9pt7b; font12pt = &FreeMonoBold12pt7b; break;
            case THEME_CAVE: case THEME_SNK: case THEME_TECHNOS: case THEME_MARCO: font9pt = &PressStart2P9pt7b; font12pt = &PressStart2P12pt7b; break;
            case THEME_TAITO: case THEME_KONAMI: case THEME_SPACE: font9pt = &Retro_Gaming9pt7b; font12pt = &Retro_Gaming12pt7b; break;
            case THEME_CAPCOM: case THEME_IGS: case THEME_BANPRESTO: case THEME_NAMCO: case THEME_RYU: case THEME_MEGAMAN: case THEME_MARIO: case THEME_BUB: font9pt = &namco__9pt7b; font12pt = &namco__12pt7b; break;
            default: font9pt = nullptr; font12pt = nullptr; break;
        }
    }
    
    char timeStr[16];
    String fmt = engineConfig ? engineConfig->getString("clock_format", "%H:%M:%S") : "%H:%M:%S";
    if (fmt.isEmpty() && engineConfig) fmt = engineConfig->getString("format", "%H:%M:%S");
    if (fmt == "%H:%M") {
        sprintf(timeStr, "%02d:%02d", storedTime.hours, storedTime.minutes);
    } else {
        sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    }

    const GFXfont* chosenFont = (isHD && font12pt) ? font12pt : font9pt;
    
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    int gfxSize = 1;
    
    if (chosenFont != nullptr) {
        matrix->setFont(chosenFont);
        matrix->setTextSize(1);
        matrix->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
        
        if (bw > matrix->width() || bh > matrix->height()) {
            // If font overflows screen at 1x size, fallback to 5x7 default font
            chosenFont = nullptr;
            matrix->setFont(nullptr);
        } else {
            int sMaxW = bw > 0 ? (matrix->width() / bw) : 1;
            int sMaxH = bh > 0 ? (matrix->height() / bh) : 1;
            int sMax = min(sMaxW, sMaxH);
            gfxSize = min(max(1, logicalSize), max(1, sMax));
            matrix->setTextSize(gfxSize);
            matrix->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
        }
    }
    
    if (chosenFont == nullptr) {
        matrix->setFont(nullptr);
        matrix->setTextSize(1);
        matrix->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0 || bh == 0) { bw = strlen(timeStr) * 6; bh = 8; }
        int sMaxW = bw > 0 ? (matrix->width() / bw) : 1;
        int sMaxH = bh > 0 ? (matrix->height() / bh) : 1;
        int sMax = min(sMaxW, sMaxH);
        gfxSize = min(max(1, logicalSize), max(1, sMax));
        matrix->setTextSize(gfxSize);
        matrix->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
    }
    
    int effectDepth = (gfxSize >= 5) ? 2 : 1;
    
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
    
    int fullW = leftExtra + bw + rightExtra;
    int fullH = topExtra + bh + bottomExtra;
    
    int offsetX = engineConfig ? engineConfig->getInt("clock_offset_x", engineConfig->getInt("offset_x", 0)) : 0;
    int offsetY = engineConfig ? engineConfig->getInt("clock_offset_y", engineConfig->getInt("offset_y", 0)) : 0;
    int x = (matrix->width() - fullW) / 2 + leftExtra + offsetX - bx;
    int y = (matrix->height() - fullH) / 2 + topExtra - by + offsetY;
    
    uint16_t textColor = matrix->color565(255, 255, 255);
    uint16_t shadowColor = matrix->color565(0, 0, 0);
    
    switch (currentTheme) {
        case THEME_NINTENDO:
            textColor = matrix->color565(228, 0, 15); // Nintendo Red
            shadowColor = matrix->color565(255, 255, 255); // White outline
            break;
            
        case THEME_CAPCOM:
            textColor = matrix->color565(255, 215, 0); // Yellow
            shadowColor = matrix->color565(0, 75, 175); // Blue
            break;
            
        case THEME_TAITO:
            textColor = matrix->color565(0, 155, 219); // Light Blue
            shadowColor = matrix->color565(255, 255, 255); // White
            break;
            
        case THEME_SEGA:
            textColor = matrix->color565(0, 85, 170); // Sega Blue
            shadowColor = matrix->color565(255, 255, 255); // White
            break;
            
        case THEME_CAVE:
            textColor = matrix->color565(138, 43, 226); // Purple
            shadowColor = matrix->color565(255, 255, 0); // Yellow
            break;
            
        case THEME_KONAMI:
            textColor = matrix->color565(255, 69, 0); // Orange Red
            shadowColor = matrix->color565(255, 255, 255); // White
            break;
            
        case THEME_SNK:
            textColor = matrix->color565(30, 144, 255); // Dodger Blue
            shadowColor = matrix->color565(255, 215, 0); // Gold
            break;
            
        case THEME_TECHNOS:
            textColor = matrix->color565(0, 0, 139); // Dark Blue
            shadowColor = matrix->color565(255, 255, 255);
            break;
            
        case THEME_IGS:
            textColor = matrix->color565(50, 205, 50); // Lime Green
            shadowColor = matrix->color565(255, 215, 0); // Gold
            break;
            
        case THEME_HUDSON:
            textColor = matrix->color565(255, 255, 0); // Yellow
            shadowColor = matrix->color565(0, 0, 0); // Black
            break;
            
        case THEME_BANPRESTO:
            textColor = matrix->color565(255, 0, 0); // Red
            shadowColor = matrix->color565(0, 0, 0); // Black
            break;
            
        case THEME_NAMCO:
            textColor = matrix->color565(255, 0, 0); // Red
            shadowColor = matrix->color565(255, 215, 0); // Yellow
            break;

        case THEME_RYU:
            textColor = matrix->color565(255, 255, 0); // Ryu Yellow
            shadowColor = matrix->color565(255, 0, 0); // Ryu Red
            break;
            
        case THEME_MARIO:
            textColor = matrix->color565(255, 50, 50); // Mario Red
            shadowColor = matrix->color565(255, 255, 255); // White shadow
            break;
            
        case THEME_MEGAMAN:
            textColor = matrix->color565(0, 255, 255); // Cyan
            shadowColor = matrix->color565(0, 0, 255); // Blue
            break;
            
        case THEME_BUB:
            textColor = matrix->color565(0, 255, 0); // Bub Green
            shadowColor = matrix->color565(255, 0, 255); // Pink
            break;
            
        case THEME_MARCO:
            textColor = matrix->color565(0, 255, 0); // Green
            shadowColor = matrix->color565(200, 200, 0); // Yellow/Gold
            break;
            
        case THEME_SPACE:
            textColor = matrix->color565(0, 255, 0); // Green
            shadowColor = matrix->color565(255, 255, 255); // White shadow
            break;

        case THEME_CUSTOM_GRADIENT: {
            uint16_t defaultC1 = matrix->color565(0, 255, 255);  // Cyan
            uint16_t defaultC2 = matrix->color565(255, 0, 255);  // Magenta
            if ((engineConfig ? engineConfig->getString("clock_color_1", "") : String("")).length() > 0) {
                const char* hex1 = (engineConfig ? engineConfig->getString("clock_color_1", "") : String("")).c_str();
                if (hex1[0] == '#') hex1++;
                if (strlen(hex1) >= 6) {
                    long val1 = strtol(hex1, NULL, 16);
                    defaultC1 = matrix->color565((val1 >> 16) & 0xFF, (val1 >> 8) & 0xFF, val1 & 0xFF);
                }
            }
            if ((engineConfig ? engineConfig->getString("clock_color_2", "") : String("")).length() > 0) {
                const char* hex2 = (engineConfig ? engineConfig->getString("clock_color_2", "") : String("")).c_str();
                if (hex2[0] == '#') hex2++;
                if (strlen(hex2) >= 6) {
                    long val2 = strtol(hex2, NULL, 16);
                    defaultC2 = matrix->color565((val2 >> 16) & 0xFF, (val2 >> 8) & 0xFF, val2 & 0xFF);
                }
            }
            textColor = defaultC1;
            shadowColor = defaultC2;
            break;
        }

        case THEME_NONE:
        default:
            textColor = matrix->color565(255, 255, 255);
            shadowColor = matrix->color565(0, 0, 0);
            break;
    }
    
    drawTextWithShadow(x, y, textColor, shadowColor, gfxSize);
}

void ArcadeClock::triggerAnimation() {
    isAnimating = true;
    animationFrame = 0;
    lastFrameTime = millis();
}

void ArcadeClock::update() {
    if (isAnimating) {
        if (true) {
            lastFrameTime = millis();
            animationFrame++;
            if (animationFrame > 30) {
                isAnimating = false;
            }
        }
    }

    if (isAnimating) {
        if (currentTheme == THEME_RYU) updateRyuAnimation();
        else if (currentTheme == THEME_MARIO) updateMarioAnimation();
        else updateMarioAnimation(); // Fallback generic jump animation
    } else {
        drawStaticTime();
    }
}

void ArcadeClock::updateRyuAnimation() {
    drawStaticTime();
}

void ArcadeClock::updateMarioAnimation() {
    drawStaticTime();
}
