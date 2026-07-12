#include <Arduino.h>
#include <SD.h>

void testSDName() {
    File root = SD.open("/");
    File file = root.openNextFile();
    if (file) {
        Serial.print("TEST NAME: ");
        Serial.println(file.name());
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 2
        Serial.print("TEST PATH: ");
        Serial.println(file.path());
        #endif
    }
}
