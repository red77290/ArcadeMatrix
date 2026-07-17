# Developer Guide (ESP32)

Welcome to the ArcadeMatrix ESP32 development guide. This document explains how to extend the C++ project, specifically how to add a new Clock and how to expose it to the Web API.

---

## 1. Adding a New Specialized Clock

Because of the monolithic architecture of the ESP32 version, adding a clock means creating a C++ class that handles its own logic and its own hardware drawing.

### Step-by-Step

1. **Create the Header (`include/MyClock.h`)**:
   ```cpp
   #pragma once
   #include <Arduino.h>
   #include "MatrixDisplay.h" // Your HUB75 matrix wrapper
   #include "ConfigLoader.h"

   class MyClock {
   public:
       MyClock(MatrixDisplay* display, Config* config);
       void loop(); // Called every frame
   private:
       MatrixDisplay* _display;
       Config* _config;
       
       // Your state variables
       int _snakeLength;
   };
   ```

2. **Create the Implementation (`src/MyClock.cpp`)**:
   ```cpp
   #include "MyClock.h"

   MyClock::MyClock(MatrixDisplay* display, Config* config) {
       _display = display;
       _config = config;
       _snakeLength = 3;
   }

   void MyClock::loop() {
       // 1. Clear screen or draw background
       _display->fillScreen(0);

       // 2. Execute logic
       // ...

       // 3. Draw directly using Adafruit GFX primitives
       _display->drawPixel(10, 10, _display->color565(255, 0, 0));
   }
   ```

3. **Register the Clock in `ClockEngine.cpp`**:
   - Include your header at the top of `src/ClockEngine.cpp`.
   - Instantiate your clock dynamically (or as a member variable) inside the `ClockEngine` based on the user's selected theme.
   - Example inside `ClockEngine::loop()`:
     ```cpp
     switch (_config->time_theme) {
         case 22:
             if (!_myClock) _myClock = new MyClock(_display, _config);
             _myClock->loop();
             break;
         // ...
     }
     ```

---

## 2. Modifying the Web API & Configuration

If your new clock requires new user settings (e.g., `snake_speed`), you must modify the configuration pipeline from the frontend all the way to the struct.

1. **Update the Struct (`include/ConfigLoader.h`)**:
   Add your new variable to the main `Config` struct.
   ```cpp
   struct Config {
       // ... existing fields
       int snake_speed = 5;
   };
   ```

2. **Update the JSON Parser & Generator (`src/WebServerAPI.cpp`)**:
   The API communicates via JSON (`ArduinoJson`). 
   - Find the method that serializes the config to send to the browser and add: 
     `doc["snake_speed"] = config.snake_speed;`
   - Find the method that parses incoming JSON from the browser and add:
     `if (doc.containsKey("snake_speed")) config.snake_speed = doc["snake_speed"].as<int>();`

3. **Update the File System Loader (`src/ConfigLoader.cpp`)**:
   Ensure your new variable is read from and saved to the SD card's `config.json` file to survive reboots.

4. **Update the Web UI (`data/index.html` & `data/main.js`)**:
   - Add the HTML inputs for your setting.
   - Update `main.js` to send your new variable in the JSON payload when the user clicks "Save".

---

## 3. Important Rules for ESP32 Development

- **Avoid `String` objects**: Use `char` arrays (`char[]`) whenever possible to avoid heap fragmentation, which is fatal on ESP32.
- **DMA Bounds**: Never draw outside the boundaries of `matrix_width` and `matrix_height`. Adafruit GFX handles most clipping, but direct memory writes will cause kernel panics.
- **Memory Leaks**: If you dynamically allocate classes (`new MyClock()`), ensure you `delete` them when the theme switches to prevent memory exhaustion.
