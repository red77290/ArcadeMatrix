#include "MessageEngine.h"
#include "fonts/ArcadeFonts.h"

MessageEngine::MessageEngine() : active(false), customFont(nullptr) {}

void MessageEngine::setCustomFont(GFXfont* font) {
    customFont = font;
}

EngineError MessageEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

void MessageEngine::activate() {
    active = true;
    startTime = millis();
    lastUpdate = millis();
}

void MessageEngine::deactivate() {
    active = false;
}

void MessageEngine::onConfigChanged(const EngineConfig* config) {
    if (config) {
        currentMsg.text = config->getString("text", "ArcadeMatrix");
        
        String colorStr = config->getString("color", "#ffffff");
        if (colorStr.startsWith("#")) colorStr = colorStr.substring(1);
        long val = strtol(colorStr.c_str(), NULL, 16);
        currentMsg.color = ((val >> 16) & 0xFF) << 11 | ((val >> 8) & 0xFF) << 5 | (val & 0xFF);
        
        currentMsg.size = config->getInt("size", 1);
        currentMsg.direction = config->getString("direction", "rtl");
        currentMsg.speed = config->getInt("speed", 50);
        currentMsg.timeoutSeconds = 0; // Infinite timeout when rotating
        
        // Font loading logic
        String fontSetting = config->getString("font", "Default");
        if (fontSetting.isEmpty() || fontSetting.equalsIgnoreCase("Default")) {
            fontSetting = config->getString("font_path", "");
        }

        if (fontSetting.equalsIgnoreCase("PressStart2P")) {
            customFont = (GFXfont*)&PressStart2P9pt7b;
        } else if (fontSetting.equalsIgnoreCase("namco")) {
            customFont = (GFXfont*)&namco__9pt7b;
        } else if (fontSetting.equalsIgnoreCase("FreeSansBold")) {
            customFont = (GFXfont*)&FreeSansBold9pt7b;
        } else if (fontSetting.equalsIgnoreCase("FreeMonoBold")) {
            customFont = (GFXfont*)&FreeMonoBold9pt7b;
        } else if (fontSetting.equalsIgnoreCase("RetroGaming")) {
            customFont = (GFXfont*)&Retro_Gaming9pt7b;
        } else if (fontSetting.endsWith(".amf") || fontSetting.startsWith("/")) {
            if (fontLoader.loadFromSD(fontSetting.c_str())) {
                customFont = fontLoader.getFont();
            } else {
                customFont = nullptr;
            }
        } else {
            fontLoader.unload();
            customFont = nullptr;
        }

        displayMessage(currentMsg);
    }
}

void MessageEngine::displayMessage(const MessageConfig& config) {
    currentMsg = config;
    active = true;
    startTime = millis();
    lastUpdate = millis();

    textWidth = currentMsg.text.length() * 6 * currentMsg.size;
    textHeight = 8 * currentMsg.size;

    if (currentMsg.direction == "rtl" || currentMsg.direction == "left") {
        cursorX = 999;
    } else if (currentMsg.direction == "ltr" || currentMsg.direction == "right") {
        cursorX = -textWidth;
    } else if (currentMsg.direction == "ttb" || currentMsg.direction == "down") {
        cursorY = -textHeight;
    } else if (currentMsg.direction == "btt" || currentMsg.direction == "up") {
        cursorY = 999;
    } else if (currentMsg.direction == "static") {
        cursorX = 999;
        cursorY = 999;
    }
}

void MessageEngine::update(EngineContext* context) {
    if (!active) return;
    
    auto* matrix = context->getMatrix();
    if (!matrix) return;

    if (currentMsg.timeoutSeconds > 0 && millis() - startTime > (currentMsg.timeoutSeconds * 1000)) {
        active = false;
        return;
    }

    // Dynamic initial bounds resolution
    if (currentMsg.direction == "static") {
        cursorX = (matrix->width() - textWidth) / 2;
        cursorY = (matrix->height() - textHeight) / 2;
        return;
    }

    if (cursorX == 999) cursorX = matrix->width();
    if (cursorY == 999) cursorY = matrix->height();
    if (currentMsg.direction == "rtl" || currentMsg.direction == "left" || currentMsg.direction == "ltr" || currentMsg.direction == "right") {
        if (cursorY == 999 || cursorY == -textHeight || cursorY > matrix->height()) {
            cursorY = (matrix->height() - textHeight) / 2;
        }
    } else {
        if (cursorX == 999 || cursorX == -textWidth || cursorX > matrix->width()) {
            cursorX = (matrix->width() - textWidth) / 2;
        }
    }
    
    // Scroll logic based on speed (ms per pixel update)
    if (millis() - lastUpdate > (unsigned long)currentMsg.speed) {
        lastUpdate = millis();

        if (currentMsg.direction == "rtl" || currentMsg.direction == "left") {
            cursorX--;
            if (cursorX < -textWidth) cursorX = matrix->width();
        } else if (currentMsg.direction == "ltr" || currentMsg.direction == "right") {
            cursorX++;
            if (cursorX > matrix->width()) cursorX = -textWidth;
        } else if (currentMsg.direction == "ttb" || currentMsg.direction == "down") {
            cursorY++;
            if (cursorY > matrix->height()) cursorY = -textHeight;
        } else if (currentMsg.direction == "btt" || currentMsg.direction == "up") {
            cursorY--;
            if (cursorY < -textHeight) cursorY = matrix->height();
        }
    }
}

void MessageEngine::render(EngineContext* context) {
    if (!active) return;
    auto* matrix = context->getMatrix();
    if (!matrix) return;

    matrix->clearScreen();
    matrix->setFont(customFont);
    matrix->setTextSize(currentMsg.size);
    matrix->setTextColor(currentMsg.color);
    matrix->setCursor(cursorX, cursorY);
    matrix->print(currentMsg.text);
}
