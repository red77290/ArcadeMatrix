# Guía de cableado y Trampas de Integración de Hardware

🇬🇧 [English](WIRING.md) | 🇫🇷 [Français](WIRING_FR.md) | 🇪🇸 Español

Cablear una matriz LED HUB75 y periféricos de almacenamiento a un ESP32 requiere una precisión absoluta. Dado que HUB75 utiliza transferencias DMA continuas y el ESP32 multiplexa sus GPIOs a través de una matriz de pines flexible (GPIO Matrix), una asignación de pines incorrecta o conflictos de librerías pueden provocar colisiones graves de bus, fallos en el montaje de la tarjeta SD o bucles de reinicio por watchdog.

ArcadeMatrix admite oficialmente dos perfiles de hardware distintos:
1. **ESP32 Estándar (`esp32dev`)** — ESP32-WROOM / ESP32-D0WD clásico (Hardware DMDos Board V3 / RetroPixelLED).
2. **Waveshare ESP32-S3 RGB Matrix (`esp32s3_waveshare`)** — ESP32-S3 con 16 MB de PSRAM Octal + 32 MB de Flash.

---

## 1. ESP32 Estándar (`esp32dev` / DMDos Board V3)

Este perfil está diseñado para placas ESP32 clásicas con 320 KB de SRAM interna, utilizando DMA HUB75 estándar y una tarjeta MicroSD conectada mediante el bus por hardware VSPI.

### 1.1 Pinout de la Matriz LED HUB75

| Pin HUB75 | GPIO ESP32 | Descripción | Notas de hardware |
| :--- | :--- | :--- | :--- |
| **R1** | 25 | Rojo superior | Línea de datos |
| **G1** | 26 | Verde superior | Línea de datos |
| **B1** | 27 | Azul superior | Línea de datos |
| **R2** | 14 | Rojo inferior | Línea de datos |
| **G2** | 12 | Verde inferior | ⚠️ *Pin de strapping (MTDI). No debe estar en HIGH durante el arranque en placas con flash de 3.3V.* |
| **B2** | 13 | Azul inferior | Línea de datos |
| **A** | 33 | Dirección A | Línea de dirección |
| **B** | 32 | Dirección B | Línea de dirección |
| **C** | 22 | Dirección C | Línea de dirección |
| **D** | 17 | Dirección D | Línea de dirección |
| **E** | **GND / -1** | Dirección E | **Paneles de 32px (escaneo 1/16): Conectar a GND.** Paneles de 64px: GPIO 21. |
| **LAT (STB)**| 4 | Latch | Pestillo de reloj |
| **OE** | 15 | Output Enable | Blanking PWM activo en bajo. ⚠️ *Pin de strapping (MTDO).* |
| **CLK** | 16 | Reloj de Matriz | Reloj DMA paralelo |

> [!CAUTION]
> **TRAMPA CRÍTICA: ¡NUNCA ASIGNES EL PIN E DE HUB75 AL GPIO 18!**
> El GPIO 18 está estrictamente reservado para el reloj SPI de la MicroSD (`VSPI_SCK`).
> En paneles estándar de 64x32 (escaneo 1/16), el pin `E` **no se utiliza** y debe conectarse a **GND** en el lado del panel (el software pasa `-1`).
> ¡Asignar el pin E al GPIO 18 secuestrará la línea de reloj SPI y desactivará por completo la tarjeta MicroSD!

### 1.2 Pinout de la Tarjeta MicroSD (Bus VSPI)

El lector MicroSD utiliza el bus por hardware VSPI dedicado:

| Pin SD | GPIO ESP32 | Señal VSPI | Notas de cableado |
| :--- | :--- | :--- | :--- |
| **CS** | 5 | Chip Select | Controlado por GPIO por software (activo en BAJO). Debe estar en HIGH en reposo/arranque. |
| **CLK / SCK**| 18 | Reloj SPI | Reloj SPI hasta 25 MHz. **NUNCA compartir con HUB75.** |
| **MISO / DO**| 19 | Salida de datos (Tarjeta -> ESP32) | Requiere resistencia de pull-up (`INPUT_PULLUP`). |
| **MOSI / DI**| 23 | Entrada de datos (ESP32 -> Tarjeta) | Salida de datos maestro SPI. |
| **VCC** | 3.3V / 5V | Alimentación | Conectar a 3.3V limpio (o 5V si el módulo dispone de regulador LDO 3.3V). |
| **GND** | GND | Masa común | Debe conectarse a la referencia de masa del ESP32. |

