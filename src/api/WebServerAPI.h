/**
 * @file WebServerAPI.h
 * @brief REST API and Web Interface Server.
 * 
 * Provides an asynchronous web server for the ESP32 that hosts the Vite/VanillaJS
 * frontend and exposes a JSON REST API for controlling the matrix state.
 */
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include "../core/SpiRamJsonDocument.h"
#include "../core/ConfigLoader.h"
#include "../core/TimingSafe.h"
#include "../engines/MessageEngine.h"
#include "../engines/MarqueeEngine.h"
#include "../engines/VisualizerEngine.h"
#include "../hal/HardwareHAL.h"

/**
 * @class WebServerAPI
 * @brief Handles HTTP requests and serves the embedded Web UI.
 */
class WebServerAPI {
public:
    /**
     * @brief Construct a new WebServerAPI object.
     * 
     * @param port The HTTP port to listen on (typically 80).
     * @param msgEngine Pointer to the MessageEngine for triggering marquee alerts.
     */
    WebServerAPI(uint16_t port, MessageEngine* msgEngine);
    
    /**
     * @brief Attach the MarqueeEngine used by the /api/marquee route.
     */
    void setMarqueeEngine(MarqueeEngine* engine);

    /**
     * @brief Attach the VisualizerEngine used by the /api/visualizer route.
     */
    void setVisualizerEngine(VisualizerEngine* engine);

    /**
     * @brief Initialize the web server, register routes, and start listening.
     */
    void begin();
    
private:
    AsyncWebServer server; ///< Underlying ESPAsyncWebServer instance
    MessageEngine* msg;    ///< Reference to the MessageEngine
    MarqueeEngine* marquee = nullptr; ///< Reference to the MarqueeEngine
    VisualizerEngine* visualizer = nullptr; ///< Reference to the VisualizerEngine
    
    /**
     * @brief Setup all REST API and Static File routes.
     */
    void setupRoutes();
    
    /**
     * @brief Helper to serialize and send a JSON response.
     * 
     * @param request The active HTTP request.
     * @param doc The JsonDocument to serialize and send.
     */
    void sendJsonResponse(AsyncWebServerRequest *request, JsonDocument& doc);

public:
    /**
     * @brief Timing-safe string comparison to prevent side-channel timing attacks.
     * 
     * @param a First string.
     * @param b Second string.
     * @return true if strings match, false otherwise.
     */
    static inline bool timingSafeCompare(const String& a, const String& b) {
        return TimingSafe::compare(a, b);
    }

    /**
     * @brief Checks whether the given HTTP request is authorized against the configured API token.
     * 
     * @param request The active HTTP request.
     * @return true if authorized (or auth disabled), false otherwise.
     */
    static bool isRequestAuthorized(AsyncWebServerRequest* request);
};
