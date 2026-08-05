#include "MessageEngine.h"

MessageEngine::MessageEngine(MatrixPanel_I2S_DMA* display) : matrix(display), active(false), customFont(nullptr) {}

void MessageEngine::setCustomFont(GFXfont* font) {
    customFont = font;
}

void MessageEngine::displayMessage(const MessageConfig& config) {
    currentMsg = config;
    active = true;
    startTime = millis();
    lastUpdate = millis();

    // Setup text properties to calculate bounds
    matrix->setTextSize(currentMsg.size);
    // Rough estimation of text width (6 pixels width per character at size 1)
    textWidth = currentMsg.text.length() * 6 * currentMsg.size;
    textHeight = 8 * currentMsg.size;

    // Initialize starting position based on direction
    if (currentMsg.direction == "rtl") {
        cursorX = matrix->width();
        cursorY = (matrix->height() - textHeight) / 2;
    } else if (currentMsg.direction == "ltr") {
        cursorX = -textWidth;
        cursorY = (matrix->height() - textHeight) / 2;
    } else if (currentMsg.direction == "ttb") {
        cursorX = (matrix->width() - textWidth) / 2;
        cursorY = -textHeight;
    } else if (currentMsg.direction == "btt") {
        cursorX = (matrix->width() - textWidth) / 2;
        cursorY = matrix->height();
    } else {
        // Fallback default (center, static)
        cursorX = (matrix->width() - textWidth) / 2;
        cursorY = (matrix->height() - textHeight) / 2;
    }
}

bool MessageEngine::isActive() {
    return active;
}

void MessageEngine::stop() {
    active = false;
    matrix->clearScreen();
}

bool MessageEngine::loop() {
    if (!active) return true;

    // Check timeout priority
    if (millis() - startTime > (currentMsg.timeoutSeconds * 1000)) {
        stop();
        return true;
    }

    // Scroll logic based on speed (ms per pixel update)
    if (millis() - lastUpdate > currentMsg.speed) {
        lastUpdate = millis();

        matrix->clearScreen(); // Ensure black background for message priority overlay
        
        matrix->setFont(customFont); // nullptr falls back to the default 5x7 font
        matrix->setTextSize(currentMsg.size);
        matrix->setTextColor(currentMsg.color);
        matrix->setCursor(cursorX, cursorY);
        matrix->print(currentMsg.text);

        // Update coordinates
        if (currentMsg.direction == "rtl") {
            cursorX--;
            if (cursorX < -textWidth) cursorX = matrix->width(); // Loop
        } else if (currentMsg.direction == "ltr") {
            cursorX++;
            if (cursorX > matrix->width()) cursorX = -textWidth;
        } else if (currentMsg.direction == "ttb") {
            cursorY++;
            if (cursorY > matrix->height()) cursorY = -textHeight;
        } else if (currentMsg.direction == "btt") {
            cursorY--;
            if (cursorY < -textHeight) cursorY = matrix->height();
        }
    }
    return true;
}
