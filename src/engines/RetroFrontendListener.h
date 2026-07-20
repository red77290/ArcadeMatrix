#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "ConfigLoader.h"
#include "GifEngine.h"
#include "ClockEngine.h"

class RetroFrontendListener {
public:
    RetroFrontendListener(MqttConfig& config, GifEngine* gifEngine, ClockEngine* clockEngine);
    void begin();
    void loop();

private:
    MqttConfig& mqttConfig;
    GifEngine* gif;
    ClockEngine* clock;
    WiFiClient espClient;
    PubSubClient mqttClient;

    unsigned long lastReconnectAttempt;

    void reconnect();
    static void callback(char* topic, byte* payload, unsigned int length);
    static RetroFrontendListener* instance;
    
    void handleMessage(String topic, String message);
};
