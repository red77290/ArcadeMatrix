# Requisitos de Hardware y Matriz de Capacidades

[English](HARDWARE.md) | 🇫🇷 [Français](HARDWARE_FR.md) | 🇪🇸 Español

## 1. Perfiles de Placa Compatibles

ArcadeMatrix admite dos perfiles de placa físicos:

### 1.1 ESP32 Estándar (`esp32dev`)
- **Microcontrolador**: ESP32 de doble núcleo clásico (NodeMCU, ESP32 Dev Module).
- **RAM**: ~320 KB de SRAM interna (sin PSRAM).
- **Almacenamiento**: Tarjeta SD por SPI (VSPI).
- **Resolución de Matriz**: Hasta 128x32 o 64x64.
- **Audio / Sensores**: Módulos externos opcionales INMP441 / SHTC3.

### 1.2 Waveshare ESP32-S3 Matrix Board (`esp32s3_waveshare`)
- **Microcontrolador**: ESP32-S3 de doble núcleo con 32 MB Flash + 16 MB de PSRAM Octal.
- **Almacenamiento**: SD_MMC de 1 bit de alta velocidad (D0=17, CMD=44, CLK=1).
- **Periféricos integrados**: Códec de audio I2S ES7210 + micrófono digital, sensor de temperatura/humedad SHTC3.
- **Resolución de Matriz**: Hasta 256x64 (doble búfer DMA SPIRAM soportado).

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
