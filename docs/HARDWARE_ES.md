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

| Motor / Función | ESP32 Estándar (Sin PSRAM) | Waveshare ESP32-S3 (16MB PSRAM) | Requisito (`EngineRequirements`) |
|---|---|---|---|
| **Reloj** (30 Temas) | ✅ Activo | ✅ Activo | Ninguno |
| **Fecha** | ✅ Activo | ✅ Activo | Ninguno |
| **Clima** | ✅ Activo | ✅ Activo | `needsNetwork=true` |
| **Reproductor GIF** | ✅ Activo (Streaming SD) | ✅ Activo (Búfer PSRAM) | `needsSd=true` |
| **Overlay Fighter** | ✅ Activo (Modo 32px) | ✅ Activo (Modo 64px) | `needsSd=true` |
| **Temperatura y Humedad** | ⚠️ Activo si SHTC3 presente | ✅ Activo (SHTC3 integrado) | `needsTempSensor=true` |
| **Visualizador y Sonómetro** | ⚠️ Activo si micro I2S presente | ✅ Activo (ES7210 integrado) | `needsAudio=true` |
| **Ticker Cripto** | 🚫 Omitido (*Requiere PSRAM*) | ✅ Activo (Caché + Cotizaciones) | `needsPsram=true`, `needsNetwork=true` |
| **Ticker Bolsa** | 🚫 Omitido (*Requiere PSRAM*) | ✅ Activo (Caché + Cotizaciones) | `needsPsram=true`, `needsNetwork=true` |
| **Mensaje Personalizado** | ✅ Activo | ✅ Activo | Ninguno |
| **Marquee Alert** | ✅ Activo | ✅ Activo | Ninguno |

---

## 3. Consideraciones de Alimentación
- **Matriz LED RGB**: Paneles HUB75 / HUB75E (P2, P2.5, P3, P4, P5).
- **Fuente de alimentación 5V dedicada**: Una matriz de 64x32 puede consumir hasta 4A a brillo blanco máximo. **Alimente siempre la matriz directamente desde la fuente de 5V, NUNCA a través del pin 5V del ESP32.**
