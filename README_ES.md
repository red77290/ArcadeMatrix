# ArcadeMatrix

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

📺 **Demostración en Video / Presentación:** https://youtu.be/2sA5wLVozRQ?si=T1gn6MYDwpq2-54c

¡Bienvenido al firmware de código abierto para ESP32 diseñado para controlar matrices LED HUB75! Este proyecto te permite mostrar relojes Arcade, GIFs animados, el tiempo en directo y **sprites de juegos de lucha MUGEN** simulados directamente en una matriz LED real.

---

> [!IMPORTANT]
> ### ⚡ Instalación Rápida por Navegador Web (Web Installer)
> ¡Flashea tu placa ESP32 directamente desde tu navegador (Chrome / Edge / Opera) en un solo clic sin necesidad de instalar ningún software!
> 
> 👉 **[🚀 Iniciar ArcadeMatrix Web Installer](https://red77290.github.io/ArcadeMatrix/)**
> 
> | Versión de Firmware | Placa de Hardware Compatible | Botón Web Installer |
> | :--- | :--- | :--- |
> | **ESP32-DevKit (Clásico)** | ESP32-DevKitC, NodeMCU-32S, WROOM-32 (4MB Flash) | Selecciona **ESP32 (Estándar)** |
> | **ESP32-S3 Waveshare** | Waveshare ESP32-S3 Matrix Board (32MB Flash + 16MB PSRAM (N32R16)) | Selecciona **ESP32-S3 (Waveshare)** |

---

## 💾 Releases y Kit de Tarjeta SD

**[⬇️ Descargar la última Release precompilada y Kit de Tarjeta SD](https://github.com/red77290/ArcadeMatrix/releases/latest)**
- **Archivos de Firmware**: Elige `ArcadeMatrix-esp32dev.zip` o `ArcadeMatrix-esp32s3_waveshare.zip` según tu placa (contiene `firmware-*.bin`, `bootloader-*.bin`, `partitions-*.bin` y `boot_app0.bin` para flasheo manual con `esptool.py`; consulta [Primeros pasos](docs/GETTING_STARTED_ES.md#flashing-a-pre-built-release)).
- **Kit de Tarjeta SD (`ArcadeMatrix-sdcard.zip`)**: Contiene la estructura de carpetas lista para copiar a la raíz de la tarjeta SD (`config.json`, carpetas GIFs/MUGEN y scripts de indexación).

## 🕹️ Motores Integrados y Simulaciones de Hardware

Cada motor en ArcadeMatrix está diseñado con **cero asignaciones dinámicas y cero contención de mutex** en la ruta crítica Core 1, garantizando una frecuencia de refresco estable de 60 FPS. A continuación se muestran simulaciones precisas de cada motor ejecutándose en pantallas físicas HUB75:

| Motor / ID | Simulación de Hardware | Descripción y Características Clave |
| :--- | :---: | :--- |
| **Desk Master Dashboard**<br>`dashboard` | <img src="docs/assets/engines/engine_dashboard.png" width="240" alt="Motor Dashboard"> | Reloj de escritorio horizontal completo con esfera de reloj analógica en pixel-art, segundero fluido, relojes mundiales, clima interior SHTC3 y teletipo en vivo de criptomonedas Binance y acciones Yahoo Finance. |
| **Reloj Retro Gaming**<br>`clock` | <img src="docs/assets/engines/engine_clock.png" width="240" alt="Reloj Retro"><br><br>[👉 **Ver galería de 10+ relojes retro ➔**](#-relojes-retro-legendarios-posters--simulaciones-hub75) | Colección exclusiva de relojes animados de arcade y consolas retro (Metal Slug, Castlevania, Mario, Mega Man, Sonic, Pokédex, Pac-Man, Tetris, Reloj Mundial, Lluvia Matrix...) con sprites 100 % fieles al píxel original. |
| **WebRadio y Reproductor Musical**<br>`music` | <img src="docs/assets/engines/engine_music.png" width="240" alt="Motor Música"> | Streaming de audio autónomo con decodificación MP3 lineal en tiempo real (`minimp3`), salida DAC I2S Everest ES8311 de alta fidelidad, carátulas de álbumes y visualizador de audio dinámico de 64 bandas. |
| **Spotify Now Playing**<br>`spotify` | <img src="docs/assets/engines/engine_spotify.png" width="240" alt="Motor Spotify"> | Visualización en tiempo real de la pista en reproducción con carátula a todo color, desplazamiento artista/título, barras de ecualizador animadas y barra de progreso. |
| **Google Cast & Nest**<br>`googlecast` | <img src="docs/assets/engines/engine_googlecast.png" width="240" alt="Motor Google Cast"> | Descubrimiento automático mDNS de dispositivos Google Home / Nest Audio con carátulas de streaming, volumen y progreso de reproducción en vivo. |
| **Ticker y Gráfico Cripto**<br>`crypto` | <img src="docs/assets/engines/engine_crypto.png" width="240" alt="Motor Cripto"> | Precios en vivo de Binance / CoinGecko, indicador de variación en 24h y gráficos sparkline históricos en tiempo real con caché TTL inteligente. |
| **Ticker de Mercado de Valores**<br>`stock` | <img src="docs/assets/engines/engine_stock.png" width="240" alt="Motor Bolsa"> | Cotizaciones en tiempo real de Yahoo Finance, indicadores de 1D % y gráficos sparkline intradiarios para acciones y ETFs de NASDAQ/S&P. |
| **Pronóstico del Clima**<br>`weather` | <img src="docs/assets/engines/engine_weather.png" width="240" alt="Motor Clima"> | Condiciones exteriores en vivo, temperaturas máximas/mínimas, humedad, viento e iconos retro animados mediante OpenWeatherMap y Open-Meteo. |
| **Sensor de Clima Interior**<br>`temp` | <img src="docs/assets/engines/engine_temp.png" width="240" alt="Motor Temperatura"> | Temperatura interior (°C/°F) y humedad relativa en tiempo real mediante el sensor I2C integrado SHTC3 con indicadores dinámicos de confort. |
| **Sonómetro Decibelímetro SPL**<br>`decibel` | <img src="docs/assets/engines/engine_decibel.png" width="240" alt="Motor Decibelios"> | Monitorización calibrada de ruido ambiental en dB SPL con smileys arcade reactivos, indicador superior de barra de salud estilo lucha VS y vúmetro segmentado. |
| **HUD de Telemetría del Sistema**<br>`sysinfo` | <img src="docs/assets/engines/engine_sysinfo.png" width="240" alt="Motor SysInfo"> | Monitor de doble columna en tiempo real de uso de CPU (%), RAM (%), temperatura del hardware SoC (°C/°F) y tiempo de actividad con barras de colores. |
| **Ticker de Noticias en Vivo**<br>`gnews` | <img src="docs/assets/engines/engine_gnews.png" width="240" alt="Motor GNews"> | Titulares de última hora en tiempo real con etiquetas temáticas (`[TECH]`, `[MUNDO]`, `[ECON]`), baliza luminosa parpadeante de emisión en directo y teletipo fluido a 60 FPS. |
| **Home Assistant y MQTT**<br>`mqttdata` | <img src="docs/assets/engines/engine_mqttdata.png" width="240" alt="Motor MQTT Data"> | Paneles de Home Assistant, valores de sensores, gráficos históricos de 24h y pronósticos locales emitidos por MQTT con blueprints listos para usar. |
| **Visualizador de Audio**<br>`visualizer` | <img src="docs/assets/engines/engine_visualizer.png" width="240" alt="Motor Visualizador"> | Barras de espectro de 32 bandas en gradiente arcoíris con retención de picos (peak hold), formas de onda de osciloscopio y modos radiales reactivos. |
| **Reproductor de GIFs Animados**<br>`gif` | <img src="docs/assets/engines/engine_gif.png" width="240" alt="Motor GIF"> | Reproducción fluida a 60 FPS de animaciones retro y listas de reproducción de tarjeta SD con aceleración canvas DMA de copia cero. |
| **Pancarta de Texto Desplazable**<br>`message` | <img src="docs/assets/engines/engine_message.png" width="240" alt="Motor Mensaje"> | Letreros de matriz de puntos personalizables con tipografía ámbar brillante, múltiples direcciones de desplazamiento y activadores de API REST. |
| **Marquesina de Recreativa Arcade**<br>`marquee` | <img src="docs/assets/engines/engine_marquee.png" width="240" alt="Motor Marquesina"> | Muestra marquesinas oficiales de juegos arcade iluminadas mediante integración Pixelcade para sistemas Batocera, Recalbox y RetroPie. |
| **Calendario y Fecha**<br>`date` | <img src="docs/assets/engines/engine_date.png" width="240" alt="Motor Fecha"> | Fecha en tamaño grande con diseño arcade, sombras proyectadas 3D estilo Capcom/Nintendo, soporte multilingüe (ES, EN, FR) y sincronización NTP/RTC DS3231. |

> [!NOTE]
> **Aviso sobre el renderizado de hardware:** Las capturas de pantalla, vistas previas de motores y posters de relojes presentados en esta documentación son simulaciones de software de alta fidelidad diseñadas para ilustrar el diseño, las animaciones y la telemetría. El aspecto visual real en un panel físico de matriz LED HUB75 puede variar según el paso de píxel (pitch), el filtro difusor acrílico, el brillo de los LEDs y la iluminación ambiental.

---

## 🎮 Relojes Retro Legendarios (Posters & Simulaciones HUB75)

> [!NOTE]
> Las vistas previas a continuación son simulaciones de software de alta fidelidad. El renderizado visual real en una matriz LED HUB75 física puede presentar ligeras diferencias (difusión óptica, colorimetría y brillo percibido).

> [!IMPORTANT]
> **Disponibilidad según el perfil de hardware (ESP32-S3 vs ESP32 Estándar):**
> Debido a la huella de memoria Flash ROM de los recursos gráficos de alta resolución en placas de 4 MB de Flash, **Metal Slug** (Tema 41), **Pokédex** (Tema 32), **World Clock** (Tema 33) y **Words Clock** (Tema 37) son exclusivos de las **placas ESP32-S3 (16 MB de Flash)**. En placas ESP32 DevKit clásicas, la selección de estos temas recurre automáticamente y sin interrupción al reloj Arcade estándar.

ArcadeMatrix incluye una colección exclusiva de relojes retro de arcade y consolas sincronizados por hardware, renderizados con sprites originales 100 % fieles al píxel a 60 FPS estables, con cero asignaciones dinámicas en el bucle caliente Core 1:

### 1. Metal Slug: Super Vehicle-001 (SNK Neo Geo) — Tema 41 *(Exclusivo ESP32-S3)*
*Pixel art auténtico de SNK Neo Geo con escenario de bazar en el desierto árabe, animaciones de combate de Marco Rossi, tanque rebelde Di-Cokka, helicóptero patrulla y tiroteos con ametralladora pesada.*
![Reloj Metal Slug](docs/assets/clocks/poster_metal_slug.png)

### 2. Castlevania (Konami NES) — Tema 31
*Campanario gótico con Simon Belmont subiendo la gran escalera de piedra hacia los aposentos de Drácula, antorcha parpadeante en pedestal, murciélago vampiro cruzando la luna de sangre y dígitos góticos en marfil de alta legibilidad.*
![Reloj Castlevania](docs/assets/clocks/poster_castlevania.png)

### 3. Super Mario Bros (NES) — Tema 30
*Overworld del Reino Champiñón con bloques de ladrillo SMB1 auténticos; Mario corre por la pantalla y salta bajo el bloque para activar un rebote elástico y cambiar el dígito (con patada a caparazón Koopa verde y aparición de moneda de oro en modo compacto 128x32).*
![Reloj Super Mario Bros](docs/assets/clocks/poster_super_mario.png)

### 4. Mega Man (Capcom NES) — Tema 35
*Fortaleza Wily de Capcom con plataformas técnicas separadas, barra de energía vital, Metool dormido y Mega Man disparando con su Buster al pod de minutos.*
![Reloj Mega Man](docs/assets/clocks/poster_megaman.png)

### 5. Sonic The Hedgehog (Sega Genesis) — Tema 39
*Plataformas ajedrezadas de Green Hill Zone, anillos dorados giratorios, muelle rojo, badnik Motobug y animaciones de espera y salto spin-dash de Sonic.*
![Reloj Sonic The Hedgehog](docs/assets/clocks/poster_sonic.png)

### 6. Pokémon Pokédex (Nintendo Game Boy) — Tema 32 *(Exclusivo ESP32-S3)*
*Interfaz Pokédex (Pocket Index) auténtica de doble pantalla con visor de inspección, sprite animado de Pikachu, reloj digital en fuente PKMN, barra de telemetría de segundos y sensor LED de estado parpadeante.*
![Reloj Pokédex](docs/assets/clocks/poster_pokedex.png)

### 7. Pac-Man Arcade (Namco 1980) — Tema 26
*Laberinto arcade Namco original con pasillos de neón azul, puntos, Pac-Man animado y los 4 fantasmas perseguidores (Blinky, Pinky, Inky, Clyde).*
![Reloj Pac-Man](docs/assets/clocks/poster_pacman.png)

### 8. Russian Tetris (Alexey Pajitnov / Game Boy) — Tema 23
*Legendario rompecabezas de bloques con minos 3D biselados dinámicos (I, J, L, O, S, T, Z) cayendo en cascada para construir las horas y minutos en tiempo real.*
![Reloj Tetris](docs/assets/clocks/poster_tetris.png)

### 9. World Clock & Terminador Solar — Tema 33 *(Exclusivo ESP32-S3)*
*Mapa mundial continental de alta resolución con terminador solar día/noche dinámico que calcula la declinación solar en tiempo real, marcador de meridiano local y hora dual UTC/local.*
![Reloj World Map](docs/assets/clocks/poster_worldmap.png)

### 10. True Matrix Rain (Hermanas Wachowski 1999) — Tema 21
*Lluvia digital icónica con cascadas de glifos verde fósforo, velocidades de caída aleatorias, cabezas blancas luminosas, estelas de persistencia y dígitos de neón brillantes.*
![Reloj Matrix Rain](docs/assets/clocks/poster_matrix_rain.png)

---

## 🚀 Compatibilidad Universal de Motores: Auto Depth & Auto Buffer

ArcadeMatrix integra un canal de ejecución avanzado y consciente de la memoria que permite que los 18 motores (incluidos los más exigentes como `AnimatedGIF`, `Stock`, `Crypto` y `MUGEN`) funcionen sin problemas tanto en placas **ESP32 clásicas con 0 KB de PSRAM** como en potentes **ESP32-S3 con 16 MB de PSRAM**:

- **🎨 Auto Color Depth (`dynamic_color_depth`)**:
  - **Gráficos Ricos en 8 Bits por Defecto**: Relojes, animaciones de lucha MUGEN, visualizadores, mensajes y marquesinas se procesan con una profundidad de color máxima de 8 bits (hasta 256 niveles de brillo por canal RGB).
  - **Recuperación Dinámica de Sandbox de Memoria**: Cuando los motores de red (`stock`, `crypto`, `weather`, `gnews`) necesitan realizar peticiones HTTPS, el controlador HUB75 DMA pasa temporalmente a 4 bits bajo apagado de hardware (blanking OE). Esto libera al instante entre **16 y 24 KB de RAM DMA contigua**, garantizando margen holgado para las conexiones TLS y el procesamiento JSON sin fragmentar la memoria dinámica.
  - **Transiciones Instantáneas de 0 ms**: Tan pronto como los datos de mercado y gráficos están en caché (`needsTlsFetch() == false`), la reducción a 4 bits se omite. Stock y Crypto se activan de inmediato en **calidad total de 8 bits con 0 ms de retardo (cero cortes en pantalla)**.

- **⚡ Canalización de Búfer Automática (`render_pipeline: auto`)**:
  - **Aceleración PSRAM**: En placas ESP32-S3, asigna un canvas de 16 bits en PSRAM con búfer doble DMA (`canvas_double`) para una fluidez absoluta a 60 FPS sin desgarro de pantalla.
  - **Aislamiento DMA en SRAM1**: En chips ESP32 estándar, el canvas fuera de pantalla de 8 KB se pre-asigna en **SRAM1** (DRAM interna exclusiva de CPU) en el arranque temprano antes de iniciar Wi-Fi y el servidor Web. Esto salvaguarda **8.192 bytes de memoria contigua apta para DMA en la SRAM2**, evitando la inanición del heap.
  - **Búfer Simple sin Desgarro**: Utiliza ráfagas secuenciales con `Hub75BulkEncoder` para sincronizar las tramas, eliminando el tearing incluso en configuraciones con un solo búfer DMA.

## Estructura de la tarjeta SD
Formatea tu tarjeta SD en **FAT32** o **exFAT**. Tu tarjeta SD debería verse así:
```
SD:/
  ├─ config.json
  ├─ gifs/
  │  │   └─ mario.gif
  └─ fighters_32/
      ├─ backgrounds/
      │   └─ stage1.raw
      └─ ryu/
          ├─ idle.fgt
          └─ attack.fgt
  └─ fighters_64/
      └─ (misma estructura para paneles de 64px de alto)
```
*Nota: la carpeta `www/` ya no es necesaria en la tarjeta SD, ya que la interfaz web ahora está integrada directamente en el firmware del ESP32.*

## Configuración (`config.json`)
El archivo `config.json` situado en la raíz de tu tarjeta SD es exhaustivo. Contiene parámetros para el tamaño de la matriz, la profundidad de color, los temas de reloj, el orden de rotación en reposo y los fondos de sprites MUGEN.
Abre el `config.json` incluido en la carpeta `release/sdCard/` para ver todos los valores posibles.

## Extracción de sprites MUGEN (script `mugen_extractor.py`)
Para mostrar luchadores en el módulo `SPRITES`, el ESP32 espera archivos brutos `.fgt`. Como el ESP32 no es lo bastante potente para decodificar de forma nativa formatos complejos de personajes MUGEN, proporcionamos un script Python personalizado para convertirlos y generar un manifiesto `index.txt` con cajas englobantes perfectas y valores de suelo virtual.

### Cómo usar el extractor:
1. Asegúrate de tener Python 3 instalado con la biblioteca `Pillow` (`pip install Pillow`), o simplemente ejecuta `tools/mugen_extractor/start_extractor.sh`/`.bat`, que lo instala automáticamente por ti.
2. Ve a la carpeta `tools/mugen_extractor/` dentro del repositorio.
3. Ejecuta el script apuntando `--src` a tu carpeta `chars/` de MUGEN:
   ```bash
   python mugen_extractor.py --src /Ruta/A/Tus/Personajes/Mugen/chars --dest ./fighters_32
   # O con un factor de escala personalizado (ej: --scale 0.5 para reducir al 50% ahorrando 75% de RAM):
   python mugen_extractor.py --src /Ruta/A/Tus/Personajes/Mugen/chars --dest ./fighters_64 --scale 0.5
   ```
4. El script genera los archivos `.fgt` junto con un manifiesto `index.txt`/`index.json` en la carpeta `--dest`. Ejecútalo dos veces (con `--dest ./fighters_32` y `--dest ./fighters_64`) si quieres assets para ambos tamaños de matriz.
5. Copia la carpeta resultante `fighters_32/` o `fighters_64/` a tu tarjeta SD.

Para ver todos los detalles, consulta la documentación en `tools/mugen_extractor/README_ES.md`.

### Fondos de sprites
¡Los luchadores necesitan una arena! Puedes definir el fondo en el que luchan colocando un archivo de imagen bruto (por ejemplo, `stage1.raw`) en `SD:/fighters_32/backgrounds/`.
Luego, vincula este fondo en tu `config.json` bajo la sección `[DATE]` (¡los fondos sirven para dar más vida al módulo de fecha!):
```ini
BACKGROUND_SPRITE=stage1.raw
```

## Indexación de playlists GIF (selección de carpetas en la Web UI)
La Web UI te permite marcar/desmarcar qué subcarpetas de `gifs/` se reproducen durante la rotación en reposo, pero necesita un manifiesto `playlists.json` para saber qué hay en la tarjeta SD. La reproducción de GIF funciona perfectamente sin él (el motor siempre lee los archivos directamente desde la tarjeta SD) - este paso solo es necesario si quieres usar ese selector de casillas.

> [!TIP]
> **Gestor de Archivos y Subida Web (¡Sin extraer la tarjeta SD!):**
> Ahora puedes gestionar playlists, crear/eliminar carpetas, renombrar y subir GIFs animados directamente desde tu navegador con la tarjeta **GIF File Manager** en la Web UI, con soporte nativo Horizontal / Vertical y reindexación automática en segundo plano — desarrollado por [@TooncesToo](https://github.com/TooncesToo).

1. Organiza tus GIF en subcarpetas dentro de `gifs/` en tu tarjeta SD, por ejemplo `gifs/mario/`, `gifs/sonic/` (cada subcarpeta se convierte en una playlist seleccionable; los archivos `.gif` sueltos directamente en `gifs/` siempre se reproducen y no necesitan este paso).
2. Ejecuta uno de los scripts nativos en `tools/gif_indexation/` - sin necesidad de Python:
   ```bash
   ./generate_index.sh /Volumes/SDCARD      # macOS/Linux - pasa la raíz de la SD o su carpeta gifs/
   ```
   ```powershell
   .\generate_index.ps1 -Path E:\           # Windows
   ```
3. Esto crea `gifs/playlists.json` en la tarjeta SD. Vuelve a ejecutarlo cada vez que agregues, elimines o renombres una carpeta dentro de `gifs/`.

Para ver todos los detalles, consulta `tools/gif_indexation/README_ES.md`.

## Fuentes personalizadas (conversión BDF → AMF)
El Reloj, la Fecha y el mensaje desplazante pueden usar fuentes bitmap personalizadas cargadas desde la tarjeta SD en lugar de las ~6 fuentes compiladas en el firmware, usando las mismas fuentes `.bdf` que `ArcadeMatrix_RPi` ya incluye. Sin embargo, el ESP32 no tiene un analizador BDF a bordo, por lo que primero deben convertirse al formato compacto `.amf`.

1. Copia tu(s) fuente(s) `.bdf` en la carpeta `fonts/` de tu tarjeta SD.
2. Ejecuta el convertidor por lotes:
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py /Volumes/SDCARD   # pasa la raíz de la SD o su carpeta fonts/
   ```
   (No requiere dependencias externas. Solo Python estándar.)
3. Esto convierte cada `.bdf` en un `.amf` del mismo nombre en el mismo lugar. Las fuentes resultantes aparecen de inmediato en la página de Configuración de la interfaz web (menús desplegables "Font" de Reloj/Fecha) - sin necesidad de reiniciar.

Para todos los detalles, revisa `tools/bdf_to_amfont/README_ES.md`.

## ⚡ Compatibilidad de Hardware y Motores

| Motor / Funcionalidad | Categoría | ESP32-S3 (Placa Waveshare) | ESP32 Clásico (DevKit / `esp32dev`) | Requisito de Hardware / Red |
| :--- | :--- | :---: | :---: | :--- |
| **Reloj y Watch Faces (`clock`)** | `info` | 🟢 60 FPS | 🟢 60 FPS | Fuentes bitmap dinámicas, temas retro/arcade |
| **Animaciones GIFs (`gifs`)** | `media` | 🟢 Fullspeed 60 FPS | 🟢 Fullspeed 60 FPS | Tarjeta Micro-SD (orientaciones Yoko / Tate) |
| **Combate M.U.G.E.N (`fighter`)** | `arcade` | 🟢 60 FPS | 🟢 60 FPS | Tarjeta Micro-SD (streaming sprites RGB565) |
| **Panel Desk Deck (`dashboard`)** | `info` | 🟢 10 FPS | 🟢 10 FPS | Wi-Fi (Reloj multi-widget, clima y mercados) |
| **Criptomonedas en Tiempo Real (`crypto`)** | `finance` | 🟢 10 FPS (8 bits) | 🟢 10 FPS (8 bits en caché) | Wi-Fi, HTTPS/TLS (Binance, CoinGecko) |
| **Bolsa de Valores y Gráficos Sparklines (`stock`)** | `finance` | 🟢 10 FPS (8 bits) | 🟢 10 FPS (8 bits en caché) | Wi-Fi, HTTPS/TLS (Yahoo Finance) |
| **Noticias en Directo (`gnews`)** | `news` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, HTTPS/TLS (API GNews) |
| **Tiempo en Directo (`weather`)** | `info` | 🟢 10 FPS | 🟢 10 FPS | Wi-Fi (OpenWeatherMap, Open-Meteo) |
| **Fecha y Calendario (`date`)** | `info` | 🟢 30 FPS | 🟢 30 FPS | Hora del sistema local y fondo sprite opcional |
| **Texto Desplazable (`message`)** | `text` | 🟢 60 FPS | 🟢 60 FPS | Desplazador de texto sub-píxel a 60 FPS |
| **Telemetría del Sistema (`sysinfo`)** | `system` | 🟢 10 FPS | 🟢 10 FPS | Indicadores en tiempo real CPU, RAM, Temp y Uptime |
| **Marquesina Retro Arcade (`marquee`)** | `arcade` | 🟢 60 FPS | 🟢 60 FPS | MQTT / Batocera / Recalbox / RetroPie |
| **Spotify Now Playing (`spotify`)** | `media` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, HTTPS/TLS, API Web de Spotify |
| **Pantalla Google Cast (`google_cast`)** | `media` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, descubrimiento local mDNS |
| **WebRadio Autónoma (`music`)** | `media` | 🟢 30 FPS (DAC I2S) | ❌ Incompatible | Requiere DAC ES8311 y PSRAM |
| **Visualizador de Espectro Audio FFT (`audiovisualizer`)** | `audio` | 🟢 60 FPS (Micro I2S) | ❌ Incompatible | Requiere conjunto de doble micro ES7210 |
| **Medidor de Decibelios SPL (`decibel`)** | `audio` | 🟢 30 FPS (Micro I2S) | ❌ Incompatible | Requiere conjunto de doble micro ES7210 |
| **Sensor de Clima Interior (`temp`)** | `sensor` | 🟢 30 FPS (I2C SHTC3) | ❌ Incompatible | Requiere sensor de temperatura/humedad SHTC3 |
| **Home Assistant y Datos MQTT (`mqttdata`)** | `info` | 🟢 30 FPS | 🟢 30 FPS | Broker MQTT / Blueprints de Home Assistant |

👉 *Para el desglose técnico exhaustivo (presupuestos de FPS, RAM interna y DMA), consulta la [Matriz de Compatibilidad de Motores](docs/ENGINE_COMPATIBILITY_MATRIX.md).*

> [!NOTE]
> ### 💡 Compatibilidad Total TLS en ESP32 Clásico (`esp32dev`) y Prefetch Inteligente de Transición
> El ESP32 estándar (WROOM-32 sin PSRAM) ahora es **totalmente compatible** con los motores conectados HTTPS/TLS (`crypto`, `stock`, `gnews`, `weather`) gracias a un **espacio de memoria seguro con liberación de DMA**:
> 1. **¿Por qué una pausa de ~1 segundo en la primera rotación?** En el ESP32 clásico, el escaneo físico HUB75 DMA y el protocolo criptográfico mbedTLS no pueden coexistir a la vez debido a los límites de SRAM interna contigua (~45 KB requeridos por mbedTLS). Durante la transición hacia un motor TLS, el firmware libera temporalmente el framebuffer DMA, abriendo una **ventana de memoria limpia de 89 KB** para descargar por lotes todas las cotizaciones, gráficos e iconos en keep-alive HTTP/1.1 en ~700 ms antes de reconfigurar la pantalla.
> 2. **Rotaciones Siguientes Instantáneas:** ¡Este prefetch de transición ocurre **solo una vez** en la rotación inicial! Para todas las rotaciones siguientes mientras la caché esté vigente (configurable desde la UI, ej. de 10 a 15 minutos), el motor muestra instantáneamente los datos desde la RAM a **profundidad completa de 8 bits, sin retraso de transición ni tráfico de red**.
> 3. **Actualización Automática:** Cuando expira el tiempo de vida (TTL) de la caché, el motor realiza limpiamente una actualización en la siguiente rotación y reinicia el ciclo.

> [!NOTE]
> **Detección de Hardware Dinámica y Degradación Suave:** Todos los sensores de hardware (Giroscopio `QMI8658`, Micrófono `ES7210`, DAC `ES8311`, Sensor de temperatura `SHTC3`) se sondean dinámicamente en el bus I2C/I2S durante el arranque. Si un periférico o sensor no está presente, la funcionalidad se **desactiva automáticamente de forma segura sin bloqueos**, recurriendo al control manual a través de la Web UI.

- **Placa ESP32-S3 Waveshare RGB Matrix (`esp32s3_waveshare`)**: **100% Compatible con todas las características.** Altamente recomendada. Necesaria para paneles grandes **256x64 True Matrix**, streaming de WebRadio, rotación giroscópica y utiliza los sensores integrados (Decibelios, Temperatura, DAC) directamente de fábrica.
- **ESP32 Clásico (WROOM-32 / `esp32dev`)**: Procesador de doble núcleo Tensilica Xtensa LX6 @ 240MHz. Soporta animaciones principales, interfaz web, MUGEN y ahora todos los motores en la nube HTTPS/TLS (`crypto`, `stock`, `gnews`, `weather`) para paneles **128x32 / 64x32**. Los sensores físicos de audio/temperatura están ausentes en los DevKits estándar a menos que se conecten externamente.

## Compilación
Para compilar el firmware por tu cuenta, debes usar **PlatformIO**.
- Para 128x32: un ESP32 WROOM estándar es suficiente.
- Para 256x64: se recomienda encarecidamente un **ESP32-S3 con PSRAM** para evitar cuelgues por falta de memoria con doble búfer.

Ejecuta el siguiente comando para compilar:
```bash
pio run -e esp32dev
```

## 📚 Documentación adicional
- [Primeros pasos (instalación de PlatformIO, compilación, flasheo, logs)](docs/GETTING_STARTED_ES.md)
- [Web Installer (flasheo desde tu navegador, sin CLI)](webinstaller/README_ES.md) - *estará disponible cuando este repositorio sea público (GitHub Pages requiere un repositorio público en el plan gratuito); hasta entonces, usa el firmware precompilado de arriba.*
- [Guía de hardware](docs/HARDWARE_ES.md)
- [Guía de cableado](docs/WIRING_ES.md)
- [Guía de configuración](docs/CONFIGURATION_ES.md)
- [Guía para desarrolladores](docs/DEVELOPER_ES.md)
- [Guía de integración con Home Assistant](docs/HOME_ASSISTANT_ES.md)
- [Blueprints de Home Assistant](tools/home_assistant/blueprints/)
- [Arquitectura](docs/ARCHITECTURE_ES.md)

## 🙏 Agradecimientos

Un enorme agradecimiento a la comunidad de código abierto y a los creadores de las increíbles bibliotecas que impulsan este proyecto:
- **[ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA)** por mrfaptastic
- **[AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)** & **[PNGdec](https://github.com/bitbank2/PNGdec)** por bitbank2
- **[ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer)** por mathieucarbou
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)** por bblanchon
- **[PubSubClient](https://github.com/knolleary/pubsubclient)** por knolleary
- **[PicoMQTT](https://github.com/mlesniew/PicoMQTT)** por mlesniew
- **[Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)** por Adafruit
- **[SdFat](https://github.com/greiman/SdFat)** por greiman
- **[Clockwise](https://github.com/jnthas/clockwise)** por Jonathas Amaral Barbosa (@jnthas) por los diseños de relojes pixel art y assets retro de Clockwise (Mario, Pokédex, World Map, Words).
- **[@TooncesToo](https://github.com/TooncesToo)** (Erik Jerue) por desarrollar el motor de Home Assistant y Datos MQTT con blueprints listos para usar, la API de biblioteca GIF de red, subida de archivos múltiples, gestor de archivos Web UI con soporte de doble orientación tanto en ESP32 como en Raspberry Pi, optimizaciones de esferas de reloj y sus destacadas contribuciones al proyecto.

¡Un agradecimiento especial al **RPiTeam** por el increíble pack de 600 GIFs!

## 📜 Licencia
Este proyecto está licenciado bajo la **[PolyForm Noncommercial License 1.0.0](LICENSE)**.

**En resumen:** eres libre de usar, modificar y compartir este proyecto para cualquier propósito no comercial (uso personal, proyectos hobbyistas, investigación, educación, organizaciones públicas/sin fines de lucro) - consulta el archivo [LICENSE](LICENSE) completo para los términos exactos. **Cualquier uso comercial (venta de unidades ensambladas, kits, o productos/servicios derivados) requiere una licencia separada - contacta a [Red1L](https://github.com/red77290) para discutir los términos comerciales.**
