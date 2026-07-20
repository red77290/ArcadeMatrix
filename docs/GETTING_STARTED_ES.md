# Primeros pasos (firmware ESP32, primera configuración de PlatformIO)

🇬🇧 [English](GETTING_STARTED.md) | 🇫🇷 [Français](GETTING_STARTED_FR.md) | 🇪🇸 Español

Esta guía está pensada para desarrolladores que nunca han usado [PlatformIO](https://platformio.org/) y quieren compilar, flashear y depurar el firmware ArcadeMatrix en local. Para el cableado de hardware, consulta `docs/HARDWARE_ES.md`/`docs/WIRING_ES.md`; para las opciones de `conf.ini`, consulta `docs/CONFIGURATION_ES.md`; para la arquitectura del codebase, consulta `docs/ARCHITECTURE_ES.md`; para los flujos de contribución (añadir relojes, endpoints REST, fuentes personalizadas), consulta `docs/DEVELOPER_ES.md`.

## 1. Instalar PlatformIO

Solo necesitas **una** de estas opciones: elige la que mejor encaje con tu flujo de trabajo:

- **Extensión de VS Code (recomendada para principiantes):** instala
  [Visual Studio Code](https://code.visualstudio.com/), luego instala la
  [extensión PlatformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
  desde el marketplace de extensiones. Incluye su propio Python/toolchain; no hace falta ninguna instalación aparte.
- **Solo CLI** (funciona en cualquier terminal y no depende del editor):
  ```bash
  pip install -U platformio
  # or, on macOS with Homebrew:
  brew install platformio
  ```
  Verifica que funcione: `pio --version`.

Todos los comandos de abajo usan la CLI `pio`. Si utilizas la extensión de VS Code, las mismas acciones están disponibles en la barra lateral de PlatformIO (iconos de build/upload/monitor) y producen resultados idénticos; aquí se muestra la CLI porque se puede copiar/pegar y funciona igual en cualquier SO/editor.

## 2. Abrir el workspace

```bash
git clone <this-repo-url>
cd ArcadeMatrix
```

No hace falta `pio project init`: `platformio.ini` ya existe en la raíz del repositorio y define las dos placas compatibles como entornos de compilación separados: `esp32dev` (ESP32 clásico, 4MB flash) y `esp32s3` (ESP32-S3, 8MB flash + PSRAM opcional). Consulta `docs/HARDWARE_ES.md` para saber cuál corresponde a tu placa y sus límites específicos de GPIO/resolución.

## 3. Compilar el firmware

```bash
# Build both environments (fastest way to sanity-check your changes):
pio run -e esp32dev -e esp32s3

# Or just the one you actually own:
pio run -e esp32dev
```

La primera compilación descarga la toolchain de Espressif y todas las bibliotecas listadas en `lib_deps` de `platformio.ini` (driver HUB75 DMA, AnimatedGIF, PNGdec, ESPAsyncWebServer, ArduinoJson, etc.). Esto puede tardar unos minutos la primera vez y después queda cacheado en `~/.platformio/`. Una compilación correcta muestra un resumen de uso `RAM:`/`Flash:` y termina con `[SUCCESS]`.

## 4. Flashearlo en tu placa

Conecta el ESP32/ESP32-S3 por USB y luego:

```bash
pio run -e esp32dev -t upload      # replace esp32dev with esp32s3 if that's your board
```

PlatformIO detecta automáticamente el puerto serie en la mayoría de los casos. Si elige el incorrecto (por ejemplo, si tienes varios dispositivos USB-serie conectados), indícalo explícitamente:

```bash
pio device list                     # find the right port name
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0   # Linux/macOS example
pio run -e esp32dev -t upload --upload-port COM5           # Windows example
```

<a id="flashing-a-pre-built-release"></a>
### Flashear una release precompilada

Si no quieres compilar desde el código fuente, descarga `ArcadeMatrix-esp32dev.zip` o
`ArcadeMatrix-esp32s3.zip` desde la [última release](https://github.com/red77290/ArcadeMatrix/releases/latest)
en su lugar. Cada una contiene `firmware-*.bin`, `bootloader-*.bin`, `partitions-*.bin` y
`boot_app0.bin`, compilados y validados por la CI. Flashea los cuatro con `esptool.py` en los offsets usados
por el particionado predeterminado de Arduino-ESP32 (los mismos offsets que utiliza el Web Installer basado en navegador;
consulta `webinstaller/README_ES.md`):

```bash
pip install esptool
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash   0x1000  bootloader-esp32dev.bin   0x8000  partitions-esp32dev.bin   0xE000  boot_app0.bin   0x10000 firmware-esp32dev.bin
```

Para `esp32s3`, usa `--chip esp32s3` y el offset `0x0` para el bootloader en lugar de `0x1000` (la cabecera del bootloader ROM del S3 es distinta); consulta la tabla de offsets de flasheo en `webinstaller/README_ES.md` para ver el detalle completo.

## 5. Leer los logs serie

El firmware registra el progreso del arranque, el estado del Wi-Fi, el resultado del montaje de la tarjeta SD, el uso del heap y los errores/advertencias de ejecución por serie a `115200` baudios (consulta `monitor_speed` en `platformio.ini`):

```bash
pio device monitor -e esp32dev -b 115200
```

Pulsa `Ctrl+C` para salir. Para reiniciar la placa y ver los logs de arranque, pulsa `Ctrl+T` seguido de `Ctrl+R` en el monitor. Combina build + flash + monitor en un solo comando para un ciclo de desarrollo rápido:

```bash
pio run -e esp32dev -t upload && pio device monitor -e esp32dev -b 115200
```

## 6. Preparar la tarjeta SD

El firmware necesita una tarjeta SD externa (cableada según `docs/WIRING_ES.md`, chip-select en GPIO 5 por
defecto; consulta `SD_CS_PIN` en `src/main.cpp`) para:
- `/conf.ini` — tus ajustes de Wi-Fi/matriz/temas (se genera automáticamente con valores predeterminados en el primer arranque si falta; edítalo directamente en la tarjeta, o mediante `/api/settings` desde la interfaz web una vez que el Wi-Fi esté activo).
- `/gifs/`, playlists de assets `.gif`/`.raw`/`.png` (consulta §4 de `docs/ARCHITECTURE_ES.md` para ver las diferencias de formato entre los tres).
- `/fighters_32/` o `/fighters_64/` — hojas de sprites `.fgt` derivadas de MUGEN (consulta `tools/mugen_extractor/README_ES.md` para generar las tuyas a partir de archivos de personajes MUGEN).
- Opcionalmente `/fonts/*.amf` — fuentes bitmap personalizadas cargables desde la SD (consulta la sección «Cargar una fuente bitmap personalizada desde la SD» de `docs/DEVELOPER_ES.md` y `tools/bdf_to_amfont/`).

Se requiere una tarjeta formateada en FAT32 (lo estándar para tarjetas de hasta 32GB; puede que las tarjetas más grandes tengan que reformatearse de exFAT a FAT32).

## 7. Ejecutar la suite de tests

```bash
pio test -e esp32dev
```

**Advertencia importante:** `test/test_config/test_config.cpp` es un test Unity **sobre el hardware objetivo**; compila contra el core real de Arduino para ESP32 (`WiFi.h`, `FS.h`, etc.) y debe **subirse a una placa física** para ejecutarse (PlatformIO lo flashea y luego lee por serie los resultados pass/fail). Actualmente no existe un objetivo de test independiente del hardware («native»/host) para este firmware; consulta `docs/ARCHITECTURE_ES.md` y `docs/DEVELOPER_ES.md` para ver por qué (el codebase se apoya en APIs específicas de ESP32 como `SD.h`/`WiFi.h` por todas partes, que no tienen equivalentes de escritorio drop-in sin un esfuerzo mayor de mocking). Esta es también la razón por la que la CI (`.github/workflows/build.yml`) solo **compila** el objetivo de test (`pio test -e <env> --without-uploading --without-testing`) en lugar de ejecutarlo: los runners de GitHub Actions no tienen un ESP32 físico conectado, pero una pasada de solo compilación sigue detectando regresiones de build (includes obsoletos, firmas rotas, etc.) en cada push/PR. Si tienes una placa conectada localmente, el comando `pio test -e esp32dev` a secas (sin flags) es el adecuado para flashearlo y ejecutarlo de verdad.

## Solución de problemas

- **`pio: command not found`** después de `pip install`: el directorio de scripts de Python no está en tu `PATH`. Usa la extensión de VS Code en su lugar, o añade a tu perfil de shell la ruta `bin` que muestra `pip show -f platformio`.
- **La subida falla / expira por timeout**: mantén pulsado el botón `BOOT`/`IO0` de la placa mientras empieza la subida (algunas placas de desarrollo ESP32 lo necesitan para entrar en el bootloader), o reduce `upload_speed` en `platformio.ini`.
- **La compilación falla con un error de biblioteca ausente**: elimina `.pio/` y vuelve a compilar; una caché de bibliotecas corrupta es la causa más habitual (`rm -rf .pio && pio run -e esp32dev`).
- **`sdWait Failed` / `sdSelectCard Failed` / `Check status failed` unas pocas veces justo al arrancar, y luego el firmware continúa con normalidad (el Wi-Fi conecta, la hora NTP se muestra correctamente)**: esto es inofensivo - son simples reintentos internos del driver SD del ESP32 durante su handshake de inicialización (frecuente con ciertas tarjetas/marcas SD a la velocidad de sondeo por defecto), no un fallo real de montaje. Si la SD fallara de verdad, `setup()` mostraría `CRITICAL ERROR: SD Card Mount Failed!` y quedaría bloqueado indefinidamente (reiniciando vía watchdog cada ~30s) - nunca llegaría a la etapa de conexión Wi-Fi. Investiga el cableado/la alimentación solo si ves ese error crítico específico, o si las lecturas/escrituras SD siguen fallando bastante después del arranque (no solo al principio). Los errores `does not exist, no permits for creation` justo después son normales/inofensivos en el primer arranque (p. ej. `playlists_selected.json`, `fighters_32/index.txt` simplemente no existen todavía hasta que guardes una playlist / ejecutes `mugen_extractor`). Consulta la tabla "Cableado de la tarjeta SD" de `docs/WIRING_ES.md` solo si estás cableando una placa nueva desde cero.
- **`AsyncTCP.cpp: begin(): failed to start task`** justo después de conectar el Wi-Fi: FreeRTOS no puede asignar una tarea para la pila TCP asíncrona, casi siempre por falta de heap interno libre (los grandes buffers DMA del HUB75 en un ESP32 sin PSRAM pueden consumir la mayor parte). Revisa `ESP.getFreeHeap()` (impreso tras iniciar la matriz) - si solo quedan unos pocos KB, reduce `mxconfig.min_refresh_rate`/la resolución del panel, o pasa a un ESP32-S3 con PSRAM para paneles grandes. El servidor web puede arrancar parcialmente pese a esta advertencia, pero espera que sea inestable hasta resolver el heap libre.
