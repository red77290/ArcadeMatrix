# Requisitos de Hardware y Matriz de Capacidades

[English](HARDWARE.md) | 🇫🇷 [Français](HARDWARE_FR.md) | 🇪🇸 Español

## 1. Perfiles de Placa Compatibles

ArcadeMatrix admite tres destinos de firmware en dos niveles de capacidad de hardware:

### 1.1 ESP32 Estándar (`esp32dev`)
- **Microcontrolador**: ESP32 de doble núcleo clásico (NodeMCU, ESP32 Dev Module).
- **RAM**: ~320 KB de SRAM interna (sin PSRAM).
- **Almacenamiento**: Tarjeta SD por SPI (VSPI: `CS=5, SCK=18, MISO=19, MOSI=23`).
- **Resolución de Matriz**: Hasta 128x32 o 64x64.
- **Audio / Sensores**: Módulos externos opcionales INMP441 / SHTC3.

### 1.2 ESP32-S3 Genérico N16R8 / N8R8 (`esp32s3_n16r8`)
- **Microcontrolador**: ESP32-S3 de doble núcleo con **Flash Quad SPI** (`8 MB` o `16 MB`, `QIO`) + **PSRAM Octal** (`8 MB`, `OPI`) — p. ej. `ESP32-S3-DevKitC-1-N16R8` o `N8R8`.
- **Detección Dinámica de Memoria**: Detecta automáticamente el tamaño físico de Flash (`8 MB` / `16 MB` / `32 MB`) y PSRAM en el arranque.
- **Almacenamiento**: `SD_MMC` de 1 bit de alta velocidad (`CLK=1`, `CMD/MOSI=44` [serigrafiado como **`RX`** en placas DevKitC-1], `D0/MISO=17`; conectar `DAT3/CS` a `3.3V` mediante una resistencia de `10kΩ`).
- **Resolución de Matriz**: Hasta 256x64 (doble búfer DMA SPIRAM soportado).

### 1.3 Waveshare ESP32-S3 Matrix Board (`esp32s3_waveshare`)
- **Microcontrolador**: ESP32-S3 de doble núcleo (`N32R16V`) con **32 MB Flash Octal (`OPI`)** + **16 MB PSRAM Octal (`OPI`)**.
- **Almacenamiento**: `SD_MMC` de 1 bit de alta velocidad (`D0=17, CMD=44, CLK=1`).
- **Periféricos integrados**: Códec de audio I2S ES7210 + micrófono digital, DAC ES8311, sensor de temperatura/humedad SHTC3.
- **Resolución de Matriz**: Hasta 256x64 (doble búfer DMA SPIRAM soportado).

### 1.4 Pinout ESP32-S3 (`esp32s3_n16r8` y `esp32s3_waveshare`)

| Subsistema | Señal (SD_MMC / Etiqueta Adaptador SPI) | GPIO ESP32-S3 | Notas |
|---|---|---|---|
| **Matriz HUB75** | `R1, G1, B1` | `GPIO 4, 5, 6` | Datos RGB mitad superior |
| **Matriz HUB75** | `R2, G2, B2` | `GPIO 7, 15, 16` | Datos RGB mitad inferior |
| **Matriz HUB75** | `A, B, C, D, E` | `GPIO 18, 8, 3, 42, 9` | Líneas de dirección de fila (`E` en paneles de 64 filas) |
| **Matriz HUB75** | `LAT, OE, CLK` | `GPIO 40, 2, 41` | Latch, Output Enable, Reloj de píxel |
| **MicroSD (SD_MMC 1-bit)** | `CLK` (`CLK` / `SCK`) | `GPIO 1` | Reloj SDMMC por hardware |
| **MicroSD (SD_MMC 1-bit)** | `CMD` (**`MOSI`** en adaptadores SPI) | `GPIO 44` (**`RX`** en DevKitC-1) | Comando / Respuesta (pull-up `10kΩ` a `3.3V`) |
| **MicroSD (SD_MMC 1-bit)** | `DAT0 / D0` (**`MISO`** en adaptadores SPI) | `GPIO 17` | Línea de datos 0 (pull-up `10kΩ` a `3.3V`) |
| **MicroSD (SD_MMC 1-bit)** | `DAT3 / CS` (**`CS`** en adaptadores SPI) | `3.3V` | Pull-up a `3.3V` con resistencia de `10kΩ` (o dejar sin conectar si el adaptador 3.3V ya incluye resistencias pull-up de `10kΩ`) |
| **Bus I2C** | `SDA, SCL` | `GPIO 47, 48` | Sensores opcionales `SHTC3` (`0x70`), `QMI8658` (`0x6B`), `MPU6050` (`0x68`) |
| **Audio I2S** | `MCLK, BCLK, WS, DIN, DOUT` | `GPIO 12, 43 (`TX`), 38, 39, 21` | ES7210/ES8311 integrados o Micro/DAC I2S externo |

