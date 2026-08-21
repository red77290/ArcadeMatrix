# Guía para desarrolladores (ESP32)

🇬🇧 [English](DEVELOPER.md) | 🇫🇷 [Français](DEVELOPER_FR.md) | 🇪🇸 Español

Bienvenido a la guía de desarrollo ESP32 de ArcadeMatrix. Este documento explica cómo ampliar el proyecto en C++, en particular cómo añadir un nuevo reloj y cómo exponerlo a la API web.

---

## 1. Añadir un nuevo reloj especializado

Gracias a la arquitectura modular acoplada al hardware de ArcadeMatrix, añadir un nuevo reloj consiste en derivar o instanciar una clase de visualización bajo `src/engines/clocks/` que gestione su propia lógica y dibujo directo.

### Matriz de Compatibilidad de Temas de Reloj (IDs 0 a 29)

| ID | Tema / Reloj | Clase / Archivo | 128x32 | 256x64 | Fuente Personalizada (.amf) | Colores Personalizados | Estado en Hardware |
|---|---|---|:---:|:---:|:---:|:---:|:---:|
| 0 | Nintendo | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 1 | Capcom | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 2 | Taito | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 3 | Sega | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 4 | Cave | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 5 | Konami | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 6 | SNK | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 7 | Technos | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 8 | IGS | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 9 | Hudson | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 10 | Banpresto | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 11 | Namco | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 12 | Ryu | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 13 | Mario | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 14 | Marco | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 15 | Megaman | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 16 | Bub | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 17 | Space Invaders | `ArcadeClock` | ✓ | ✓ | — | — | Validado en hardware |
| 18 | Cyberpunk | `CyberpunkClock` | ✓ | ✓ | — | — | Validado en hardware |
| 19 | FlipClock | `FlipClock` | ✓ | ✓ | — | — | Validado en hardware |
| **20** | **Custom Gradient** | `ClockEngine` | **✓** | **✓** | **✓** | **✓** | **Validado en hardware** |
| 21 | PongClock | `PongClock` | ✓ | ✓ | — | — | Validado en hardware |
| 22 | TetrisClock | `TetrisClock` | ✓ | ✓ | — | — | Validado en hardware |
| 23 | TetrisGameboy | `TetrisGameboyClock` | ✓ | ✓ | — | — | Validado en hardware |
| 24 | PacmanClock | `PacmanClock` | ✓ | ✓ | — | — | Validado en hardware |
| 25 | WordClock | `WordClock` | ✓ | ✓ | — | — | Validado en hardware |
| 26 | BinaryClock | `BinaryClock` | ✓ | ✓ | — | — | Validado en hardware |
| 27 | VersusClock | `VersusClock` | ✓ | ✓ | — | — | Validado en hardware |
| 28 | SlotMachineClock | `SlotMachineClock` | ✓ | ✓ | — | — | Validado en hardware |
| 29 | MatrixRainClock | `MatrixRainClock` | ✓ | ✓ | — | — | Validado en hardware |

---

### Paso a paso

1. **Crea el header (`src/engines/clocks/MyClock.h`):**
   ```cpp
   #pragma once
   #include <Arduino.h>
   #include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
   #include "core/ConfigLoader.h"

   class MyClock {
   public:
       MyClock(MatrixPanel_I2S_DMA* display, ConfigLoader* config);
       void loop(); // Llamado en cada cuadro
   private:
       MatrixPanel_I2S_DMA* _display;
       ConfigLoader* _config;
   };
   ```

2. **Crea la implementación (`src/engines/clocks/MyClock.cpp`):**
   ```cpp
   #include "MyClock.h"

   MyClock::MyClock(MatrixPanel_I2S_DMA* display, ConfigLoader* config) : _display(display), _config(config) {}

   void MyClock::loop() {
       if (!_display) return;
       _display->fillScreen(0);
       _display->drawPixel(10, 10, _display->color565(255, 0, 0));
   }
   ```

3. **Registra el reloj en `ClockEngine.cpp`:**
   - Incluye tu header al principio de `src/engines/ClockEngine.cpp`.
   - Instancia tu reloj según el tema `clock_theme` seleccionado por el usuario.
   - Ejecuta la llamada en `ClockEngine::loop()`.

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
   Asegúrate de que tu nueva variable se lea del archivo `config.json` de la tarjeta SD y se guarde allí para sobrevivir a los reinicios.

4. **Actualiza la interfaz web (`src/api/WebUI.h`, generada desde el frontend Vue; consulta `scripts/build_webui.py`):**
   - Añade los inputs HTML para tu ajuste.
   - Actualiza el JS del frontend para enviar tu nueva variable en el payload JSON cuando el usuario haga clic en "Save".

### Endpoints REST destacados (lista no exhaustiva)

