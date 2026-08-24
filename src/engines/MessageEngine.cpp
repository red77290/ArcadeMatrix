#include "MessageEngine.h"
#include "fonts/ArcadeFonts.h"

MessageEngine::MessageEngine() : active(false), customFont(nullptr) {}

void MessageEngine::setCustomFont(GFXfont* font) {
    customFont = font;
}

EngineError MessageEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (context) matrixDisplay = context->getMatrix();
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

void MessageEngine::activate() {
    active = true;
    startTime = millis();
    lastUpdate = millis();
    displayMessage(currentMsg);
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

    if (customFont != nullptr && matrixDisplay != nullptr) {
        int16_t x1 = 0, y1 = 0;
        uint16_t w = 0, h = 0;
        matrixDisplay->setFont(customFont);
        matrixDisplay->setTextSize(currentMsg.size);
        matrixDisplay->getTextBounds(currentMsg.text.c_str(), 0, 0, &x1, &y1, &w, &h);
        textWidth = w;
        textHeight = h;
        baselineOffset = (y1 < 0) ? -y1 : textHeight;
    } else {
        textWidth = currentMsg.text.length() * 6 * currentMsg.size;
        textHeight = 8 * currentMsg.size;
        baselineOffset = 0;
    }

    int matrixW = matrixDisplay ? matrixDisplay->width() : 128;
    int matrixH = matrixDisplay ? matrixDisplay->height() : 64;

    if (currentMsg.direction == "rtl" || currentMsg.direction == "left") {
        cursorX = matrixW;
        cursorY = ((matrixH - textHeight) / 2) + baselineOffset;
    } else if (currentMsg.direction == "ltr" || currentMsg.direction == "right") {
        cursorX = -textWidth;
        cursorY = ((matrixH - textHeight) / 2) + baselineOffset;
    } else if (currentMsg.direction == "ttb" || currentMsg.direction == "down") {
        cursorX = (matrixW - textWidth) / 2;
        cursorY = -textHeight + baselineOffset;
    } else if (currentMsg.direction == "btt" || currentMsg.direction == "up") {
        cursorX = (matrixW - textWidth) / 2;
        cursorY = matrixH + baselineOffset;
    } else if (currentMsg.direction == "static" || currentMsg.direction == "none") {
        cursorX = (matrixW - textWidth) / 2;
        cursorY = ((matrixH - textHeight) / 2) + baselineOffset;
    }
}

void MessageEngine::update(EngineContext* context) {
    if (!active) return;
    
    auto* matrix = context->getMatrix();
    if (!matrix) return;
    matrixDisplay = matrix;

    if (currentMsg.timeoutSeconds > 0 && millis() - startTime > (currentMsg.timeoutSeconds * 1000)) {
        active = false;
        return;
    }

    if (currentMsg.direction == "static" || currentMsg.direction == "none") {
        cursorX = (matrix->width() - textWidth) / 2;
        cursorY = ((matrix->height() - textHeight) / 2) + baselineOffset;
        return;
    }
    
    // Scroll logic based on speed (ms per pixel update)
    if (millis() - lastUpdate > (unsigned long)currentMsg.speed) {
        lastUpdate = millis();

        if (currentMsg.direction == "rtl" || currentMsg.direction == "left") {
            cursorX--;
            if (cursorX < -textWidth) cursorX = matrix->width();
            cursorY = ((matrix->height() - textHeight) / 2) + baselineOffset;
        } else if (currentMsg.direction == "ltr" || currentMsg.direction == "right") {
            cursorX++;
            if (cursorX > matrix->width()) cursorX = -textWidth;
            cursorY = ((matrix->height() - textHeight) / 2) + baselineOffset;
        } else if (currentMsg.direction == "ttb" || currentMsg.direction == "down") {
            cursorY++;
            if (cursorY > matrix->height() + baselineOffset) cursorY = -textHeight + baselineOffset;
            cursorX = (matrix->width() - textWidth) / 2;
        } else if (currentMsg.direction == "btt" || currentMsg.direction == "up") {
            cursorY--;
            if (cursorY < -textHeight + baselineOffset) cursorY = matrix->height() + baselineOffset;
            cursorX = (matrix->width() - textWidth) / 2;
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

EngineDescriptor MessageEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_msg;
    desc_msg.metadata = {"message", "Message", "display", FIRMWARE_VERSION};
    desc_msg.capabilities.realtime = true;
    desc_msg.schema.fields = {
        ConfigField("text", ConfigType::STRING, "Message Text", "Text banner or message to display", "ArcadeMatrix", true, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("color", ConfigType::COLOR, "Text Color", "Hex color code (#RRGGBB)", "#ffffff", false, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("size", ConfigType::INTEGER, "Font Size", "Text scale multiplier", "1", false, "1", "4", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("direction", ConfigType::ENUM, "Direction", "Scroll direction or static", "rtl", false, "", "", "", "rtl,ltr,ttb,btt,static", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("speed", ConfigType::INTEGER, "Speed (ms)", "Scroll delay per step (lower is faster)", "50", false, "10", "200", "5", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("font", ConfigType::ENUM, "Font", "Display typeface", "Default", false, "", "", "", "", "/api/fonts", false, "", ValidationPolicy::FallbackDefault)
    };
    desc_msg.factory = []() { return std::unique_ptr<IEngine>(new MessageEngine()); };
    return desc_msg;
}

