# Guía para desarrolladores (ESP32)

🇬🇧 [English](DEVELOPER.md) | 🇫🇷 [Français](DEVELOPER_FR.md) | 🇪🇸 Español

Bienvenido a la guía de desarrollo ESP32 de ArcadeMatrix. Este documento explica cómo ampliar el proyecto en C++, en particular cómo añadir un nuevo reloj y cómo exponerlo a la API web.

---

## 1. Añadir un nuevo reloj especializado

Debido a la arquitectura monolítica de la versión ESP32, añadir un reloj significa crear una clase C++ que gestione su propia lógica y su propio dibujo sobre el hardware.

### Paso a paso

1. **Crea el header (`include/MyClock.h`):**
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

2. **Crea la implementación (`src/MyClock.cpp`):**
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

3. **Registra el reloj en `ClockEngine.cpp`:**
   - Incluye tu header al principio de `src/ClockEngine.cpp`.
   - Instancia tu reloj dinámicamente (o como variable miembro) dentro de `ClockEngine` según el tema seleccionado por el usuario.
   - Ejemplo dentro de `ClockEngine::loop()`:
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

## 2. Modificar la API web y la configuración

Si tu nuevo reloj necesita nuevos ajustes de usuario (por ejemplo, `snake_speed`), debes modificar toda la canalización de configuración, desde el frontend hasta la struct.

1. **Actualiza la struct (`src/core/ConfigLoader.h`):**
   Añade tu nueva variable a la struct principal `Config`.
   ```cpp
   struct Config {
       // ... existing fields
       int snake_speed = 5;
   };
   ```

2. **Actualiza el parser y el generador JSON (`src/api/WebServerAPI.cpp`):**
   La API se comunica mediante JSON (`ArduinoJson`).
   - Busca el método que serializa la configuración para enviarla al navegador y añade:
     `doc["snake_speed"] = config.snake_speed;`
   - Busca el método que analiza el JSON entrante desde el navegador y añade:
     `if (doc.containsKey("snake_speed")) config.snake_speed = doc["snake_speed"].as<int>();`

3. **Actualiza el cargador del sistema de archivos (`src/core/ConfigLoader.cpp`):**
   Asegúrate de que tu nueva variable se lea del archivo `conf.ini` de la tarjeta SD y se guarde allí para sobrevivir a los reinicios.

4. **Actualiza la interfaz web (`src/api/WebUI.h`, generada desde el frontend Vue; consulta `scripts/build_webui.py`):**
   - Añade los inputs HTML para tu ajuste.
   - Actualiza el JS del frontend para enviar tu nueva variable en el payload JSON cuando el usuario haga clic en "Save".

### Endpoints REST destacados (lista no exhaustiva)

| Endpoint | Método | Propósito |
|----------|--------|-----------|
| `/api/status` | GET | Uptime, heap libre / mínimo libre, estadísticas de PSRAM. |
| `/api/settings` | GET/POST | Lectura/escritura completa de la configuración (persiste en `conf.ini`). |
| `/api/wifi` | POST | `{ssid, password}` - guarda las credenciales e intenta una reconexión inmediata, informando sincrónicamente del éxito o fallo (no requiere reinicio). |
| `/api/marquee` | POST | Cuerpo de imagen RGB565 bruto (little-endian, row-major, exactamente `width*height*2` bytes que deben coincidir con la resolución configurada del panel; consulta `tools/mugen_extractor` para la misma convención de formato). Lo muestra inmediatamente durante ~8 s, interrumpiendo la rotación en reposo, y luego la reanuda. No hay decodificador de imágenes en el dispositivo, así que cualquier integración bridge/frontend debe preconvertir el artwork (PNG/JPEG/box-art) a este formato bruto antes de hacer el POST. |
| `/api/update` | POST | Subida OTA del firmware (`Update.h`), escribe en el slot de partición OTA inactivo. |

### Integración marquee/box-art estilo Pixelcade (frontends de cabinas arcade)

A diferencia de `ArcadeMatrix_RPi` (que descarga artwork marquee de Pixelcade en vivo desde GitHub bajo demanda,
consulta su `core/dmd_cache.py`), el ESP32 no tiene presupuesto sobrante de flash/RAM/CPU para un cliente HTTPS
que obtenga imágenes en mitad de una partida, ni una caché en disco ilimitada que pueda crecer con el tiempo. La
arquitectura recomendada, en cambio, precachea todo el artwork **en la tarjeta SD por adelantado**, de modo que
mostrarlo en tiempo de ejecución sea solo una búsqueda rápida de archivo local; no interviene ninguna red cuando
se lanza un juego:

1. **Configuración única:** ejecuta `tools/pixelcade_sync/pixelcade_sync.sh` (macOS/Linux) o
   `pixelcade_sync.ps1` (Windows) en tu PC (no en el ESP32) para
   descargar el repositorio de artwork de Pixelcade y organizarlo como `/pixelcade/<system>/<game>.png`.
   Copia el resultado a tu tarjeta SD. Consulta el README de esa herramienta para filtrar solo los sistemas
   que usas (el repositorio completo ocupa varios cientos de MB).
2. **Configuración única:** ejecuta `tools/recalbox_daemon/install.sh` (macOS/Linux) o `install.ps1`
   (Windows) desde tu PC para instalar por SSH un pequeño daemon de eventos en tu dispositivo
   Recalbox/Batocera; te pide ambas IP, sin necesidad de abrir una sesión SSH manual. Es el *mismo*
   protocolo de daemon que usa `ArcadeMatrix_RPi` (`core/ssh_installer.py`), así que una única instalación
   sirve para ambos proyectos.
