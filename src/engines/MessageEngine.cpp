#include "MessageEngine.h"
#include "../core/drawing/MarqueeFitGeometry.h"
#include "fonts/ArcadeFonts.h"

MessageEngine::MessageEngine() : active(false), customFont(nullptr) {}

void MessageEngine::setCustomFont(GFXfont* font) {
    customFont = font;
    m_renderFont = font;
}

EngineError MessageEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (context) matrixDisplay = context->getSurface();
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
    // drop any message still parked for the render core, otherwise update() would re-activate us right after
    portENTER_CRITICAL(&pendingMux);
    pendingFlag = false;
    portEXIT_CRITICAL(&pendingMux);
    active = false;
}

void MessageEngine::resume() {
    lastUpdate = millis();
}

MessageEngine::ScrollDir MessageEngine::parseScrollDir(const String& dir) {
    if (dir.equalsIgnoreCase("rtl") || dir.equalsIgnoreCase("left")) return ScrollDir::RTL;
    if (dir.equalsIgnoreCase("ltr") || dir.equalsIgnoreCase("right")) return ScrollDir::LTR;
    if (dir.equalsIgnoreCase("ttb") || dir.equalsIgnoreCase("down")) return ScrollDir::TTB;
    if (dir.equalsIgnoreCase("btt") || dir.equalsIgnoreCase("up")) return ScrollDir::BTT;
    return ScrollDir::STATIC;
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

void MessageEngine::measureCurrentMessage(int matrixW, int matrixH) {
    const int sz = (currentMsg.size > 0) ? (int)currentMsg.size : 1;
    m_renderFont = customFont;

    auto measureDefaultFont = [&]() {
        const int len = (int)currentMsg.text.length();
        m_boundsX1 = 0;
        m_boundsY1 = 0;
        textWidth = (len > 0) ? ((len * 6 - 1) * sz) : 0;
        textHeight = 7 * sz;
    };

    if (m_renderFont != nullptr && matrixDisplay != nullptr) {
        int16_t x1 = 0, y1 = 0;
        uint16_t w = 0, h = 0;
        matrixDisplay->setFont(m_renderFont);
        matrixDisplay->setTextSize(sz);
        matrixDisplay->getTextBounds(currentMsg.text.c_str(), 0, 0, &x1, &y1, &w, &h);
        m_boundsX1 = x1;
        m_boundsY1 = y1;
        textWidth = (int)w;
        textHeight = (int)h;

        // If static text overflows display width with custom font, fall back to built-in 5x7 for THIS message
        // without permanently destroying `customFont` for subsequent shorter messages.
        if (m_configuredDir == ScrollDir::STATIC && textWidth > matrixW) {
            m_renderFont = nullptr;
            measureDefaultFont();
        }
    } else {
        m_renderFont = nullptr;
        measureDefaultFont();
    }

    // If static text still exceeds display width (e.g. 64x32 or rotated portrait matrix), auto-switch to scrolling
    m_effectiveDir = (m_configuredDir == ScrollDir::STATIC && textWidth > matrixW)
        ? ScrollDir::RTL
        : m_configuredDir;

    int centeredX = 0;
    int centeredY = 0;
    core::drawing::computeTextVisualCenter(
        matrixW, matrixH, m_boundsX1, m_boundsY1, textWidth, textHeight, currentMsg.offsetY,
        centeredX, centeredY
    );

    if (m_effectiveDir == ScrollDir::RTL) {
        cursorX = (float)(matrixW - m_boundsX1);
        cursorY = (float)centeredY;
    } else if (m_effectiveDir == ScrollDir::LTR) {
        cursorX = (float)(-textWidth - m_boundsX1);
        cursorY = (float)centeredY;
    } else if (m_effectiveDir == ScrollDir::TTB) {
        cursorX = (float)centeredX;
        cursorY = (float)(-textHeight - m_boundsY1);
    } else if (m_effectiveDir == ScrollDir::BTT) {
        cursorX = (float)centeredX;
        cursorY = (float)(matrixH - m_boundsY1);
    } else {
        cursorX = (float)centeredX;
        cursorY = (float)centeredY;
    }
}

void MessageEngine::displayMessage(const MessageConfig& config) {
    currentMsg = config;
    if (currentMsg.size == 0) currentMsg.size = 1;
    active = true;
    startTime = millis();
    lastUpdate = millis();

    int matrixW = matrixDisplay ? matrixDisplay->width() : 128;
    int matrixH = matrixDisplay ? matrixDisplay->height() : 64;
    m_lastSurfaceW = matrixW;
    m_lastSurfaceH = matrixH;
    m_configuredDir = parseScrollDir(currentMsg.direction);
    measureCurrentMessage(matrixW, matrixH);
}

void MessageEngine::queueMessage(const MessageConfig& config) {
    portENTER_CRITICAL(&pendingMux);
    pendingMsg = config;
    pendingFlag = true;
    portEXIT_CRITICAL(&pendingMux);
    active = true;   // so the arbiter routes the display to us; the text itself is applied on the render core
}

void MessageEngine::update(EngineContext* context) {
    if (pendingFlag) {
        MessageConfig cfg;
        portENTER_CRITICAL(&pendingMux);
        cfg = pendingMsg;
        pendingFlag = false;
        portEXIT_CRITICAL(&pendingMux);
        displayMessage(cfg);
    }
    if (!active) return;
    
    auto* matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return;
    matrixDisplay = matrix;

    const int curW = matrix->width();
    const int curH = matrix->height();
    if (curW != m_lastSurfaceW || curH != m_lastSurfaceH) {
        m_lastSurfaceW = curW;
        m_lastSurfaceH = curH;
        measureCurrentMessage(curW, curH);
    }

    if (currentMsg.timeoutSeconds > 0 && millis() - startTime > (currentMsg.timeoutSeconds * 1000)) {
        active = false;
        return;
    }

    int centeredX = 0;
    int centeredY = 0;
    core::drawing::computeTextVisualCenter(
        curW, curH, m_boundsX1, m_boundsY1, textWidth, textHeight, currentMsg.offsetY,
        centeredX, centeredY
    );

    if (m_effectiveDir == ScrollDir::STATIC) {
        cursorX = (float)centeredX;
        cursorY = (float)centeredY;
        lastUpdate = millis();
        return;
    }
    
    uint32_t now = millis();
    float dt = (float)(now - lastUpdate);
    lastUpdate = now;
    if (dt > 100.0f) dt = 16.6f; // Clamped to prevent jumps on pause or stall
    if (dt <= 0.0f) return;

    float speedMs = (float)max(1, currentMsg.speed);
    float movePx = dt / speedMs;

    if (m_effectiveDir == ScrollDir::RTL) {
        cursorX -= movePx;
        if ((cursorX + (float)m_boundsX1 + (float)textWidth) < 0.0f) cursorX = (float)(curW - m_boundsX1);
        cursorY = (float)centeredY;
    } else if (m_effectiveDir == ScrollDir::LTR) {
        cursorX += movePx;
        if ((cursorX + (float)m_boundsX1) > (float)curW) cursorX = (float)(-textWidth - m_boundsX1);
        cursorY = (float)centeredY;
    } else if (m_effectiveDir == ScrollDir::TTB) {
        cursorY += movePx;
        if ((cursorY + (float)m_boundsY1) > (float)curH) cursorY = (float)(-textHeight - m_boundsY1);
        cursorX = (float)centeredX;
    } else if (m_effectiveDir == ScrollDir::BTT) {
        cursorY -= movePx;
        if ((cursorY + (float)m_boundsY1 + (float)textHeight) < 0.0f) cursorY = (float)(curH - m_boundsY1);
        cursorX = (float)centeredX;
    }
}

void MessageEngine::render(EngineContext* context) {
    if (!active) return;
    auto* matrix = context ? context->getSurface() : nullptr;
    if (!matrix) return;

    matrix->fillScreen(0);
    matrix->setFont(m_renderFont);
    matrix->setTextSize(currentMsg.size > 0 ? currentMsg.size : 1);
    matrix->setTextColor(currentMsg.color);
    matrix->setCursor((int16_t)roundf(cursorX), (int16_t)roundf(cursorY));
    matrix->print(currentMsg.text);
}

EngineDescriptor MessageEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_msg;
    desc_msg.metadata = {"message", "Message", "display", FIRMWARE_VERSION};
    desc_msg.capabilities.realtime = true;
    desc_msg.requirements.targetFps = 60;
    desc_msg.requirements.prefersDoubleBuffer = true;
    desc_msg.requirements.supportsSingleBuffer = true;
    desc_msg.requirements.internalPersistentBytes = 5000;
    desc_msg.schema.fields = {
        ConfigField("text", ConfigType::STRING, "Message Text", "Text banner or message to display", "ArcadeMatrix", true, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("color", ConfigType::COLOR, "Text Color", "Hex color code (#RRGGBB)", "#ffffff", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("size", ConfigType::INTEGER, "Font Size", "Text scale multiplier", "1", false, "1", "4", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("direction", ConfigType::ENUM, "Direction", "Scroll direction or static", "rtl", false, "", "", "", "rtl,ltr,ttb,btt,static", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("speed", ConfigType::INTEGER, "Speed (ms)", "Scroll delay per step (lower is faster)", "50", false, "10", "200", "5", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("font", ConfigType::ENUM, "Font", "Display typeface", "Default", false, "", "", "", "", "/api/fonts", false, "", ValidationPolicy::FallbackDefault)
    };
    desc_msg.factory = []() { return std::unique_ptr<IEngine>(new MessageEngine()); };
    return desc_msg;
}