| Endpoint | Método | Propósito |
|----------|--------|-----------|
| `/api/status` | GET | Uptime, heap libre / mínimo libre, estadísticas de PSRAM. |
| `/api/settings` | GET/POST | Lectura/escritura completa de la configuración (persiste en `config.json`). |
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
4. `FrontendSyncEngine::handleGameEvent()` (firmware) lo analiza, mapea el `SystemId` a un nombre de
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
4. Establece `custom_font_path=/fonts/myfont.amf` bajo `[fonts]` en `config.json` (o mediante `/api/settings`).
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

---

## 4. Limitaciones Conocidas y Seguridad

### Seguridad
- **API sin autenticación**: La API REST HTTP y el servidor WebUI se ejecutan sin autenticación en la red local. ArcadeMatrix está diseñado para usarse en una red de confianza (LAN privada). No expongas el puerto 80 directamente a Internet sin un reverse proxy con autenticación.
- **CORS**: Las cabeceras `Access-Control-Allow-Origin: *` están habilitadas por defecto para permitir el control desde aplicaciones web locales.

### Limitaciones Conocidas
- **Sin Rollback OTA Automático**: El proceso de actualización OTA escribe en la partición secundaria y cambia el flag del bootloader. Si se inicia un firmware funcionalmente defectuoso, no existe un mecanismo de restauración automática sin reflash físico o WebSerial.
- **Consultas de Red Sincrónicas en los Providers**: Aunque `AsyncWebServer` procesa las peticiones HTTP de forma asíncrona, las actualizaciones de cotizaciones de los proveedores (`CryptoEngine`, `StockEngine`, `WeatherEngine`) realizan peticiones de red de forma sincrónica con caché local en memoria. En caso de fallo de red, se conserva el último valor en caché sin bloquear el bucle principal de pantalla.
- **Tarjeta SD Requerida para GIF y MUGEN**: La reproducción de GIFs animados y sprites MUGEN requiere estrictamente una tarjeta microSD formateada en FAT32 o exFAT.

---

## 5. Tutorial: Añadir un Nuevo Módulo de Rotación (ej. Pager/Noticias)

Si deseas añadir un nuevo módulo de visualización (por ejemplo, "Pager" para noticias) al bucle de rotación, sigue estos pasos desde la UI hasta el backend.

### Paso 1: La Interfaz Web
Los selectores de rotación se definen en `api/www/index.html`. Añade tu nuevo módulo como una casilla de verificación:
```html
<label class="toggle-checkbox">
  <input type="checkbox" value="pager">
  <span class="toggle-label">Pager</span>
</label>
```
El JS (`app.js`) lee automáticamente todas las casillas marcadas y las envía como una cadena (ej. `rotation: "clock,gifs,pager"`) al endpoint `/api/settings`.

### Paso 2: La Configuración (`config.json`)
En `src/core/ConfigLoader.cpp`, la cadena se lee y se guarda en la tarjeta SD:
```cpp
// En ConfigLoader::parseConfig() bajo "[IDLE]"
if (key == "ROTATION") idle.rotation = value;

// Y para guardarla en ConfigLoader::saveConfig()
out += "ROTATION=" + idle.rotation + "\n";
```
*Nota: Al almacenarse como cadena, `idle.rotation` guardará nativamente `"clock,gifs,pager"` sin necesidad de modificar el analizador.*

### Paso 3: El Motor (Engine)
Crea un nuevo motor (ej. `src/engines/PagerEngine.h`).
```cpp
#pragma once
#include "core/Matrix.h"

class PagerEngine {
public:
    void init() {
        // Configurar cliente HTTP para obtener noticias
    }
    
    void draw() {
        matrix->clearScreen();
        // Dibujar UI usando matrix->setCursor() y matrix->print()
    }
    
    void stop() {
        // Liberar memoria
    }
};
```

### Paso 4: El Bucle de Rotación
En `src/core/RotationManager.cpp` (o `main.cpp`), la cadena de rotación se divide. Cuando le toca reproducirse al pager:
```cpp
if (currentModule == "pager") {
    pagerEngine->init();
    // En el main loop()
    pagerEngine->draw();
}
```


## Ciclo de vida moderno de los motores (Lazy-Once)
A partir de la refactorización de Paridad de Arquitectura S13, ArcadeMatrix utiliza un ciclo de vida "Lazy-Once" estricto para los motores tanto en ESP32 como en RPi:
- **`initialize(context, config)`**: Se llama **exactamente una vez** la primera vez que el administrador de rotación selecciona el motor. Haga las asignaciones de memoria importantes aquí.
- **`activate()`**: Se llama cada vez que el motor gira a la vista.
- **`update(context) / render(context)`**: Se llama a 60 FPS. **No debe asignar memoria**.
- **`deactivate()`**: Se llama cuando se gira hacia otro motor.
- **`on_config_changed(config)`**: Se llama cuando el usuario cambia la configuración a través de la interfaz de usuario web mientras el motor ya está inicializado.
- **`is_finished()`**: Usado por la rotación para omitir condicionalmente motores si no tienen trabajo pendiente.