3. En tiempo de ejecución, el daemon publica `{"status": "playing"|"browsing"|"stopped", "game": "<rom
   basename>", "system": "<SystemId>"}` por MQTT en `recalbox/system/playing` (o
   `batocera/system/playing`) cada vez que cambia el juego seleccionado/en ejecución.
4. `RetroFrontendListener::handleGameEvent()` (firmware) lo analiza, mapea el `SystemId` a un nombre de
   carpeta Pixelcade (`mapSystemToPixelcadeFolder()`, mantenido en sincronía con el `SYSTEM_MAP` de
   `dmd_cache.py` en la RPi), y comprueba `/pixelcade/<folder>/<game>.png` en la tarjeta SD:
   - Si existe: lo muestra inmediatamente mediante `gif->playGif()` (decodificador PNG de GifEngine, añadido
     junto con el soporte GIF; consulta `esp32-gif-png` en el changelog).
   - Si no existe (aún no sincronizado, o Pixelcade no tiene arte para ese juego): vuelve a desplazar
     el nombre del juego como texto mediante `MessageEngine`, reproduciendo el comportamiento de la RPi cuando falla su caché.
   - En `"status": "stopped"`: llama a `gif->stop()`, reanudando la rotación inactiva de GIF/reloj.

Esto mantiene el 100 % de la obtención / decodificación / redimensionado de imágenes fuera del camino de tiempo
real por completo (se hace una sola vez, offline, en un PC con ancho de banda real y sin restricciones de memoria),
siguiendo la misma filosofía de «preconvertir offline» que los assets `.raw` de GifEngine y `tools/mugen_extractor`.

**Rutas heredadas / alternativas que siguen soportadas:**
- `/api/marquee` (POST, cuerpo RGB565 bruto) sigue disponible para scripts bridge que quieran enviar
  directamente una imagen arbitraria que no sea Pixelcade (por ejemplo, un marquee generado a medida) en lugar de
  depender de la búsqueda Pixelcade cacheada en la SD descrita arriba.
- El topic nativo `/Recalbox/EmulationStation/Event` (`rungame`/`stop`) también sigue suscrito como
  fallback básico para instalaciones que no quieran instalar el daemon personalizado, aunque no aporta detalles
  del juego/sistema (Recalbox no los incluye en ese topic), así que solo puede mostrarse un placeholder genérico.
- El protocolo bridge de texto plano `STOP_GAME`/`START_GAME:<path>` de configuraciones antiguas también sigue funcionando.

---

### Cargar una fuente bitmap personalizada desde la SD (BitmapFontLoader)

Por defecto, todas las fuentes están compiladas dentro del firmware (`src/engines/fonts/`). Para añadir una
fuente personalizada sin recompilar el firmware:

1. Obtén o crea una fuente bitmap BDF. Cualquiera de las fuentes incluidas en el proyecto RPi
   (`ArcadeMatrix_RPi/fonts/*.bdf`) sirve como punto de partida, o bien toma una de un archivo de fuentes BDF X11/X.
2. Conviértela al formato `.amf` de ArcadeMatrix:
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py myfont.bdf myfont.amf
   ```
   Por defecto cubre el ASCII imprimible (`0x20`-`0x7E`); pasa `--first`/`--last` para cubrir un
   rango distinto de codepoints si la fuente BDF lo tiene (por ejemplo, Latin-1 extendido).
3. Copia `myfont.amf` a la tarjeta SD, por ejemplo `/fonts/myfont.amf`.
4. Establece `custom_font_path=/fonts/myfont.amf` bajo `[fonts]` en `conf.ini` (o mediante `/api/settings`).
5. Reinicia. `BitmapFontLoader::loadFromSD()` analiza el archivo al arrancar en una estructura compatible
   con `GFXfont` asignada en heap y se la entrega a `MessageEngine` para el banner `/api/message`
   (la fuente 5x7 predeterminada se usa silenciosamente como fallback si el archivo falta o está corrupto).

**Notas del formato:** `.amf` refleja exactamente la disposición de memoria de las fuentes compiladas de Adafruit
(offsets de bitmap alineados por bytes por glifo, bits empaquetados MSB-first); consulta el docstring de
`tools/bdf_to_amfont/bdf_to_amfont.py` para ver el layout binario exacto, y el parser lector correspondiente del lado ESP32
en `BitmapFontLoader.cpp`. Las fuentes están limitadas a 65535 bytes de datos bitmap de glifos empaquetados
(`GFXglyph.bitmapOffset` es un `uint16_t`), el mismo techo que impone la propia herramienta `fontconvert` de
Adafruit a las fuentes compiladas, no una limitación nueva.

---

## 3. Reglas importantes para el desarrollo en ESP32

- **Evita los objetos `String`:** usa arrays de `char` (`char[]`) siempre que sea posible para evitar la fragmentación del heap, algo fatal en ESP32.
- **Límites DMA:** nunca dibujes fuera de los límites de `matrix_width` y `matrix_height`. Adafruit GFX gestiona la mayor parte del clipping, pero las escrituras directas en memoria provocarán kernel panics.
- **Fugas de memoria:** si asignas dinámicamente clases (`new MyClock()`), asegúrate de hacer `delete` cuando el tema cambie para evitar agotar la memoria.
