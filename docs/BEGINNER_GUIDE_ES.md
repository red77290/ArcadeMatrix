# Guía para Principiantes: Hardware, Conexiones, Paneles y Alimentación

🇬🇧 [English](BEGINNER_GUIDE.md) | 🇫🇷 [Français](BEGINNER_GUIDE_FR.md) | 🇪🇸 Español

Esta guía breve y sin tecnicismos va directo al grano: elegir tu placa electrónica, los paneles **`HUB75`** adecuados, calcular el amperaje de tu fuente de **`5V`** y arrancar **ArcadeMatrix** en minutos.

---

## 1. ¿Cómo funciona ArcadeMatrix? (En 4 pasos rápidos)

La pantalla es **100 % autónoma** (no necesita quedar conectada a ningún PC):
1. **Preparar la MicroSD (`FAT32`)**: descomprime **[`ArcadeMatrix-sdcard.zip`](https://github.com/red77290/ArcadeMatrix/releases/latest/download/ArcadeMatrix-sdcard.zip)** en la raíz de la tarjeta SD y **abre `config/network.json` con el Bloc de notas / TextEdit para escribir el `ssid` y `password` de tu Wi-Fi 2.4 GHz** *(obligatorio: ¡el Wi-Fi no se puede configurar desde la pantalla LED!)*.
2. **Flashear en 1 clic (una sola vez)**: conecta el ESP32 por USB al PC/Mac, abre **[red77290.github.io/ArcadeMatrix](https://red77290.github.io/ArcadeMatrix/)** en Chrome o Edge y pulsa **Install**.
3. **Conectar al panel LED y alimentar a `5V`**: al encenderse, el DMD se conecta solo a tu Wi-Fi y muestra su dirección IP.
4. **Controlar desde el móvil o PC**: abre **`http://arcadematrix.local`** (o la IP mostrada) en el navegador para cambiar relojes retro, clima/bolsa, subir GIFs o enlazar **Recalbox / Batocera**.

---

## 2. Elegir y conectar la placa electrónica (3 opciones)

![Los 3 montajes de hardware ArcadeMatrix](assets/guide/board_options_es.png)

### Opción A — ESP32 Clásico + Placa hija Mortaca o BenFactory *(Sin soldar — `128×32` máx)*
- **Montaje sándwich sin cable plano**: la placa hija se encaja **directamente en el conector `HUB75 IN` detrás del Panel n.º 1** (cero fajas), y un **ESP32 de 30 pines** (`WROOM-32`, 15 pines por lado) se encaja encima. Ranura MicroSD y bornas `5V` ya integradas.
- ⚠️ **Límite**: sin PSRAM, el ESP32 clásico soporta **únicamente `64×32` y `128×32`** *(no admite `128×64` ni `256×64`)*.
- 🔗 **Dónde comprar**: **[Placa Mortaca (DMDos)](https://www.mortaca.com/)** ([dmdos.net](https://www.dmdos.net/)) o **[Placa BenFactory](https://benfactory.fr/categorie-produit/carte-arcade/)** + un `ESP32 DevKit V1 30 pines`.
- **Botón Web Installer**: **`Standard ESP32`**.

### Opción B — Placa todo-en-uno Waveshare `ESP32-S3-RGB-Matrix` *(Sin soldar — Hasta `256×64` HD — Recomendada)*
- **Encaje directo**: trae conector hembra `HUB75` y lector MicroSD soldados detrás — **se encaja directamente detrás del primer panel LED**, sin faja ni soldaduras.
- **Funciones extra que añade**:
  - Pantallas **`128×64` y `256×64` HD** (16 MB PSRAM + 32 MB Flash), además de `64×32` / `128×32`.
  - Los **4 relojes HD exclusivos** (*Metal Slug*, *Pokédex*, *World Clock*, *Words Clock*).
  - **Doble micrófono integrado** (medidor de decibelios + visualizador musical) y **conector de altavoz** (WebRadio autónoma).
  - **Sensor interior `SHTC3`** (temperatura/humedad), **giroscopio `QMI8658`** (rotación auto horizontal/vertical *TATE*), **reloj RTC** y vídeo Showcase en RAM sin desgastar la SD.
- 🔗 **Enlaces oficiales**: **[Tienda Waveshare ESP32-S3-RGB-Matrix](https://www.waveshare.com/esp32-s3-rgb-matrix.htm)** | **[Wiki Waveshare](https://docs.waveshare.com/ESP32-S3-RGB-Matrix)**.
- **Botón Web Installer**: **`Waveshare ESP32-S3 (N32R16V)`**.

### Opción C — Montaje casero soldado (DIY)
No existen placas Mortaca / BenFactory para los `ESP32-S3` de 44 pines, por lo que los montajes soldados usan un **`ESP32-S3-DevKitC-1 (N16R8 / N8R8)` con PSRAM** para tener **`128×64` / `256×64` HD** y los relojes HD a bajo coste (o un `ESP32 Clásico` de 30 pines para `128×32` máx).

#### C.1 — Esquema DIY Recomendado: `ESP32-S3` con PSRAM (`N16R8` / `N8R8` — 44 pines, hasta `256×64` HD)
![Esquema para soldar DIY ESP32-S3 N16R8 a color](assets/guide/diy_wiring_esp32s3_es.png)

- **Material**: `ESP32-S3-DevKitC-1 N16R8` (o `N8R8` — comprueba `R8` = 8 MB PSRAM) + un **lector MicroSD `3.3V` directo** *(sin chip 5V `LVC125A` de Arduino, ya que el S3 usa `SD_MMC 1-bit` rápido)* + resistencia de `10 kΩ` entre `CS` y `3V3`.
- ⛔ **No conectes nunca nada en `GPIO 35`, `36`, `37`** (reservados internamente a la PSRAM). En la placa, `GPIO 44` aparece como **`RX`** y `GPIO 43` como **`TX`**.
- **Botón Web Installer**: **`ESP32-S3 (N16R8 / N8R8)`**.

| HUB75 (Panel) | ESP32-S3 | HUB75 (Panel) | ESP32-S3 | MicroSD 3.3V (`SD_MMC`) | ESP32-S3 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Pin 1 (`R1`)** | `GPIO 4` | **Pin 9 (`A`)** | `GPIO 18` | **`CLK / SCK`** | `GPIO 1` |
| **Pin 2 (`G1`)** | `GPIO 5` | **Pin 10 (`B`)** | `GPIO 8` | **`CMD` (`MOSI`)** | `RX` (`GPIO 44`) |
| **Pin 3 (`B1`)** | `GPIO 6` | **Pin 11 (`C`)** | `GPIO 3` | **`D0` (`MISO`)** | `GPIO 17` |
| **Pin 4 (`GND`)** | `GND` | **Pin 12 (`D`)** | `GPIO 42` | **`CS / DAT3`** | `3V3` vía `10 kΩ` |
| **Pin 5 (`R2`)** | `GPIO 7` | **Pin 13 (`CLK`)** | `GPIO 41` | **`VCC` / `GND`** | `3V3` / `GND` |
| **Pin 6 (`G2`)** | `GPIO 15` | **Pin 14 (`LAT`/`STB`)** | `GPIO 40` | *(Opc. I2C SHTC3/Giro)* | `SDA=47`, `SCL=48` |
| **Pin 7 (`B2`)** | `GPIO 16` | **Pin 15 (`OE`)** | `GPIO 2` | *(Opc. Audio I2S DAC)* | `BCLK=TX(43), WS=38, DIN=21` |
| **Pin 8 (`E`)** | `GPIO 9` *(64px)* | **Pin 16 (`GND`)** | `GND` | | |

#### C.2 — Esquema DIY Original: `ESP32 Clásico` (`esp32dev` — 30 pines, `128×32` máx)
![Esquema original para soldar ESP32 Clásico de 30 pines](assets/guide/diy_wiring_esp32_es.png)

- **Límite**: **solo `64×32` y `128×32`**. Une `Pin 8 (E)` a `GND` (**nunca a `D18`**, que es el reloj de la MicroSD). Botón Web Installer: **`Standard ESP32`**.

| HUB75 (Panel) | ESP32 (30p) | HUB75 (Panel) | ESP32 (30p) | MicroSD (`SPI`) | ESP32 (30p) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Pin 1 (`R1`)** | `D25` | **Pin 9 (`A`)** | `D33` | **`CS`** | `D5` |
| **Pin 2 (`G1`)** | `D26` | **Pin 10 (`B`)** | `D32` | **`SCK / CLK`** | `D18` |
| **Pin 3 (`B1`)** | `D27` | **Pin 11 (`C`)** | `D22` | **`MISO / DO`** | `D19` |
| **Pin 4 (`GND`)** | `GND` | **Pin 12 (`D`)** | `TX2` (`17`) | **`MOSI / DI`** | `D23` |
| **Pin 5 (`R2`)** | `D14` | **Pin 13 (`CLK`)** | `RX2` (`16`) | **`VCC`** | `3V3` *(o `5V` reg.)* |
| **Pin 6 (`G2`)** | `D12` | **Pin 14 (`LAT`/`STB`)** | `D4` | **`GND`** | `GND` |
| **Pin 7 (`B2`)** | `D13` | **Pin 15 (`OE`)** | `D15` | | |
| **Pin 8 (`E`)** | `GND` | **Pin 16 (`GND`)** | `GND` | | |

---

## 3. Paneles compatibles, Resoluciones y Potencia de Alimentación `5V` (Amperaje)

Compra siempre **paneles LED RGB de interior (`Indoor HUB75` / `HUB75E`)** (evita modelos *Outdoor*). **`P2`, `P2.5`, `P3`, `P4`, `P5`** es la separación en milímetros entre dos LEDs.

> 💡 **Equivalencia de tamaño en paneles `128×64`:**
> - Los paneles **`P2` de `128×64`** (`25,6 × 12,8 cm`) son muy fáciles de encontrar y tienen **exactamente el mismo tamaño físico que un `P4` (`64×32`)**, ¡con 4× más píxeles! Unir **`2× P2 (128×64)`** da un **`256×64` HD** del tamaño exacto de un DMD de pinball (`51,2 × 12,8 cm`).
> - Igualmente, un **`P2.5` de `128×64`** (`32 × 16 cm`) mide **exactamente lo mismo que un `P5` (`64×32`)** (`64 × 16 cm` con 2 paneles).

![Paneles HUB75, Resoluciones y Alimentación 5V](assets/guide/panel_resolutions_es.png)

> [!CAUTION]
> **¡Siempre `5 Voltios (5V)` exactos (nunca `12V` ni `24V`)!** Cuanto mayor es el panel (`P4`/`P5`) y más paneles encadenas, más amperaje (`A`) necesitas. Tener Amperios de sobra es 100 % seguro: la pantalla solo consume lo que necesita.

### Tabla resumen: Paneles, Dimensiones y Amperaje `5V` requerido

| Resolución | Paneles unidos | Tamaño total | Placa necesaria | `config/hardware.json` / WebUI | Fuente de `5V` y Amperaje necesario |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`64 × 32`** | **1× `64×32`** (`P2.5` a `P5`) | `16×8 cm` (`P2.5`) a `32×16 cm` (`P5`) | ESP32 o ESP32-S3 | `height=32`, `width=64`, `chain_length=1` | **¡Cargador USB-C `5V / 2A–3A` OK!** |
| **`128 × 32`** *(DMD Clásico)* | **2× `64×32`** lado a lado (`P2.5`, `P3`, **`P4`**, **`P5`**) | • `2× P2.5`: `32 × 8 cm`<br>• `2× P3`: `38,4 × 9,6 cm`<br>• **`2× P4`: `51,2 × 12,8 cm`** *(DMD pinball)*<br>• **`2× P5`: `64 × 16 cm`** | ESP32 Clásico *(Mortaca / BenFactory / DIY)* o ESP32-S3 | `height=32`, `width=64`, `chain_length=2` | **Cargador USB-C `5V / 3A` (15W) OK** a brillo normal<br>*(Prever `5V / 5A` [25W] si usas brillo al 100 % en `P4`/`P5`)* |
| **`128 × 64`** | **1× `128×64`** (`P2`, `P2.5`, `P3`) | • `1× P2`: `25,6 × 12,8 cm` *(= 1× P4)*<br>• `1× P2.5`: `32 × 16 cm` *(= 1× P5)*<br>• `1× P3`: `38,4 × 19,2 cm` | **SOLO ESP32-S3** *(no `esp32dev`)* | `height=64`, `width=128`, `chain_length=1` | **Fuente dedicada `5V / 5A a 6A`** (`25W–30W`) *(no USB-C)* |
| **`256 × 64` HD** *(2 paneles — Recomendado)* | **2× `128×64`** lado a lado en **`P2`**, **`P2.5`** o **`P3`** | • **`2× P2`: `51,2 × 12,8 cm`** *(= DMD `2× P4`)*<br>• **`2× P2.5`: `64 × 16 cm`** *(= DMD `2× P5`)*<br>• `2× P3`: `76,8 × 19,2 cm` | **SOLO ESP32-S3** *(Waveshare o `N16R8`)* | `height=64`, `width=128`, `chain_length=2` | • **`2× P2`**: **`5V / 8A a 10A`** (`40W–50W`)<br>• **`2× P2.5` o `2× P3`**: **`5V / 10A a 12A`** (`50W–60W`, ej. Mean Well `LRS-50-5` / `LRS-75-5`) |
| **`256 × 64` Mural Gigante** *(8 paneles: 4×2)* | **8× `64×32`** (`P2.5`, `P3`, `P4`, `P5` en 2 filas) | • `8× P3`: `76,8 × 19,2 cm`<br>• **`8× P4`: `102,4 × 25,6 cm`** *(¡1 metro!)*<br>• **`8× P5`: `128 × 32 cm`** | *Raspberry Pi ([ArcadeMatrix_RPi](https://github.com/red77290/ArcadeMatrix_RPi)) — en ESP32-S3 usa `2× 128×64`* | *(Versión RPi con `U-mapper`)* | • **`8× P2.5` / `8× P3`**: **`5V / 20A`** (`100W`, `LRS-100-5`)<br>• **`8× P4` / `8× P5`**: **`5V / 25A a 30A`** (`125W–150W`, `LRS-150-5`) |

**Conexión de la fuente `5V` dedicada:** alimenta cada panel directamente con los cables incluidos (**Rojo = `+5V`**, **Negro = `GND`**), y lleva `+5V` / `GND` a la borna de tu placa (o comparte el `GND` común si alimentas el ESP32 por USB). Detrás de los paneles sigue las flechas `➔`: la placa entra en **`IN`** del Panel 1 y una pequeña faja une **`OUT` (Panel 1) → `IN` (Panel 2)**.

---

## 4. Configurar `config/network.json` (¡Wi-Fi obligatorio!) y Primer Arranque

> [!IMPORTANT]
> **¡El DMD no puede conectarse al Wi-Fi configurándolo desde la pantalla!** Debes **editar obligatoriamente a mano el archivo `config/network.json`** en la MicroSD con tu `ssid` y `password` antes de encenderlo.

1. **Prepara la MicroSD (`FAT32`)**: copia el contenido de **[`ArcadeMatrix-sdcard.zip`](https://github.com/red77290/ArcadeMatrix/releases/latest/download/ArcadeMatrix-sdcard.zip)** directamente en la raíz de la SD (`config/`, `gifs/`, `fighters_32/`...).
2. **Edita `config/network.json` en la tarjeta SD** con el *Bloc de notas* (Windows) o *TextEdit* (Mac) para poner tu **Wi-Fi `2.4 GHz`**:
   ```json
   "wifi": {
       "ssid": "NombreDeTuWiFi_2.4GHz",
       "password": "TuPasswordWiFi",
       "hostname": "arcadematrix"
   }
   ```
   *(Si el tamaño de tus paneles difiere del `128×32` = `2× 64×32` por defecto, edita también `config/hardware.json` — p. ej. para `256×64` con `2× 128×64`, pon `"width": 128, "height": 64, "chainLength": 2`).*
3. **Flashea el firmware en 1 clic** en **[red77290.github.io/ArcadeMatrix](https://red77290.github.io/ArcadeMatrix/)** desde Chrome o Edge (`Standard ESP32`, `ESP32-S3 (N16R8 / N8R8)`, o `Waveshare ESP32-S3 (N32R16V)`).
4. **Inserta la MicroSD y enciende**: el DMD se conectará a tu Wi-Fi y mostrará su IP. Abre **`http://arcadematrix.local`** (o `http://<IP_MOSTRADA>`) en el móvil o PC para manejarlo todo al instante (relojes, clima, subida de GIFs sin sacar la SD, [sincronización con Recalbox / Batocera](../README_ES.md#-sincronizaci%C3%B3n-con-recalbox-batocera-y-retropie--showcase-en-juego-marquesinas-fichas-y-v%C3%ADdeos-gameplay)).

---

## 5. Solución rápida de problemas

- **Colores Rojo / Azul invertidos**: en la Web (**Sistema → Hardware → Orden de Color RGB**), cambia `RGB` por **`RBG`** (o `BGR`) y guarda.
- **Pantalla en negro con corriente**: comprueba que la placa está conectada en **`IN` (`J1`)** y no en `OUT`, o cambia **Controlador Matriz** a **`FM6124 / FM6126A`**.
- **Parpadeo / líneas fantasma**: activa **Invertir Fase de Reloj (`clkphase`)** o sube **`Latch Blanking`** en **Sistema → Hardware**.
- **No sale puerto USB en Web Installer**: el cable USB es «solo carga»; usa un cable USB de datos real con Chrome o Edge.

---

## 6. Anexo: Uso de las Herramientas para PC y Consolas (`ArcadeMatrix-tools.zip`)

Todas las herramientas opcionales para PC, Mac, Linux y consolas retro están reunidas en un único archivo independiente — **nunca necesitas clonar ni descargar todo el código fuente del proyecto**:

👉 **[⬇️ Descargar `ArcadeMatrix-tools.zip` (Última Release)](https://github.com/red77290/ArcadeMatrix/releases/latest/download/ArcadeMatrix-tools.zip)** *(también disponible directamente en el [Instalador Web](https://red77290.github.io/ArcadeMatrix/)).*

Descomprime `ArcadeMatrix-tools.zip` en tu ordenador para utilizar las herramientas incluidas en `ArcadeMatrix-tools/`:

1. **`recalbox_daemon/` — Sincronizar Recalbox, Batocera y RetroPie (Marquesinas + Récords + Showcase)**
   - **Para qué sirve**: Muestra las marquesinas de sistema y juego en `0 ms` al navegar por la consola, lanza fichas animadas y vídeos Showcase en partida, y celebra tus nuevos récords High-Score a 60 FPS al salir del juego (~3.100 ROMs MAME/FBNeo `.hi` + NVRAM Neo-Geo/CPS decodificados de serie sin binarios externos).
   - **Cómo usarlo**: Ejecuta `recalbox_daemon/install.ps1` (Windows PowerShell) o `./recalbox_daemon/install.sh` (macOS / Linux) — o `rpi_emulationstation_base_os_setup.sh` en RetroPie/Linux — e introduce la IP de tu consola + la IP de tu ArcadeMatrix.
2. **`pixelcade_sync/` — Descargar Marquesinas Oficiales de Pixelcade en la MicroSD**
   - **Para qué sirve**: Descarga y organiza automáticamente en tu tarjeta MicroSD la colección oficial de marquesinas arcade y consolas de Pixelcade.
   - **Cómo usarlo**: Conecta la MicroSD al ordenador y ejecuta `pixelcade_sync/pixelcade_sync.ps1` (Windows) o `./pixelcade_sync/pixelcade_sync.sh` (macOS / Linux) indicando la unidad de la tarjeta SD.
3. **`mugen_extractor/` — Añadir Luchadores MUGEN (`.fgt`)**
   - **Para qué sirve**: Convierte carpetas de personajes MUGEN (`.sff` / `.air` en `chars/`) en sprites `.fgt` RGB565 optimizados + manifiesto `index.txt` para el overlay `fighter`.
   - **Cómo usarlo**: Haz doble clic en `mugen_extractor/start_extractor.bat` (Windows) o ejecuta `./mugen_extractor/start_extractor.sh` (macOS / Linux) — instala `Pillow` automáticamente — o lanza `python3 mugen_extractor.py --src /ruta/chars --dest ./fighters_32` y copia `fighters_32/` (o `fighters_64/`) en la tarjeta SD.
4. **`font_conversion/` y `bdf_to_amfont/` — Añadir Fuentes Personalizadas (Reloj, Fecha y Mensaje)**
   - **Para qué sirve**: Convierte fuentes bitmap `.bdf` al formato compacto `.amf` en la SD sin dependencias externas (`font_conversion/generate_fonts.py` convierte toda la carpeta `SD:/fonts/` por lotes, mientras que `bdf_to_amfont/bdf_to_amfont.py` convierte un archivo individual).
   - **Cómo usarlo**: Copia tus archivos `.bdf` en `SD:/fonts/` y ejecuta `python3 font_conversion/generate_fonts.py /ruta/a/SDCARD` (o `start_generate_fonts.bat` / `./start_generate_fonts.sh`). Las fuentes aparecen al instante en los menús de la Web UI sin reiniciar.
5. **`gif_indexation/` y `anim_compiler/` — Preparación Masiva sin Conexión en PC**
   - **Para qué sirve**: Aunque la gestión diaria de GIFs se hace por Wi-Fi desde **GIF File Manager** en la Web UI, `gif_indexation/generate_index.ps1` (`.sh`) permite indexar miles de GIFs por lotes en un lector SD de PC, y `anim_compiler/gif2amanim.py` compila flujos `.amanim`.
6. **`home_assistant/` — Blueprints Domóticos para Home Assistant (`mqttdata`)**
   - **Para qué sirve**: Blueprints YAML listos para importar (`home_assistant/blueprints/*.yaml`) en Home Assistant (`Ajustes → Automatizaciones y escenas → Blueprints`) para enviar sensores, gráficas de 24h, tablas y clima al DMD por MQTT.

---

## 7. Modo Online vs Modo Offline Autónomo (Con `DMD Media Showcase`) — ¿Cuál elegir?

Sin conocimientos informáticos, puedes disfrutar de tu pantalla de **3 formas** con la aplicación gratuita **[DMD Media Showcase](https://github.com/red77290/dmd_media_showcase)**:

1. 🌐 **Modo 100 % ONLINE (Cero archivos que copiar en la tarjeta SD)**:
   - Inicia Showcase en tu PC, Mac o NAS Docker y activa **Showcase Server** en la Web de tu pantalla (`🟢 Communication OK`). Recibirás miles de juegos retro, películas, series y animes en directo sin repeticiones.
2. 💾 **Modo 100 % OFFLINE (PC Apagado y Sin Internet — Packs en MicroSD)**:
   - Abre una vez la pestaña **`7. Batch Scraper & Packs Offline`** de Showcase desde tu PC, Mac o móvil:
     - **Para el Reloj / Rotación**: genera `100` a `1 000+` archivos `.gif` `RGB888` a la carta (`Completo`, `Solo Vídeo`, `Solo Póster`..., `2 s` a `120 s` máx.) ordenados en `snes_platformer`, `megadrive_fighting`, `neogeo_run&gun` + `playlists.json` y cópialos en **`SD:/gifs/`** (o `SD:/gifs_tate/` en vertical).
     - **Para recreativas Recalbox / Batocera sin conexión**: importa tus `gamelist.xml` y copia **`showcase/`** (o `showcase_tate/`) + **`pixelcade/`** en la raíz de la SD. Al iniciar un juego, el marquee sale en `0 ms` y el `.gif` Showcase arranca tras `3,5 s` sin ningún PC encendido.
3. ⚡ **Modo HÍBRIDO (Automático)**:
   - Conserva `SD:/showcase/` en la SD **y** deja **Showcase Server** activado: la pantalla reproduce siempre primero tus `.gif` locales (`0` tráfico Wi-Fi) y recurre al servidor Online solo para juegos nuevos que no estén en la SD.
   - 👉 **[Leer el Tutorial Completo Paso a Paso (Principiantes, PC, Docker y Móvil)](https://github.com/red77290/dmd_media_showcase/blob/main/docs/TUTORIAL_ONLINE_OFFLINE_SCRAPING_ES.md)**

💬 ¿Dudas? ¡Únete a la comunidad en **[Telegram ArcadeMatrix](https://t.me/+DrSdAO8UMUViNmE0)**!

