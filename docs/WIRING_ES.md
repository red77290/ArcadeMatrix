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

## Cableado ESP32-S3

⚠️ **Conflicto conocido, aún no revalidado sobre hardware real:** el firmware usa actualmente el **mismo**
mapa de pines en ESP32-S3 que en el ESP32 clásico (consulta la tabla superior), definido una sola vez en `src/MatrixEngine.cpp`.
Esto es un problema específicamente para paneles 256x64, porque ese es justo el caso en el que se requiere PSRAM octal
(N8R8/N16R8), y la PSRAM octal en ESP32-S3 reserva internamente **GPIO 33-37**, lo que entra
directamente en conflicto con el pin `A` (GPIO 33) y queda junto al pin `B` (GPIO 32) de arriba. El firmware
muestra una advertencia en tiempo de ejecución sobre esto (consulta `MatrixEngine::begin`), pero el mapa de pines en sí todavía no se ha
actualizado / verificado frente a un cableado físico ESP32-S3. Si vas a cablear un panel 256x64 en ESP32-S3,
comprueba los GPIO disponibles en tu placa concreta antes de confiar en el mapa predeterminado, y plantéate
remapear `A`/`B` (y volver a probar) a GPIO fuera del rango 33-37.

### Referencia de disponibilidad de GPIO en ESP32-S3

Esta tabla resume qué GPIO son seguros para usar con señales HUB75 en un módulo ESP32-S3, según el modo de PSRAM.
Compruébalo siempre contra la hoja de datos de tu placa específica, ya que algunos devkits también conectan GPIO adicionales
a periféricos integrados (USB, botones, LED RGB, etc.).

| Rango GPIO | Estado | Notas |
|------------|--------|-------|
| 0, 3, 45, 46 | **Reservado (strapping pins)** | Se usan al arrancar para seleccionar el modo; evita conducirlos directamente. |
| 19, 20 | Reservado (USB) | USB nativo D-/D+ en la mayoría de los devkits S3. |
| 26-32 | **Reservado (Quad Flash/PSRAM)** | Siempre reservados en ESP32-S3, independientemente del modo PSRAM. |
| 33-37 | **Reservado solo en modo PSRAM octal (« opi »)** | Libres si tu módulo no tiene PSRAM o usa PSRAM Quad (« qio »). **Entra en conflicto con el pin A actual del firmware (33) y está junto al pin B (32) cuando se usa PSRAM octal**; consulta la advertencia anterior. |
| 1-2, 4-18, 21, 38-48 | Generalmente libres | Pool recomendado para remapear `A`/`B` (y cualquier otra señal en conflicto) fuera de 33-37 al usar PSRAM octal. |

**Acción recomendada para builds 256x64 (PSRAM octal) en ESP32-S3:** remapea los pines `A` y `B` en la struct `_pins` de `MatrixEngine.cpp` a dos GPIO del grupo « generalmente libres » de arriba (por ejemplo 38/39),
recablea en consecuencia y elimina / valida la advertencia en tiempo de ejecución cuando quede confirmado sobre hardware real.
Esto aún no se ha hecho en este codebase: trata el cableado predeterminado S3 256x64 como **no verificado**.
