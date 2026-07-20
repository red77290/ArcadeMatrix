#include "ArcadeClock.h"
#include "../../core/ConfigLoader.h"
#include "../fonts/ArcadeFonts.h"

ArcadeClock::ArcadeClock(MatrixPanel_I2S_DMA* display) : ClockFace(display) {
    lastMinute = 255;
    isAnimating = false;
    animationFrame = 0;
    lastFrameTime = 0;
    extern ConfigLoader config;
    currentTheme = static_cast<PublisherTheme>(config.time.clock_theme);
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

void ArcadeClock::drawTextWithShadow(int x, int y, uint16_t textColor, uint16_t shadowColor) {
    extern ConfigLoader config;
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    // setTextSize and font are already set by drawStaticTime
    
    // Arcade 3D Outline Effect
    // Draw thick shadow
    matrix->setTextColor(shadowColor);
    matrix->setCursor(x + 2, y + 2); matrix->print(timeStr);
    matrix->setCursor(x + 1, y + 2); matrix->print(timeStr);
    matrix->setCursor(x + 2, y + 1); matrix->print(timeStr);
    
    // Draw outline (black)
    uint16_t outline = matrix->color565(0, 0, 0);
    matrix->setTextColor(outline);
    matrix->setCursor(x - 1, y); matrix->print(timeStr);
    matrix->setCursor(x + 1, y); matrix->print(timeStr);
    matrix->setCursor(x, y - 1); matrix->print(timeStr);
    matrix->setCursor(x, y + 1); matrix->print(timeStr);
    
    // Draw inner text
    matrix->setTextColor(textColor);
    matrix->setCursor(x, y);
    matrix->print(timeStr);
}

void ArcadeClock::drawStaticTime() {
    extern ConfigLoader config;
    int logicalSize = config.time.clock_size > 0 ? config.time.clock_size : 1;
    
    const GFXfont* font9pt = nullptr;
    const GFXfont* font12pt = nullptr;
    
    switch (config.time.clock_font) {
        case THEME_NINTENDO: case THEME_HUDSON: font9pt = &FreeSansBold9pt7b; font12pt = &FreeSansBold12pt7b; break;
        case THEME_SEGA: font9pt = &FreeMonoBold9pt7b; font12pt = &FreeMonoBold12pt7b; break;
        case THEME_CAVE: case THEME_SNK: case THEME_TECHNOS: font9pt = &PressStart2P9pt7b; font12pt = &PressStart2P12pt7b; break;
        case THEME_TAITO: case THEME_KONAMI: font9pt = &Retro_Gaming9pt7b; font12pt = &Retro_Gaming12pt7b; break;
        case THEME_CAPCOM: case THEME_IGS: case THEME_BANPRESTO: case THEME_NAMCO: case THEME_RYU: case THEME_MEGAMAN: case THEME_MARIO: case THEME_BUB: font9pt = &namco__9pt7b; font12pt = &namco__12pt7b; break;
        default: font9pt = nullptr; font12pt = nullptr; break;
    }
    
    matrix->setTextSize(1);
    matrix->setFont(font9pt);
    int16_t bx, by;
    uint16_t bw, bh;
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", storedTime.hours, storedTime.minutes, storedTime.seconds);
    matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
    
    if (bw == 0 || bh == 0) { bw = 48; bh = 7; } // Fallback
    
    int maxScaleW = matrix->width() / bw;
    int maxScaleH = matrix->height() / bh;
    int sMax = min(maxScaleW, maxScaleH);
    
    bool fallbackToSmall = false;
    if (sMax < 1) {
        sMax = 1;
        fallbackToSmall = true; // The font is inherently too big for this screen!
    }
    
    int gfxSize = 1;
    bool use12pt = false;
    
    if (logicalSize == 3) {
        gfxSize = sMax;
        // If sMax > 1, size 3 can just use 9pt scaled up.
    } else if (logicalSize == 2) {
        gfxSize = max(1, (sMax * 2) / 3);
        // If the user wants a smaller size, but sMax=1 (so it's already at scale 1), we must use the smaller default font
        if (gfxSize == 1 && sMax == 1) fallbackToSmall = true;
    } else {
        gfxSize = max(1, sMax / 3);
        if (gfxSize == 1 && sMax <= 2) fallbackToSmall = true;
    }
    
    if (fallbackToSmall) {
        matrix->setFont(nullptr); // Use default 5x7 font
        // Re-calculate sMax for the default font
        matrix->setTextSize(1);
        matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
        if (bw == 0 || bh == 0) { bw = 48; bh = 7; }
        
        int sMaxDefault = min(matrix->width() / bw, matrix->height() / bh);
        if (sMaxDefault < 1) sMaxDefault = 1;
        
        if (logicalSize == 3) gfxSize = sMaxDefault;
        else if (logicalSize == 2) gfxSize = max(1, (sMaxDefault * 2) / 3);
        else gfxSize = max(1, sMaxDefault / 3);
        
        matrix->setTextSize(gfxSize);
    } else {
        matrix->setFont(font9pt);
        matrix->setTextSize(gfxSize);
    }
    
    matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
    
    // Safety check: if it STILL overflows, force it to smallest possible font
    if (bw > matrix->width()) {
        matrix->setFont(nullptr);
        matrix->setTextSize(1);
        matrix->getTextBounds("88:88:88", 0, 0, &bx, &by, &bw, &bh);
    }
    
    int x = (matrix->width() - bw) / 2 + config.time.clock_offset_x - bx;
    int y = (matrix->height() - bh) / 2 - by + config.time.clock_offset_y;
    
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

        case THEME_NONE:
        default:
            textColor = matrix->color565(255, 255, 255);
            shadowColor = matrix->color565(0, 0, 0);
            break;
    }
    
    drawTextWithShadow(x, y, textColor, shadowColor);
}

void ArcadeClock::triggerAnimation() {
    isAnimating = true;
    animationFrame = 0;
    lastFrameTime = millis();
}

void ArcadeClock::update() {
    if (isAnimating) {
        if (millis() - lastFrameTime > 50) {
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