### 1.3 Interfaces de Control y Periféricos

| Componente | GPIO ESP32 | Descripción |
| :--- | :--- | :--- |
| **Botón (PIN)** | 21 | Pulsador multifunción (Clic corto: navegar / Clic largo: seleccionar). Conectado a GND. |
| **Receptor IR** | 34 | Demodulador infrarrojo protocolo NEC (GPIO solo de entrada). |
| **I2C SDA** | 21 | Bus de sensores opcional (compartido con el botón si I2C está activo). |
| **I2C SCL** | 22 | Bus de sensores opcional (en conflicto con la línea C de la matriz en cableado clásico). |

---

## 2. Trampas Técnicas y Reglas de Arquitectura para MicroSD en ESP32

Si modificas la inicialización de HAL o el código de almacenamiento, respeta rigurosamente estos 6 invariantes:

### 🔴 Trampa 1: Secuestro del CS por Hardware mediante `SPI.begin()`
En el framework Arduino-ESP32 (`esp32-hal-spi.c`), pasar `SD_CS_PIN` (5) como 4º argumento de `SPI.begin(sck, miso, mosi, ss)` llama automáticamente a `pinMatrixOutAttach(ss, SPI_SS_IDX, ...)`. Esto vincula el GPIO 5 al controlador de hardware SPI.
**Sin embargo**, la biblioteca `greiman/SdFat` gestiona el CS por software mediante `digitalWrite(m_csPin, level)`. Una vez asignado a `SPI_SS_IDX`, las escrituras GPIO normales **no pueden poner el pin en nivel BAJO**. La tarjeta SD nunca se selecciona, resultando en un error `code=0x1 (CMD0 timeout), data=0xFF`.
- **Regla:** Inicializar siempre SPI con `SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, -1)` y desconectar explícitamente el pin CS: `pinMatrixOutDetach(SD_CS_PIN, false, false)`.

### 🔴 Trampa 2: Falta de `USER_SPI_BEGIN` en `SdFat`
En `SdFat` (`SdSpiArduinoDriver.h`), si `spiConfig.options` no incluye `USER_SPI_BEGIN`, el driver llama internamente a `m_spi->begin()` sin argumentos, restableciendo el periférico SPI con los pines predeterminados del framework.
- **Regla:** Pasar siempre `SdSpiConfig(SD_CS_PIN, SHARED_SPI | USER_SPI_BEGIN, SD_SCK_MHZ(f), &SPI)`.

### 🟠 Trampa 3: Línea MISO Flotante
Muchos módulos pasivos de MicroSD económicos carecen de resistencias de pull-up en la línea DAT0/MISO. Durante el arranque y los estados de alta impedancia, un MISO flotante lee permanentemente `0xFF`.
- **Regla:** Habilitar explícitamente el pull-up interno del ESP32: `pinMode(VSPI_MISO, INPUT_PULLUP)`.

### 🟠 Trampa 4: Transición al Modo SPI (74 Ciclos de Reloj)
Al encenderse, todas las tarjetas SD inician en modo natif SD Bus. Para cambiar su autómata interno a modo SPI, el host debe emitir al menos 74 ciclos de reloj (20 bytes dummy `0xFF` a 1 MHz) con `CS = HIGH` antes de bajar CS y enviar `CMD0`.
- **Regla:** Enviar 20 bytes dummy mediante `SPI.transfer(0xFF)` manteniendo CS en HIGH antes de llamar a `sd.begin()`.

### 🔴 Trampa 5: Expiración del Watchdog durante el Fallback Multifrecuencia (TG1WDT ~9.2s)
El perro guardián por hardware del bootloader del ESP32 (TG1WDT) tiene un tiempo límite de aproximadamente 9,2 segundos.
Cuando `sd.begin()` intenta 4 frecuencias consecutivas (25 -> 16 -> 10 -> 4 MHz), cada intento fallido espera hasta 2000 ms (`SD_INIT_TIMEOUT`). Si fallan 4 intentos, transcurren 8 segundos. Con la sobrecarga de arranque, se alcanza ~9,2s y se dispara un `TG1WDT_SYS_RESET`, provocando un bucle de reinicios.
- **Regla:** Alimentar siempre el watchdog (`esp_task_wdt_reset()`) entre intentos y mantener corta la lista de frecuencias.

