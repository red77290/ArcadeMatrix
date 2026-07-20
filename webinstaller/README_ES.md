# ArcadeMatrix Web Installer

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Esta carpeta contiene el código fuente de una página estática de flasheo con [ESP Web Tools](https://esphome.github.io/esp-web-tools/), publicada en GitHub Pages por el job `deploy-pages` de `.github/workflows/build.yml`
(se ejecuta en cada push a `main`, después de que ambas compilaciones del firmware terminen correctamente).

## Cómo funciona

- `index.html` incrusta dos widgets `<esp-web-install-button>` (uno por cada placa compatible), cada uno
  apuntando a un pequeño `manifest-*.json` que describe qué binario va en qué offset de flash.
- Los binarios reales del firmware (`firmware-esp32dev.bin`, `bootloader-esp32dev.bin`,
  `partitions-esp32dev.bin` y los equivalentes de `esp32s3`) **no** se commitean aquí: son artefactos de build recientes, copiados al sitio publicado por la CI desde `.pio/build/<env>/`. Esto mantiene el historial git libre de churn binario en cada commit y al mismo tiempo sirve siempre el último build de `main` a los visitantes.
- `bin/boot_app0.bin` sí está **commiteado**: es un archivo diminuto y fijo (8KB) proporcionado por el framework Arduino-ESP32 (selecciona qué partición OTA de aplicación arrancar) e idéntico en todos los builds, así que no tiene sentido regenerarlo / volver a alojarlo en cada compilación.

## Offsets de flash (particionado predeterminado de Arduino-ESP32)

| Archivo | ESP32 (clásico) | ESP32-S3 |
|---|---|---|
| bootloader | `0x1000` | `0x0` (la cabecera del bootloader ROM del S3 es distinta) |
| partitions | `0x8000` | `0x8000` |
| boot_app0  | `0xE000` | `0xE000` |
| firmware   | `0x10000` | `0x10000` |

Esto coincide exactamente con lo que `pio run -t upload -v` invoca internamente (verificado localmente con
`esptool.py ... write_flash`), así que un flasheo desde el navegador mediante esta página y un flasheo con `pio run -t upload`
producen el mismo resultado.

## Probarlo localmente

Abre `index.html` usando un servidor local de archivos estáticos (no `file://`, WebSerial necesita un origen real)
después de copiar binarios reales a esta carpeta para que coincidan con las rutas del manifest, por ejemplo:

```bash
pio run -e esp32dev -e esp32s3
cp .pio/build/esp32dev/bootloader.bin webinstaller/bootloader-esp32dev.bin
cp .pio/build/esp32dev/partitions.bin webinstaller/partitions-esp32dev.bin
cp .pio/build/esp32dev/firmware.bin   webinstaller/firmware-esp32dev.bin
cp .pio/build/esp32s3/bootloader.bin  webinstaller/bootloader-esp32s3.bin
cp .pio/build/esp32s3/partitions.bin  webinstaller/partitions-esp32s3.bin
cp .pio/build/esp32s3/firmware.bin    webinstaller/firmware-esp32s3.bin
cd webinstaller && python3 -m http.server 8080
```

Luego visita `http://localhost:8080` en Chrome/Edge con una placa conectada por USB.
