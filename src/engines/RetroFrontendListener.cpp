#include "RetroFrontendListener.h"

RetroFrontendListener* RetroFrontendListener::instance = nullptr;

RetroFrontendListener::RetroFrontendListener(MqttConfig& config, GifEngine* gifEngine, ClockEngine* clockEngine) 
    : mqttConfig(config), gif(gifEngine), clock(clockEngine), mqttClient(espClient) {
    instance = this;
    lastReconnectAttempt = 0;
}

void RetroFrontendListener::begin() {
    if (!mqttConfig.enabled || mqttConfig.broker.isEmpty()) return;
    
    mqttClient.setServer(mqttConfig.broker.c_str(), mqttConfig.port);
    mqttClient.setCallback(RetroFrontendListener::callback);
}

void RetroFrontendListener::loop() {
    if (!mqttConfig.enabled) return;
    
    if (!mqttClient.connected()) {
        long now = millis();
        // Increase reconnect delay to 30 seconds (30000ms) to avoid lagging the main matrix
        // loop every 5 seconds if the MQTT broker is offline or unreachable.
        if (now - lastReconnectAttempt > 30000) {
            lastReconnectAttempt = now;
            reconnect();
        }
    } else {
        mqttClient.loop();
    }
}

void RetroFrontendListener::reconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Attempting MQTT connection...");
        if (mqttClient.connect(mqttConfig.deviceName.c_str(), mqttConfig.user.c_str(), mqttConfig.pass.c_str())) {
            Serial.println("connected");
            mqttClient.subscribe("batocera/events");
            mqttClient.subscribe("/Recalbox/EmulationStation/Event"); // Official Recalbox topic
            // Add other frontend topics here (e.g., retropie)
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
        }
    }
}

void RetroFrontendListener::callback(char* topic, byte* payload, unsigned int length) {
    if (!instance) return;
    
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    instance->handleMessage(String(topic), message);
}

void RetroFrontendListener::handleMessage(String topic, String message) {
    Serial.printf("Frontend Event: [%s] %s\n", topic.c_str(), message.c_str());
    
    // Recalbox natively publishes lowercase events to /Recalbox/EmulationStation/Event
    // Examples: "rungame", "stop", "shutdown"
    
    if (topic == "/Recalbox/EmulationStation/Event") {
        if (message == "stop" || message == "stopgame") {
            gif->stop();
        } else if (message == "rungame") {
            // Recalbox puts extra game details in /tmp/es_state.inf on the Pi,
            // but normally users write a small python bridge script to send the exact system/game path over MQTT.
            // For now, we trigger a generic response or read the payload if the user uses a bridge script.
            gif->playGif("/gifs/recalbox_generic.raw"); // Placeholder
        }
    } else {
        // Custom Batocera or Recalbox bridge script logic
        if (message == "STOP_GAME") {
            gif->stop();
        } else if (message.startsWith("START_GAME:")) {
            String gifPath = message.substring(11);
            gif->playGif(gifPath.c_str());
        }
    }
}