### 🔴 Trampa 6: Formateo Monolítico de la Partición NVS
Llamar a `nvs_flash_erase()` de forma síncrona en una partición NVS corrupta desactiva la caché de la CPU y detiene las interrupciones FreeRTOS. En particiones grandes, esto excede el temporizador del Interrupt Watchdog Timer (IWDT), provocando un reinicio `TG1WDT_SYS_RESET`.
- **Regla:** Formatear la partición NVS sector a sector en bloques de 4 KB con `esp_task_wdt_reset()` y `delay(5)` por sector.

---

## 3. Placa Waveshare ESP32-S3 Matrix (`esp32s3_waveshare`)

La placa Waveshare ESP32-S3 (N32R16: 32 MB Flash + 16 MB PSRAM Octal) integra su propio enrutamiento de fábrica y utiliza el bus de alta velocidad de 1 bit `SD_MMC`.

### 3.1 Pinout Oficial Waveshare S3

| Señal HUB75 | GPIO ESP32-S3 | Señal SD_MMC | GPIO ESP32-S3 | Periféricos Integrados | GPIO ESP32-S3 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **R1** | 4 | **D0 (DAT0)** | 17 | **I2C SDA** | 47 (SHTC3 @ 0x70, ES7210 @ 0x40) |
| **G1** | 5 | **CMD** | 44 | **I2C SCL** | 48 |
| **B1** | 6 | **CLK** | 1 | **I2S MCLK** | 12 |
| **R2** | 7 | | | **I2S SCLK** | 43 |
| **G2** | 15 | | | **I2S LRCK** | 38 |
| **B2** | 16 | | | **I2S ASDOUT** | 39 |
| **A** | 18 | | | **Salida DAC (ES8311)**| 21 |
| **B** | 8 | | | | |
| **C** | 3 | | | | |
| **D** | 42 | | | | |
| **E** | 9 | | | | |
| **LAT** | 40 | | | | |
| **OE** | 2 | | | | |
| **CLK** | 41 | | | | |

### 3.2 Trampas Técnicas y Reglas de Arquitectura para Waveshare S3

### 🔴 Trampa 1: Zona de Exclusión Estricta de PSRAM Octal OPI (GPIO 33 a 37)
Los 16 MB de PSRAM Octal requieren 8 líneas de datos y un reloj diferencial. En el ESP32-S3 N32R16, **los GPIOs 33, 34, 35, 36 y 37 están consumidos permanentemente por la interfaz de PSRAM**.
- **Regla:** NUNCA asignar ni manipular los GPIOs 33 a 37 en el software. Cualquier lectura, escritura o enlace a estos pines provoca un pánico de kernel irrecuperable inmediato.

### 🔴 Trampa 2: SD_MMC Nativo vs SPI
La ranura MicroSD de la placa Waveshare S3 está cableada directamente al controlador de hardware SDMMC del ESP32-S3 (GPIO 1, 44, 17). **NO** admite comunicación SPI.
- **Regla:** Compilar siempre con `USE_SD_MMC = 1` e inicializar mediante `SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)`. Nunca instanciar ni enlazar `SdFat` en la placa S3.

### 🟠 Trampa 3: Contexto VFS Fat en DRAM Interna Rápida (`max_files=3`)
Al inicializar `SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 3)`, el parámetro `max_files=3` es crítico. Garantiza que el contexto del sistema de archivos FatFs (~1,7 KB) se asigne en la SRAM interna en lugar de la PSRAM externa. Esto evita esperas por expulsión de líneas de caché frente al motor GDMA de HUB75 que lee los framebuffers desde la PSRAM.

---

## 4. Pautas de Alimentación (Todas las placas)

- **Fuente de alimentación de 5V dedicada:** Una matriz LED RGB 64x32 activa puede consumir hasta 4.0A con brillo blanco máximo.
- **Masa común:** Conectar siempre la línea de masa (GND) de la fuente de alimentación externa de 5V directamente al pin `GND` del ESP32.
- **Nunca alimentar los paneles por USB:** Los puertos USB suelen proporcionar entre 500 mA y 1 A, lo que provoca caídas de tensión (brownouts) en el ESP32, causando escrituras corruptas en la SD o caídas por watchdog.
