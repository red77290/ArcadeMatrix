# Requisitos de hardware

🇬🇧 [English](HARDWARE.md) | 🇫🇷 [Français](HARDWARE_FR.md) | 🇪🇸 Español

## Modelos ESP32
El firmware ArcadeMatrix es compatible con placas ESP32 estándar, pero los requisitos cambian según el tamaño de tu matriz:

### 128x32 o menor (uso estándar)
- **Placa:** ESP32 WROOM estándar (por ejemplo NodeMCU ESP32S, ESP32 Dev Module).
- **RAM:** la SRAM estándar es totalmente suficiente.
- **Cableado:** usa los pines VSPI/HSPI estándar. Consulta [WIRING_ES.md](WIRING_ES.md) para ver el pinout.

### 256x64 o mayor (uso avanzado)
- **Placa:** **ESP32-S3 con PSRAM** (por ejemplo ESP32-S3 WROOM-1 N8R8).
- **RAM:** la PSRAM es **OBLIGATORIA** para el doble búfer a 24 bits de profundidad de color en paneles grandes.
- **¿Por qué?** Una pantalla 256x64 requiere ~98KB por frame. El doble búfer necesita ~200KB de RAM DMA contigua, algo que el ESP32 estándar no puede proporcionar de forma fiable mientras mantiene el Wi-Fi y el servidor web. El ESP32-S3 descarga esto de forma transparente a la PSRAM o dispone de suficientes bloques contiguos para evitar cuelgues OOM (Out Of Memory).

### ESP32-S3 Waveshare (Soporte 100% probado y validado en hardware real)
La placa **Waveshare ESP32-S3 Matrix Board** (32MB Flash + 16MB PSRAM (N32R16)) es **100% compatible y verificada físicamente en hardware real**. 
El perfil dedicado `HARDWARE_PROFILE_WAVESHARE_S3` (`pio run -e esp32s3_waveshare`) remapea los pines HUB75 a GPIOs libres (A=18, B=8, C=3, D=42, E=9) y utiliza la interfaz SD_MMC de 1 bit de alta velocidad (CMD=44, CLK=1, D0=17), eliminando cualquier conflicto con la PSRAM octal. Todo funciona de forma impecable sin ningún conflicto de GPIO. Consulta [WIRING_ES.md](WIRING_ES.md) para ver la tabla completa de pines.

## Múltiples paneles: encadenado vs verdaderas rejillas/muros 2D (runtime vs compile-time)
El build RPi (`ArcadeMatrix_RPi`) usa la biblioteca `rpi-rgb-led-matrix`, que expone `--led-chain`,
`--led-parallel` y `--led-rows` como flags **totalmente configurables en tiempo de ejecución**. Una Raspberry Pi tiene 2-3 cabeceras GPIO HUB75 independientes, así que construir un muro 2D de paneles (por ejemplo 2 filas x 2 columnas) es solo un cambio de configuración, sin recompilar.

Las placas ESP32 solo exponen una **única** salida HUB75. Este firmware ya admite `CHAIN=N` en
`conf.ini` (`ConfigLoader::matrix.chainLength`) para encadenar paneles **en una sola fila** en tiempo de ejecución
(por ejemplo `CHAIN=4` para una cinta 512x32). Esto funciona hoy y no requiere cambios de firmware.

**Las verdaderas rejillas/muros 2D (múltiples filas de paneles encadenados, por ejemplo un muro 2x2) NO están actualmente integrados en este firmware.** La biblioteca subyacente `ESP32-HUB75-MatrixPanel-I2S-DMA` sí incluye un helper
`VirtualMatrixPanel_T` que remapea coordenadas virtuales (x,y) sobre un encadenado serpentino/zig-zag de paneles para
construir ese tipo de muro, pero es una **clase template de C++**: su forma de cadena y su tipo de escaneo son
parámetros **de compilación**, no algo que pueda leerse desde `conf.ini` al arrancar como el resto de ajustes del
proyecto. Integrarlo correctamente requeriría:
1. Un flag / entorno PlatformIO dedicado por layout de muro (recompilar + reflashear para cambiar el layout), o
2. Refactorizar todos los motores (~46 call sites) desde el tipo concreto `MatrixPanel_I2S_DMA*` hacia una interfaz común basada en `Adafruit_GFX*`, para poder sustituir una instancia `VirtualMatrixPanel_T`.

Ambas opciones son nada triviales y representan un hueco arquitectónico real frente al soporte runtime completo de `--led-parallel` en la RPi. Se sigue como limitación conocida en lugar de ignorarse en silencio. El encadenado en una sola fila mediante `CHAIN=` sigue siendo hoy la forma soportada de construir una pantalla mayor.

### Uso de la flash
El firmware nunca usa SPIFFS/LittleFS: todos los assets de runtime (GIF, sprites de luchadores, playlists,
`conf.ini`) viven en la tarjeta SD externa. Por eso el entorno PlatformIO `esp32dev` usa
`board_build.partitions = min_spiffs.csv` en lugar de la tabla de particiones predeterminada de Arduino-ESP32. Esto
mantiene el mismo layout OTA de doble banco (dos slots de app, así que `/api/update` sigue funcionando) pero
amplía cada slot de app de 1.25MB a ~1.875MB recuperando la partición SPIFFS de ~900KB, que si no quedaría desperdiciada. En el momento de escribir esto, el firmware `esp32dev` usa ~66 % de su slot de app (frente al 98 %+ antes de este cambio), un margen cómodo para las próximas funciones de PNG / iconos meteorológicos.

## Hardware de la matriz
- **Tipo:** paneles de matriz LED RGB HUB75 / HUB75E (P2, P2.5, P3, P4, P5).
- **Chips driver:** compatibles con registros de desplazamiento estándar (FM6126A, ICN2038S, etc.).
- **Fuente de alimentación:** se requiere una fuente dedicada de 5V. Una matriz 64x32 puede consumir hasta 4 A con brillo blanco total. **¡No alimentes la matriz directamente desde el ESP32!**