---

## 2. Matriz de Disponibilidad de Motores

| Motor / Función | ID Canónico | ESP32 Estándar (Sin PSRAM) | Waveshare ESP32-S3 (16MB PSRAM) | Requisito (`EngineRequirements`) |
|---|---|---|---|---|
| **Reloj** (36 Temas) | `clock` | ✅ Activo | ✅ Activo | Ninguno |
| **Fecha** | `date` | ✅ Activo | ✅ Activo | Ninguno |
| **Clima** | `weather` | ✅ Activo | ✅ Activo | `needsNetwork=true` |
| **Reproductor GIF** | `gifs` | ✅ Activo (Streaming SD) | ✅ Activo (Búfer PSRAM) | `needsSd=true` |
| **Ticker Cripto** | `crypto` | ✅ Activo (Profundidad dinámica 4-bit + precarga) | ✅ Activo (8-bit + Caché) | `needsNetwork=true`, `needsTls=true` |
| **Ticker Bolsa** | `stock` | ✅ Activo (Profundidad dinámica 4-bit + precarga) | ✅ Activo (8-bit + Caché) | `needsNetwork=true`, `needsTls=true` |
| **Visualizador de Audio** | `audiovisualizer` | ⚠️ Activo si micro I2S presente | ✅ Activo (ES7210 integrado) | `needsAudio=true` |
| **Sonómetro de Nivel Acústico** | `decibelMeter` | ⚠️ Activo si micro I2S presente | ✅ Activo (ES7210 integrado) | `needsAudio=true` |
| **Temperatura y Humedad** | `temp` | ⚠️ Activo si SHTC3 presente | ✅ Activo (SHTC3 integrado) | `needsTempSensor=true` |
| **Mensaje Personalizado** | `message` | ✅ Activo | ✅ Activo | Ninguno |
| **Google Cast / Nest** | `google_cast` | ✅ Activo | ✅ Activo | `needsNetwork=true` |
| **Reproductor Spotify** | `spotify` | ✅ Activo (Profundidad dinámica 4-bit) | ✅ Activo | `needsNetwork=true`, `needsTls=true` |
| **Monitor de Sistema** | `system_info` | ✅ Activo | ✅ Activo | Ninguno |
| **Reproductor Musical Universal** | `music_player` | 🚫 Omitido (*Requiere PSRAM y Salida de Audio*) | ✅ Activo | `needsPsram=true`, `needsAudioOutput=true` |
| **Dashboard Multi-Widgets** | `dashboard` | ✅ Activo (Profundidad dinámica 4-bit + precarga) | ✅ Activo | Ninguno (Red/Sensor opcionales) |
| **Ticker GNews en Vivo** | `gnews` | ✅ Activo (Profundidad dinámica 4-bit + caché SD) | ✅ Activo | `needsNetwork=true`, `needsTls=true` |
| **Marquee Arcade** | `marquee` | ✅ Activo | ✅ Activo | Ninguno |
| **Datos MQTT** | `mqttdata` | ✅ Activo (Hasta 4 páginas) | ✅ Activo (Hasta 6 páginas) | `needsNetwork=true` |
| **Overlay Fighter** *(Transversal)* | `fighter` *(overlay)* | ✅ Activo (Modo 32px) | ✅ Activo (Modo 64px) | `needsSd=true` |

---

## 3. Consideraciones de Alimentación
- **Matriz LED RGB**: Paneles HUB75 / HUB75E (P2, P2.5, P3, P4, P5).
- **Fuente de alimentación 5V dedicada**: Una matriz de 64x32 puede consumir hasta 4A a brillo blanco máximo. **Alimente siempre la matriz directamente desde la fuente de 5V, NUNCA a través del pin 5V del ESP32.**
