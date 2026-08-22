#include "MessageEngine.h"

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

void MessageEngine::onConfigChanged(const EngineConfig* engineConfig) {}

void MessageEngine::displayMessage(const MessageConfig& config) {
    currentMsg = config;
    active = true;
    startTime = millis();
    lastUpdate = millis();

    // Default sizing setup - bounds will be relative to actual display later
    textWidth = currentMsg.text.length() * 6 * currentMsg.size;
    textHeight = 8 * currentMsg.size;

    // We can't set cursor fully without matrix bounds, so we defer dynamic pos to update/render
    // But we will reset it slightly out of bounds based on direction convention here.
    if (currentMsg.direction == "rtl" || currentMsg.direction == "left") {
        cursorX = 999; // Will snap in update()
    } else if (currentMsg.direction == "ltr" || currentMsg.direction == "right") {
        cursorX = -textWidth;
    } else if (currentMsg.direction == "ttb" || currentMsg.direction == "down") {
        cursorY = -textHeight;
    } else if (currentMsg.direction == "btt" || currentMsg.direction == "up") {
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
    if (cursorX == 999) cursorX = matrix->width();
    if (cursorY == 999) cursorY = matrix->height();
    if (currentMsg.direction == "rtl" || currentMsg.direction == "left" || currentMsg.direction == "ltr" || currentMsg.direction == "right") {
        if (cursorY == 999 || cursorY == -textHeight || cursorY > matrix->height()) { // Not set yet or was vertical
            cursorY = (matrix->height() - textHeight) / 2;
        }
    } else { // vertical
        if (cursorX == 999 || cursorX == -textWidth || cursorX > matrix->width()) {
            cursorX = (matrix->width() - textWidth) / 2;
        }
    }
    
    matrix->setFont(customFont); // nullptr falls back to the default 5x7 font
    // Scroll logic based on speed (ms per pixel update)
    if (millis() - lastUpdate > currentMsg.speed) {
        lastUpdate = millis();

        // Update coordinates
        if (currentMsg.direction == "rtl" || currentMsg.direction == "left") {
            cursorX--;
            if (cursorX < -textWidth) cursorX = matrix->width(); // Loop
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
