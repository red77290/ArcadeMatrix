# Guía de cableado

🇬🇧 [English](WIRING.md) | 🇫🇷 [Français](WIRING_FR.md) | 🇪🇸 Español

Cablear una matriz LED HUB75 a un ESP32 requiere conexiones precisas. Los pines de datos de la matriz se mapean a los GPIO del ESP32.

## Pinout del ESP32 estándar (WROOM)
Este es el mapa de cableado predeterminado para el motor DMA.

| Pin HUB75 | GPIO ESP32 | Descripción |
|-----------|------------|-------------|
| R1        | 25         | Rojo superior |
| G1        | 26         | Verde superior |
| B1        | 27         | Azul superior |
| R2        | 14         | Rojo inferior |
| G2        | 12         | Verde inferior |
| B2        | 13         | Azul inferior |
| A         | 33         | Dirección A |
| B         | 32         | Dirección B |
| C         | 22         | Dirección C |
| D         | 17         | Dirección D |
| E         | 18         | Dirección E (para paneles de 64px de alto) |
| LAT (STB) | 4          | Latch |
| OE        | 15         | Output Enable |
| CLK       | 16         | Clock |

*Nota: el pin E solo es necesario si tu matriz tiene 64 píxeles de alto (por ejemplo, tasa de escaneo 1/32). En paneles de 32px de alto (escaneo 1/16), el pin `E` del panel normalmente no está conectado o está unido a GND en el propio panel, así que el GPIO18 queda dedicado exclusivamente al reloj SPI de la tarjeta SD de abajo - sin conflicto en esa configuración (muy habitual).*

## Cableado de la tarjeta SD (ambas placas)

La tarjeta SD usa un bus SPI independiente (`SPI.begin()` en `src/main.cpp`), separado de los
pines I2S/DMA dedicados de la matriz HUB75 anteriores.

| Pin SD | GPIO ESP32 | Descripción |
|--------|------------|-------------|
| CS     | 5          | Chip Select |
| SCK    | 18         | Reloj SPI |
| MISO   | 19         | Datos desde la tarjeta |
| MOSI   | 23         | Datos hacia la tarjeta |

ℹ️ El GPIO18 se comparte sobre el papel entre esta línea SCK de la SD y el pin de dirección
HUB75 `E` definido arriba, pero esto **no es un conflicto** en el caso común: en paneles de
32px de alto (escaneo 1/16), `E` no se usa/cablea hacia el ESP32 (a menudo unido a GND en el
propio panel), así que el GPIO18 queda libre para la tarjeta SD - esta es la configuración
probada/funcional. Solo si cableas un panel genuino de 64px de alto donde `E` esté realmente
conectado al GPIO18 compartirían ambos el mismo pin físico; en ese caso concreto, remapea la
línea SCK de la SD (`VSPI_SCK` en `src/main.cpp`) o el pin `E` del HUB75 (`_pins` en
`src/core/MatrixEngine.cpp`) a un GPIO libre antes de cablear ambos. Si tu tarjeta SD falla al
montarse (`sdWait Failed` / `sdSelectCard Failed` en el log serie), las causas más probables
son: la tarjeta no está formateada en FAT32, no está bien insertada, o tu fuente de alimentación
no puede alimentar la matriz + SD + Wi-Fi simultáneamente (el regulador integrado de una placa
ESP32 desnuda suele ser insuficiente para un panel completamente cableado - usa una fuente
dedicada de 5V/3A+ que alimente tanto el panel como el ESP32).

## Cableado ESP32-S3 Waveshare (100% probado y validado en hardware real)

La placa **Waveshare ESP32-S3 Matrix Board** (32MB Flash + 16MB PSRAM (N32R16)) integra su propio cableado HUB75 e interfaz SD_MMC de 1 bit. El perfil dedicado `HARDWARE_PROFILE_WAVESHARE_S3` en `include/HardwareProfile.h` (`pio run -e esp32s3_waveshare`) remapea automáticamente todos los pines para **evitar por completo el rango GPIO 33-37 reservado a la PSRAM Octal**.

**Esta configuración está 100% probada y verificada físicamente en hardware real con un funcionamiento fluido.**

### Pinout oficial Waveshare ESP32-S3 (HUB75 y SD_MMC)

| Señales HUB75 | GPIO ESP32-S3 | Señales Tarjeta SD (SD_MMC) | GPIO ESP32-S3 |
| :--- | :--- | :--- | :--- |
| **R1** | 4 | **D0** | 17 |
| **G1** | 5 | **CMD** | 44 |
| **B1** | 6 | **CLK** | 1 |
| **R2** | 7 | | |
| **G2** | 15 | | |
| **B2** | 16 | | |
| **A** | 18 | | |
| **B** | 8 | | |
| **C** | 3 | | |
| **D** | 42 | | |
| **E** | 9 | | |
| **LAT** | 40 | | |
| **OE** | 2 | | |
| **CLK** | 41 | | |

*Todos los pines HUB75 y SD están situados fuera del rango crítico GPIO 33-37 reservado por la PSRAM octal, lo que garantiza un funcionamiento perfecto en paneles de 256x64 y 128x32 sin ningún conflicto.*
